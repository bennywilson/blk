/// renderer_webgpu.cpp
///
/// 2026 blk

#include <cmath>
#include <fstream>
#if defined(__EMSCRIPTEN__)
	#include <emscripten/emscripten.h>
#endif
#include "blk_core.h"
#include "entity_header.h"
#include "renderer_webgpu.h"

namespace {
	/// Lets the browser run while startup waits on an adapter or a device.
	///
	/// Those callbacks only fire once control returns to the page, so a plain
	/// spin would hang the tab forever. emscripten_sleep() yields and resumes
	/// here, which is what the wasm build's ASYNCIFY is for. On Windows the
	/// callbacks arrive on this thread and the spin is fine.
	void pump_until_callback() {
#if defined(__EMSCRIPTEN__)
		emscripten_sleep(1);
#endif
	}
}

namespace {
	/// Dawn hands back a pointer and a length, not a C string.
	std::string to_string(const WGPUStringView view) {
		return (view.data != nullptr && view.length > 0) ? std::string(view.data, view.length) : std::string();
	}

	WGPUStringView label(const char* const text) {
		WGPUStringView view = {};
		view.data = text;
		view.length = strlen(text);
		return view;
	}

	/// FrameConstants
	///
	/// The bytes behind `scene_constants[0]` - GlobalConstantData in
	/// common_global.hlsli, GlobalUniformData in renderer_dx12.h. Same layout
	/// for both backends; the shader spike's cbuffer_layout_check.py is what
	/// says WGSL agrees with D3D12 about every offset here.
	struct FrameConstants {
		Mat4 view;
		Mat4 view_projection;
		Mat4 inv_view_proj;
		Vec4 camera;
		Vec4 splat_params;
		Vec4 splat_params_2;
		Vec4 srv_heap_base;
		Vec4 pad[16];
	};
	static_assert(sizeof(FrameConstants) == 512, "FrameConstants must match BaseData's 512 bytes");

	/// DrawConstants
	///
	/// The bytes behind `scene_constants[scene_index.index]` - SceneData in the
	/// shaders, SceneInstanceData in renderer_dx12.h.
	struct DrawConstants {
		Mat4 mvp;
		Mat4 world;
		Mat4 inv_world;
		Vec4 color;
		Vec4 spec;
		Vec4 time_since_spawn;
		f32 texture_list[16];
		Vec4 entity_id;
		Vec4 pad[12];
	};
	static_assert(offsetof(DrawConstants, entity_id) == 304, "entity_id sits immediately after texture_list");
	static_assert(sizeof(DrawConstants) == 512, "DrawConstants must match BaseData's 512 bytes");

	/// BoneConstants
	///
	/// `BoneData` in skinned_model.hlsl: the 128 bone matrices one skinned draw
	/// blends between.
	struct BoneConstants {
		Mat4 bones[128];
	};
	static_assert(sizeof(BoneConstants) == 8192, "BoneConstants must match BoneData");

	constexpr u32 k_frame_binding = 0;
	constexpr u32 k_sampler_binding = 32;
	constexpr u32 k_texture_binding = 16;
	constexpr u32 k_draw_binding = 0;
	constexpr u32 k_bone_binding = 1;

	// The five colour targets the material pixel shaders write, in SV_TARGET
	// order. Named here rather than shared: ERenderTarget, D3D12's enum for the
	// same targets, sits in renderer_dx12.h, and moving it into shared code is
	// its own item on the roadmap's cleanup list.
	enum GBufferSlot {
		Slot_Color = 0,
		Slot_Normal,
		Slot_Specular,
		Slot_SceneDepth,
		Slot_EntityId,
	};

	// Color/Normal/Specular are RGBA8; SceneDepth and EntityId are R32_FLOAT
	// colour targets written by an ordinary pixel shader, not depth surfaces.
	constexpr WGPUTextureFormat k_gbuffer_formats[] = {
		WGPUTextureFormat_RGBA8Unorm,
		WGPUTextureFormat_RGBA8Unorm,
		WGPUTextureFormat_RGBA8Unorm,
		WGPUTextureFormat_R32Float,
		WGPUTextureFormat_R32Float,
	};

	/// DdsImage
	///
	/// Just enough DDS to load what the engine ships: BC1/BC3 blocks, which go
	/// to the GPU compressed and untouched, and 32-bit uncompressed, which is
	/// what the terrain height map is. No decoder is needed for the compressed
	/// ones - WebGPU takes the blocks as they are, given texture-compression-bc.
	struct DdsImage {
		u32 width = 0;
		u32 height = 0;
		u32 mip_count = 1;
		WGPUTextureFormat format = WGPUTextureFormat_Undefined;
		u32 block_bytes = 0; // per 4x4 block, compressed only
		u32 pixel_bytes = 0; // per pixel, uncompressed only
		const u8* pixels = nullptr;
		size_t pixels_size = 0;

		bool compressed() const { return block_bytes > 0; }
	};

	u32 read_u32(const std::vector<u8>& bytes, const size_t offset) {
		u32 value = 0;
		memcpy(&value, bytes.data() + offset, sizeof(value));
		return value;
	}

	/// parse_dds
	///
	/// Offsets are the fixed DDS_HEADER layout: height 12, width 16, mip count
	/// 28, and the pixel format's FourCC at 84.
	bool parse_dds(const std::vector<u8>& file, DdsImage& out) {
		constexpr size_t k_header_end = 128;
		if (file.size() < k_header_end || memcmp(file.data(), "DDS ", 4) != 0) {
			return false;
		}

		out.height = read_u32(file, 12);
		out.width = read_u32(file, 16);
		out.mip_count = (std::max)(read_u32(file, 28), 1u);

		const u32 four_cc = read_u32(file, 84);
		size_t data_start = k_header_end;

		if (four_cc == 0x31545844) { // 'DXT1'
			out.format = WGPUTextureFormat_BC1RGBAUnorm;
			out.block_bytes = 8;
		} else if (four_cc == 0x33545844) { // 'DXT3'
			out.format = WGPUTextureFormat_BC2RGBAUnorm;
			out.block_bytes = 16;
		} else if (four_cc == 0x35545844) { // 'DXT5'
			out.format = WGPUTextureFormat_BC3RGBAUnorm;
			out.block_bytes = 16;
		} else if (four_cc == 0) {
			// Uncompressed: the channel masks say which way round it is stored.
			const u32 bit_count = read_u32(file, 88);
			const u32 red_mask = read_u32(file, 92);
			if (bit_count != 32) {
				return false;
			}

			out.format = (red_mask == 0x00FF0000) ? WGPUTextureFormat_BGRA8Unorm : WGPUTextureFormat_RGBA8Unorm;
			out.pixel_bytes = 4;
		} else {
			return false;
		}

		out.pixels = file.data() + data_start;
		out.pixels_size = file.size() - data_start;
		return true;
	}

	/// RenderPipeline_WebGpu
	///
	/// The engine's pipeline objects are still the D3D12-shaped "load one file,
	/// get one pipeline" kind. This backend builds its pipelines itself from the
	/// generated WGSL, so these stay empty for now.
	class RenderPipeline_WebGpu : public RenderPipeline {
	public:
		void release() override {}
	};

	void on_uncaptured_error(WGPUDevice const*, WGPUErrorType type, WGPUStringView message, void*, void*) {
		blk::warn("Renderer_WebGpu - device error %d: %s", (int)type, to_string(message).c_str());
	}

	void on_device_lost(WGPUDevice const*, WGPUDeviceLostReason reason, WGPUStringView message, void*, void*) {
		blk::warn("Renderer_WebGpu - device lost %d: %s", (int)reason, to_string(message).c_str());
	}

	/// Puts the gbuffer's Color target on the screen. Temporary: the real
	/// frame ends with the lighting and post-process passes, which this
	/// backend does not run yet, so without it the window shows nothing.
	const char* const k_blit_wgsl = R"(
@group(0) @binding(0) var blit_sampler: sampler;
@group(0) @binding(1) var blit_texture: texture_2d<f32>;

struct VertexOut {
	@builtin(position) position: vec4<f32>,
	@location(0) uv: vec2<f32>,
};

@vertex
fn vertex_main(@builtin(vertex_index) index: u32) -> VertexOut {
	// One oversized triangle covering the viewport.
	var out: VertexOut;
	let x = f32((index << 1u) & 2u) * 2.0 - 1.0;
	let y = f32(index & 2u) * 2.0 - 1.0;
	out.position = vec4<f32>(x, y, 0.0, 1.0);

	// v follows clip-space y rather than opposing it, which mirrors the image:
	// the engine's gbuffer comes out vertically flipped relative to the screen,
	// and on D3D12 it is the lighting pass's fullscreen quad that puts it back.
	// Checked against the world: the floor sits below the camera, and only this
	// way round does it land in the lower half of the window.
	out.uv = vec2<f32>((x + 1.0) * 0.5, (y + 1.0) * 0.5);
	return out;
}

@fragment
fn fragment_main(in: VertexOut) -> @location(0) vec4<f32> {
	return textureSample(blit_texture, blit_sampler, in.uv);
}
)";
}

/// RenderBuffer_WebGpu
///
/// `RenderBuffer`'s contract is map() / write / unmap(), so the staging copy is
/// what callers write into and unmap() is where it reaches the GPU. Usage
/// covers both vertex and index, since the base class does not say which a
/// buffer is when it creates it.
class RenderBuffer_WebGpu : public RenderBuffer {
public:
	explicit RenderBuffer_WebGpu(Renderer_WebGpu* const owner) :
		m_owner(owner) {}

	u8* map() override {
		m_staging.resize(padded_size());
		return m_staging.data();
	}

	void unmap() override {
		if (m_buffer != nullptr && !m_staging.empty()) {
			wgpuQueueWriteBuffer(m_owner->queue(), m_buffer, 0, m_staging.data(), m_staging.size());
		}
	}

	void release() override {
		if (m_buffer != nullptr) {
			wgpuBufferRelease(m_buffer);
			m_buffer = nullptr;
		}
		m_staging.clear();
		m_staging.shrink_to_fit();
	}

	WGPUBuffer buffer() const { return m_buffer; }
	u64 buffer_size() const { return padded_size(); }

private:
	// wgpuQueueWriteBuffer works in 4-byte units, and a uint16 index buffer with
	// an odd count is not a multiple of 4.
	u64 padded_size() const { return (size_bytes() + 3u) & ~3u; }

	void create_internal() override {
		release();
		if (size_bytes() == 0) {
			return;
		}

		WGPUBufferDescriptor descriptor = {};
		descriptor.size = padded_size();
		descriptor.usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_Index | WGPUBufferUsage_CopyDst;
		m_buffer = wgpuDeviceCreateBuffer(m_owner->device(), &descriptor);
	}

	Renderer_WebGpu* m_owner = nullptr;
	WGPUBuffer m_buffer = nullptr;
	std::vector<u8> m_staging;
};

/// Renderer_WebGpu::~Renderer_WebGpu
Renderer_WebGpu::~Renderer_WebGpu() {
}

/// Renderer_WebGpu::request_adapter
bool Renderer_WebGpu::request_adapter() {
	struct Result {
		WGPUAdapter adapter = nullptr;
		bool done = false;
	} result;

	WGPURequestAdapterOptions options = {};
	options.featureLevel = WGPUFeatureLevel_Core;
	options.compatibleSurface = m_surface;

	WGPURequestAdapterCallbackInfo callback = {};
	callback.mode = WGPUCallbackMode_AllowProcessEvents;
	callback.userdata1 = &result;
	callback.callback = [](WGPURequestAdapterStatus status, WGPUAdapter adapter, WGPUStringView message, void* userdata, void*) {
		Result* const out = (Result*)userdata;
		if (status != WGPURequestAdapterStatus_Success) {
			blk::warn("Renderer_WebGpu - requestAdapter failed: %s", to_string(message).c_str());
		}
		out->adapter = adapter;
		out->done = true;
	};

	wgpuInstanceRequestAdapter(m_instance, &options, callback);
	while (!result.done) {
		wgpuInstanceProcessEvents(m_instance);
		pump_until_callback();
	}


	m_adapter = result.adapter;
	return m_adapter != nullptr;
}

/// Renderer_WebGpu::request_device
bool Renderer_WebGpu::request_device() {
	struct Result {
		WGPUDevice device = nullptr;
		bool done = false;
	} result;

	// BC is what the engine's .dds files are; without it they would have to be
	// decompressed on the CPU. Every desktop GPU has it, phones often do not.
	WGPUFeatureName required_features[1] = { WGPUFeatureName_TextureCompressionBC };
	m_block_compression = wgpuAdapterHasFeature(m_adapter, WGPUFeatureName_TextureCompressionBC) != 0;
	if (!m_block_compression) {
		blk::warn("Renderer_WebGpu - no texture-compression-bc; the engine's BC textures cannot be loaded");
	}

	WGPUDeviceDescriptor descriptor = {};
	descriptor.requiredFeatureCount = m_block_compression ? 1 : 0;
	descriptor.requiredFeatures = required_features;
	descriptor.uncapturedErrorCallbackInfo.callback = on_uncaptured_error;
	descriptor.deviceLostCallbackInfo.mode = WGPUCallbackMode_AllowProcessEvents;
	descriptor.deviceLostCallbackInfo.callback = on_device_lost;

	WGPURequestDeviceCallbackInfo callback = {};
	callback.mode = WGPUCallbackMode_AllowProcessEvents;
	callback.userdata1 = &result;
	callback.callback = [](WGPURequestDeviceStatus status, WGPUDevice device, WGPUStringView message, void* userdata, void*) {
		Result* const out = (Result*)userdata;
		if (status != WGPURequestDeviceStatus_Success) {
			blk::warn("Renderer_WebGpu - requestDevice failed: %s", to_string(message).c_str());
		}
		out->device = device;
		out->done = true;
	};

	wgpuAdapterRequestDevice(m_adapter, &descriptor, callback);
	while (!result.done) {
		wgpuInstanceProcessEvents(m_instance);
		pump_until_callback();
	}

	m_device = result.device;
	return m_device != nullptr;
}

/// Renderer_WebGpu::create_frame_targets
void Renderer_WebGpu::create_frame_targets() {
	for (u32 i = 0; i < k_gbuffer_target_count; i++) {
		WGPUTextureDescriptor descriptor = {};
		descriptor.dimension = WGPUTextureDimension_2D;
		descriptor.size = { frame_width(), frame_height(), 1 };
		descriptor.format = k_gbuffer_formats[i];
		descriptor.mipLevelCount = 1;
		descriptor.sampleCount = 1;
		descriptor.usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_TextureBinding;

		m_gbuffer[i] = wgpuDeviceCreateTexture(m_device, &descriptor);
		m_gbuffer_view[i] = wgpuTextureCreateView(m_gbuffer[i], nullptr);
	}

	WGPUTextureDescriptor depth_descriptor = {};
	depth_descriptor.dimension = WGPUTextureDimension_2D;
	depth_descriptor.size = { frame_width(), frame_height(), 1 };
	depth_descriptor.format = WGPUTextureFormat_Depth24Plus;
	depth_descriptor.mipLevelCount = 1;
	depth_descriptor.sampleCount = 1;
	depth_descriptor.usage = WGPUTextureUsage_RenderAttachment;

	m_depth_target = wgpuDeviceCreateTexture(m_device, &depth_descriptor);
	m_depth_view = wgpuTextureCreateView(m_depth_target, nullptr);
}

/// Renderer_WebGpu::create_bind_group_layouts
///
/// One layout per update frequency, matching the groups the generated WGSL
/// declares. The draw group's buffer is dynamic: one uniform buffer holds every
/// draw's constants and each draw binds its own slice by offset, which is the
/// WebGPU equivalent of D3D12 indexing the scene_constants[] table.
void Renderer_WebGpu::create_bind_group_layouts() {
	WGPULimits limits = {};
	wgpuDeviceGetLimits(m_device, &limits);
	const u32 alignment = (limits.minUniformBufferOffsetAlignment > 0) ? limits.minUniformBufferOffsetAlignment : 256;
	m_draw_stride = ((u32)sizeof(DrawConstants) + alignment - 1) & ~(alignment - 1);
	m_max_draws = 4096;

	WGPUBindGroupLayoutEntry frame_entries[2] = {};
	frame_entries[0].binding = k_frame_binding;
	frame_entries[0].visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
	frame_entries[0].buffer.type = WGPUBufferBindingType_Uniform;
	frame_entries[0].buffer.minBindingSize = sizeof(FrameConstants);
	frame_entries[1].binding = k_sampler_binding;
	frame_entries[1].visibility = WGPUShaderStage_Fragment;
	frame_entries[1].sampler.type = WGPUSamplerBindingType_Filtering;

	WGPUBindGroupLayoutDescriptor frame_layout = {};
	frame_layout.label = label("frame");
	frame_layout.entryCount = 2;
	frame_layout.entries = frame_entries;
	m_frame_layout = wgpuDeviceCreateBindGroupLayout(m_device, &frame_layout);

	WGPUBindGroupLayoutEntry material_entry = {};
	material_entry.binding = k_texture_binding;
	material_entry.visibility = WGPUShaderStage_Fragment;
	material_entry.texture.sampleType = WGPUTextureSampleType_Float;
	material_entry.texture.viewDimension = WGPUTextureViewDimension_2D;

	WGPUBindGroupLayoutDescriptor material_layout = {};
	material_layout.label = label("material");
	material_layout.entryCount = 1;
	material_layout.entries = &material_entry;
	m_material_layout = wgpuDeviceCreateBindGroupLayout(m_device, &material_layout);

	// Both are dynamic, and the offsets are passed in binding order, so a static
	// draw still supplies a bone offset - it just never reads the binding.
	WGPUBindGroupLayoutEntry draw_entries[2] = {};
	draw_entries[0].binding = k_draw_binding;
	draw_entries[0].visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
	draw_entries[0].buffer.type = WGPUBufferBindingType_Uniform;
	draw_entries[0].buffer.hasDynamicOffset = true;
	draw_entries[0].buffer.minBindingSize = sizeof(DrawConstants);
	draw_entries[1].binding = k_bone_binding;
	draw_entries[1].visibility = WGPUShaderStage_Vertex;
	draw_entries[1].buffer.type = WGPUBufferBindingType_Uniform;
	draw_entries[1].buffer.hasDynamicOffset = true;
	draw_entries[1].buffer.minBindingSize = sizeof(BoneConstants);

	WGPUBindGroupLayoutDescriptor draw_layout = {};
	draw_layout.label = label("draw");
	draw_layout.entryCount = 2;
	draw_layout.entries = draw_entries;
	m_draw_layout = wgpuDeviceCreateBindGroupLayout(m_device, &draw_layout);

	WGPUBindGroupLayout groups[] = { m_frame_layout, m_material_layout, m_draw_layout };
	WGPUPipelineLayoutDescriptor pipeline_layout = {};
	pipeline_layout.label = label("material");
	pipeline_layout.bindGroupLayoutCount = 3;
	pipeline_layout.bindGroupLayouts = groups;
	m_material_pipeline_layout = wgpuDeviceCreatePipelineLayout(m_device, &pipeline_layout);

	WGPUBufferDescriptor frame_buffer = {};
	frame_buffer.size = sizeof(FrameConstants);
	frame_buffer.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
	m_frame_constants = wgpuDeviceCreateBuffer(m_device, &frame_buffer);

	WGPUBufferDescriptor draw_buffer = {};
	draw_buffer.size = (u64)m_draw_stride * m_max_draws;
	draw_buffer.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
	m_draw_constants = wgpuDeviceCreateBuffer(m_device, &draw_buffer);
	m_draw_staging.resize((size_t)m_draw_stride * m_max_draws);

	// 8 KB a draw, so this is sized for the skinned models actually on screen
	// rather than for m_max_draws.
	m_bone_stride = ((u32)sizeof(BoneConstants) + alignment - 1) & ~(alignment - 1);
	m_max_bone_draws = 256;

	WGPUBufferDescriptor bone_buffer = {};
	bone_buffer.size = (u64)m_bone_stride * m_max_bone_draws;
	bone_buffer.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
	m_bone_constants = wgpuDeviceCreateBuffer(m_device, &bone_buffer);
	m_bone_staging.resize((size_t)m_bone_stride * m_max_bone_draws);

	WGPUSamplerDescriptor sampler = {};
	sampler.addressModeU = WGPUAddressMode_Repeat;
	sampler.addressModeV = WGPUAddressMode_Repeat;
	sampler.addressModeW = WGPUAddressMode_Repeat;
	sampler.magFilter = WGPUFilterMode_Linear;
	sampler.minFilter = WGPUFilterMode_Linear;
	sampler.mipmapFilter = WGPUMipmapFilterMode_Linear;
	sampler.maxAnisotropy = 1;
	m_sampler = wgpuDeviceCreateSampler(m_device, &sampler);

	WGPUBindGroupEntry frame_bindings[2] = {};
	frame_bindings[0].binding = k_frame_binding;
	frame_bindings[0].buffer = m_frame_constants;
	frame_bindings[0].size = sizeof(FrameConstants);
	frame_bindings[1].binding = k_sampler_binding;
	frame_bindings[1].sampler = m_sampler;

	WGPUBindGroupDescriptor frame_group = {};
	frame_group.layout = m_frame_layout;
	frame_group.entryCount = 2;
	frame_group.entries = frame_bindings;
	m_frame_bind_group = wgpuDeviceCreateBindGroup(m_device, &frame_group);

	WGPUBindGroupEntry draw_bindings[2] = {};
	draw_bindings[0].binding = k_draw_binding;
	draw_bindings[0].buffer = m_draw_constants;
	draw_bindings[0].size = sizeof(DrawConstants);
	draw_bindings[1].binding = k_bone_binding;
	draw_bindings[1].buffer = m_bone_constants;
	draw_bindings[1].size = sizeof(BoneConstants);

	WGPUBindGroupDescriptor draw_group = {};
	draw_group.layout = m_draw_layout;
	draw_group.entryCount = 2;
	draw_group.entries = draw_bindings;
	m_draw_bind_group = wgpuDeviceCreateBindGroup(m_device, &draw_group);
}

/// Renderer_WebGpu::create_default_material
void Renderer_WebGpu::create_default_material() {
	WGPUTextureDescriptor descriptor = {};
	descriptor.dimension = WGPUTextureDimension_2D;
	descriptor.size = { 1, 1, 1 };
	descriptor.format = WGPUTextureFormat_RGBA8Unorm;
	descriptor.mipLevelCount = 1;
	descriptor.sampleCount = 1;
	descriptor.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
	WGPUTexture texture = wgpuDeviceCreateTexture(m_device, &descriptor);

	const u8 white_pixel[4] = { 255, 255, 255, 255 };
	WGPUTexelCopyTextureInfo destination = {};
	destination.texture = texture;
	WGPUTexelCopyBufferLayout layout = {};
	layout.bytesPerRow = 4;
	layout.rowsPerImage = 1;
	WGPUExtent3D extent = { 1, 1, 1 };
	wgpuQueueWriteTexture(m_queue, &destination, white_pixel, sizeof(white_pixel), &layout, &extent);

	MaterialTexture white = {};
	white.texture = texture;
	white.view = wgpuTextureCreateView(texture, nullptr);

	WGPUBindGroupEntry binding = {};
	binding.binding = k_texture_binding;
	binding.textureView = white.view;

	WGPUBindGroupDescriptor group = {};
	group.label = label("default material");
	group.layout = m_material_layout;
	group.entryCount = 1;
	group.entries = &binding;
	white.bind_group = wgpuDeviceCreateBindGroup(m_device, &group);

	m_textures.clear();
	m_textures.push_back(white);
}

/// Renderer_WebGpu::material_bind_group
///
/// Anything unknown - no texture on the material, or one that failed to load -
/// falls back to the white pixel at index 0.
WGPUBindGroup Renderer_WebGpu::material_bind_group(const u32 texture_id) const {
	return (texture_id < m_textures.size()) ? m_textures[texture_id].bind_group : m_textures[0].bind_group;
}

/// Renderer_WebGpu::load_wgsl
WGPUShaderModule Renderer_WebGpu::load_wgsl(const std::string& file_name) {
	const std::string path = "../blk_engine/assets/shaders/wgsl/" + file_name;
	std::ifstream file(blk::os_path(path));
	if (file.fail()) {
		blk::warn("Renderer_WebGpu - could not open %s", path.c_str());
		return nullptr;
	}

	const std::string source((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

	WGPUShaderSourceWGSL wgsl = {};
	wgsl.chain.sType = WGPUSType_ShaderSourceWGSL;
	wgsl.code = label(source.c_str());

	WGPUShaderModuleDescriptor descriptor = {};
	descriptor.nextInChain = &wgsl.chain;
	descriptor.label = label(file_name.c_str());
	return wgpuDeviceCreateShaderModule(m_device, &descriptor);
}

/// Renderer_WebGpu::create_material_pipeline
///
/// One pipeline per material shader, writing the five gbuffer targets. The
/// vertex layout is the engine's `vertexLayout`; only position and uv are
/// declared, because that is all the generated vertex shaders read.
WGPURenderPipeline Renderer_WebGpu::create_material_pipeline(const std::string& shader_name, const bool skinned) {
	WGPUShaderModule vertex_module = load_wgsl(shader_name + ".vertex_shader.wgsl");
	WGPUShaderModule fragment_module = load_wgsl(shader_name + ".pixel_shader.wgsl");
	if (vertex_module == nullptr || fragment_module == nullptr) {
		return nullptr;
	}

	// Locations follow the order the semantics are DECLARED in the HLSL, which
	// is not the order they sit in the vertex: the skinned shader declares
	// NORMAL before COLOR, so location 2 reads the normal at offset 24 while
	// location 3 reads the blend indices at offset 20. Bone indices arrive as
	// unorm and the shader multiplies them back up by 255.
	WGPUVertexAttribute attributes[5] = {};
	attributes[0].format = WGPUVertexFormat_Float32x3;
	attributes[0].offset = offsetof(vertexLayout, position);
	attributes[0].shaderLocation = 0;
	attributes[1].format = WGPUVertexFormat_Float32x2;
	attributes[1].offset = offsetof(vertexLayout, uv);
	attributes[1].shaderLocation = 1;
	attributes[2].format = WGPUVertexFormat_Unorm8x4;
	attributes[2].offset = offsetof(vertexLayout, normal);
	attributes[2].shaderLocation = 2;
	attributes[3].format = WGPUVertexFormat_Unorm8x4;
	attributes[3].offset = offsetof(vertexLayout, color);
	attributes[3].shaderLocation = 3;
	attributes[4].format = WGPUVertexFormat_Unorm8x4;
	attributes[4].offset = offsetof(vertexLayout, tangent);
	attributes[4].shaderLocation = 4;

	WGPUVertexBufferLayout vertex_buffer = {};
	vertex_buffer.arrayStride = sizeof(vertexLayout);
	vertex_buffer.stepMode = WGPUVertexStepMode_Vertex;
	vertex_buffer.attributeCount = skinned ? 5 : 2;
	vertex_buffer.attributes = attributes;

	WGPUColorTargetState targets[k_gbuffer_target_count] = {};
	for (u32 i = 0; i < k_gbuffer_target_count; i++) {
		targets[i].format = k_gbuffer_formats[i];
		targets[i].writeMask = WGPUColorWriteMask_All;
	}

	WGPUFragmentState fragment = {};
	fragment.module = fragment_module;
	fragment.entryPoint = label("pixel_shader");
	fragment.targetCount = k_gbuffer_target_count;
	fragment.targets = targets;

	WGPUDepthStencilState depth = {};
	depth.format = WGPUTextureFormat_Depth24Plus;
	depth.depthWriteEnabled = WGPUOptionalBool_True;
	depth.depthCompare = WGPUCompareFunction_Less;
	depth.stencilFront.compare = WGPUCompareFunction_Always;
	depth.stencilBack.compare = WGPUCompareFunction_Always;

	WGPURenderPipelineDescriptor descriptor = {};
	descriptor.label = label(shader_name.c_str());
	descriptor.layout = m_material_pipeline_layout;
	descriptor.vertex.module = vertex_module;
	descriptor.vertex.entryPoint = label("vertex_shader");
	descriptor.vertex.bufferCount = 1;
	descriptor.vertex.buffers = &vertex_buffer;
	descriptor.primitive.topology = WGPUPrimitiveTopology_TriangleList;
	// No culling yet: the engine's winding is the D3D12 convention and getting
	// it wrong here would silently drop half the scene.
	descriptor.primitive.cullMode = WGPUCullMode_None;
	descriptor.multisample.count = 1;
	descriptor.multisample.mask = 0xFFFFFFFF;
	descriptor.depthStencil = &depth;
	descriptor.fragment = &fragment;

	return wgpuDeviceCreateRenderPipeline(m_device, &descriptor);
}

/// Renderer_WebGpu::create_blit_pipeline
void Renderer_WebGpu::create_blit_pipeline() {
	WGPUShaderSourceWGSL wgsl = {};
	wgsl.chain.sType = WGPUSType_ShaderSourceWGSL;
	wgsl.code = label(k_blit_wgsl);

	WGPUShaderModuleDescriptor module_descriptor = {};
	module_descriptor.nextInChain = &wgsl.chain;
	module_descriptor.label = label("blit");
	WGPUShaderModule module = wgpuDeviceCreateShaderModule(m_device, &module_descriptor);

	WGPUBindGroupLayoutEntry entries[2] = {};
	entries[0].binding = 0;
	entries[0].visibility = WGPUShaderStage_Fragment;
	entries[0].sampler.type = WGPUSamplerBindingType_Filtering;
	entries[1].binding = 1;
	entries[1].visibility = WGPUShaderStage_Fragment;
	entries[1].texture.sampleType = WGPUTextureSampleType_Float;
	entries[1].texture.viewDimension = WGPUTextureViewDimension_2D;

	WGPUBindGroupLayoutDescriptor layout_descriptor = {};
	layout_descriptor.entryCount = 2;
	layout_descriptor.entries = entries;
	m_blit_layout = wgpuDeviceCreateBindGroupLayout(m_device, &layout_descriptor);

	WGPUPipelineLayoutDescriptor pipeline_layout = {};
	pipeline_layout.bindGroupLayoutCount = 1;
	pipeline_layout.bindGroupLayouts = &m_blit_layout;

	WGPUColorTargetState target = {};
	target.format = m_surface_format;
	target.writeMask = WGPUColorWriteMask_All;

	WGPUFragmentState fragment = {};
	fragment.module = module;
	fragment.entryPoint = label("fragment_main");
	fragment.targetCount = 1;
	fragment.targets = &target;

	WGPURenderPipelineDescriptor descriptor = {};
	descriptor.label = label("blit");
	descriptor.layout = wgpuDeviceCreatePipelineLayout(m_device, &pipeline_layout);
	descriptor.vertex.module = module;
	descriptor.vertex.entryPoint = label("vertex_main");
	descriptor.primitive.topology = WGPUPrimitiveTopology_TriangleList;
	descriptor.multisample.count = 1;
	descriptor.multisample.mask = 0xFFFFFFFF;
	descriptor.fragment = &fragment;
	m_blit_pipeline = wgpuDeviceCreateRenderPipeline(m_device, &descriptor);

	WGPUBindGroupEntry bindings[2] = {};
	bindings[0].binding = 0;
	bindings[0].sampler = m_sampler;
	bindings[1].binding = 1;
	// Point this at another slot to inspect one - Slot_Normal is the useful one
	// for checking geometry and skinning before textures exist.
	bindings[1].textureView = m_gbuffer_view[Slot_Color];

	WGPUBindGroupDescriptor group = {};
	group.layout = m_blit_layout;
	group.entryCount = 2;
	group.entries = bindings;
	m_blit_bind_group = wgpuDeviceCreateBindGroup(m_device, &group);
}

/// Renderer_WebGpu::initialize_internal
void Renderer_WebGpu::initialize_internal(HWND hwnd, const uint32_t frame_width, const uint32_t frame_height) {
	m_instance = wgpuCreateInstance(nullptr);
	blk::error_check(m_instance != nullptr, "Renderer_WebGpu - wgpuCreateInstance() failed");

	// The surface comes first: the adapter is requested as compatible with it.
	// Where it comes from is the one real platform difference - a window handle
	// natively, the page's canvas in the browser.
	WGPUSurfaceDescriptor surface_descriptor = {};
#if defined(__EMSCRIPTEN__)
	WGPUEmscriptenSurfaceSourceCanvasHTMLSelector from_canvas = {};
	from_canvas.chain.sType = WGPUSType_EmscriptenSurfaceSourceCanvasHTMLSelector;
	from_canvas.selector = label("#canvas");
	surface_descriptor.nextInChain = &from_canvas.chain;
#else
	WGPUSurfaceSourceWindowsHWND from_hwnd = {};
	from_hwnd.chain.sType = WGPUSType_SurfaceSourceWindowsHWND;
	from_hwnd.hinstance = GetModuleHandle(nullptr);
	from_hwnd.hwnd = hwnd;
	surface_descriptor.nextInChain = &from_hwnd.chain;
#endif
	m_surface = wgpuInstanceCreateSurface(m_instance, &surface_descriptor);
	blk::error_check(m_surface != nullptr, "Renderer_WebGpu - wgpuInstanceCreateSurface() failed");

	blk::error_check(request_adapter(), "Renderer_WebGpu - no adapter");

	WGPUAdapterInfo info = {};
	wgpuAdapterGetInfo(m_adapter, &info);
	blk::log("Renderer_WebGpu - adapter '%s' (backend %d)", to_string(info.device).c_str(), (int)info.backendType);

	blk::error_check(request_device(), "Renderer_WebGpu - no device");
	m_queue = wgpuDeviceGetQueue(m_device);

	// Dawn lists the surface's preferred format first.
	WGPUSurfaceCapabilities capabilities = {};
	wgpuSurfaceGetCapabilities(m_surface, m_adapter, &capabilities);
	m_surface_format = (capabilities.formatCount > 0) ? capabilities.formats[0] : WGPUTextureFormat_BGRA8Unorm;

	WGPUSurfaceConfiguration configuration = {};
	configuration.device = m_device;
	configuration.format = m_surface_format;
	configuration.usage = WGPUTextureUsage_RenderAttachment;
	configuration.width = frame_width;
	configuration.height = frame_height;
	configuration.alphaMode = WGPUCompositeAlphaMode_Auto;
	configuration.presentMode = WGPUPresentMode_Fifo;
	wgpuSurfaceConfigure(m_surface, &configuration);

	create_frame_targets();
	create_bind_group_layouts();
	create_default_material();
	create_blit_pipeline();

	m_material_pipelines["static_model"] = create_material_pipeline("static_model", false);
	m_material_pipelines["skinned_model"] = create_material_pipeline("skinned_model", true);

	blk::log("Renderer_WebGpu initialized - %ux%u, surface format %d, draw stride %u",
		frame_width, frame_height, (int)m_surface_format, m_draw_stride);
}

/// Renderer_WebGpu::shut_down_internal
void Renderer_WebGpu::shut_down_internal() {
	if (m_surface != nullptr) {
		wgpuSurfaceUnconfigure(m_surface);
		wgpuSurfaceRelease(m_surface);
		m_surface = nullptr;
	}

	if (m_queue != nullptr) {
		wgpuQueueRelease(m_queue);
		m_queue = nullptr;
	}

	if (m_device != nullptr) {
		wgpuDeviceRelease(m_device);
		m_device = nullptr;
	}

	if (m_adapter != nullptr) {
		wgpuAdapterRelease(m_adapter);
		m_adapter = nullptr;
	}

	if (m_instance != nullptr) {
		wgpuInstanceRelease(m_instance);
		m_instance = nullptr;
	}

	blk::log("Renderer_WebGpu - shut down after %u frames", m_frame_index);
}

/// Renderer_WebGpu::begin_frame_resources
void Renderer_WebGpu::begin_frame_resources() {
	if (m_device == nullptr) {
		return;
	}

	WGPUCommandEncoderDescriptor descriptor = {};
	descriptor.label = label("frame");
	m_encoder = wgpuDeviceCreateCommandEncoder(m_device, &descriptor);
}

/// Renderer_WebGpu::get_pass_execute
///
/// Only "gbuffer" so far; every other pass returns nullptr and is skipped by
/// run_render_graph() with nothing touched.
RenderGraph::ExecuteFn Renderer_WebGpu::get_pass_execute(const std::string& pass_name, const std::vector<ViewContext>& views, size_t view_index) {
	static const ERenderPassMask opaque_mask = { ERenderPass::RP_Lighting };

	if (pass_name == "gbuffer") {
		return [this, &views, view_index]() { render_gbuffer(views[view_index].camera, opaque_mask); };
	}

	return nullptr;
}

/// Renderer_WebGpu::render_gbuffer
///
/// Mirrors Renderer_Dx12::render_gbuffer_internal: the same clears, the same
/// per-draw constants, the same draw order. Static and skinned models only -
/// particles and terrain have their own pipelines.
void Renderer_WebGpu::render_gbuffer(const RenderCamera& camera, const ERenderPassMask& render_pass_mask) {
	WGPURenderPassColorAttachment color_attachments[k_gbuffer_target_count] = {};
	for (u32 i = 0; i < k_gbuffer_target_count; i++) {
		color_attachments[i].view = m_gbuffer_view[i];
		color_attachments[i].depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
		color_attachments[i].loadOp = WGPULoadOp_Clear;
		color_attachments[i].storeOp = WGPUStoreOp_Store;
		color_attachments[i].clearValue = { 0.0, 0.0, 0.0, 0.0 };
	}
	// Same clears as D3D12: a flat normal, and -1 meaning "no entity here".
	color_attachments[Slot_Normal].clearValue = { 0.5, 0.5, 0.5, 0.0 };
	color_attachments[Slot_EntityId].clearValue = { -1.0, 0.0, 0.0, 0.0 };

	WGPURenderPassDepthStencilAttachment depth_attachment = {};
	depth_attachment.view = m_depth_view;
	depth_attachment.depthLoadOp = WGPULoadOp_Clear;
	depth_attachment.depthStoreOp = WGPUStoreOp_Store;
	depth_attachment.depthClearValue = 1.0f;

	WGPURenderPassDescriptor pass_descriptor = {};
	pass_descriptor.label = label("gbuffer");
	pass_descriptor.colorAttachmentCount = k_gbuffer_target_count;
	pass_descriptor.colorAttachments = color_attachments;
	pass_descriptor.depthStencilAttachment = &depth_attachment;

	WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(m_encoder, &pass_descriptor);

	FrameConstants frame = {};
	frame.view = camera.view_matrix;
	frame.view_projection = camera.view_projection_matrix;
	frame.inv_view_proj = camera.inv_view_projection_matrix;
	frame.camera = Vec4(camera.view_position, 1.f);
	wgpuQueueWriteBuffer(m_queue, m_frame_constants, 0, &frame, sizeof(frame));

	m_frame_draws = 0;
	m_frame_bone_draws = 0;

	for (auto& render_comp : render_components()) {
		if (m_frame_draws >= m_max_draws) {
			break;
		}

		if (!render_pass_in_mask(render_comp->render_pass(), render_pass_mask)) {
			continue;
		}

		// Which pipeline, which model, and - for a skinned draw - a slice of the
		// bone buffer. Everything after this point is common to both.
		WGPURenderPipeline pipeline = nullptr;
		const Model* model = nullptr;
		u32 bone_offset = 0;

		if (render_comp->IsA(StaticModelComponent::GetType())) {
			pipeline = m_material_pipelines["static_model"];
			model = static_cast<const StaticModelComponent*>(render_comp)->model();
		} else if (render_comp->IsA(SkeletalModelComponent::GetType())) {
			if (m_frame_bone_draws >= m_max_bone_draws) {
				continue;
			}

			const SkeletalModelComponent* const skeletal = static_cast<const SkeletalModelComponent*>(render_comp);
			pipeline = m_material_pipelines["skinned_model"];
			model = skeletal->model();

			// Same rows D3D12 writes, and untransposed for the same reason: the
			// shader's row-vector mul consumes the basis vectors directly.
			bone_offset = m_frame_bone_draws * m_bone_stride;
			BoneConstants* const bones = (BoneConstants*)(m_bone_staging.data() + bone_offset);
			const auto& bone_list = skeletal->GetFinalBoneMatrices();
			for (size_t i = 0; i < bone_list.size() && i < 128; i++) {
				bones->bones[i].make_identity();
				bones->bones[i][0] = bone_list[i].GetAxis(0);
				bones->bones[i][1] = bone_list[i].GetAxis(1);
				bones->bones[i][2] = bone_list[i].GetAxis(2);
				bones->bones[i][3] = bone_list[i].GetAxis(3);

				bones->bones[i][0].w = 0;
				bones->bones[i][1].w = 0;
				bones->bones[i][2].w = 0;
			}

			m_frame_bone_draws++;
		} else {
			continue;
		}

		if (pipeline == nullptr || model == nullptr) {
			continue;
		}

		const RenderBuffer_WebGpu* const vertex_buffer = (const RenderBuffer_WebGpu*)model->vertex_buffer();
		const RenderBuffer_WebGpu* const index_buffer = (const RenderBuffer_WebGpu*)model->index_buffer();
		if (vertex_buffer == nullptr || index_buffer == nullptr ||
			vertex_buffer->buffer() == nullptr || index_buffer->buffer() == nullptr) {
			continue;
		}

		Vec4 color(1.f, 1.f, 1.f, 1.f);
		Vec4 spec(0.f, 0.f, 0.f, 1.f);

		// D3D12 puts the texture's heap index in texture_list[0] and the shader
		// indexes the descriptor heap with it. Here the same id selects a bind
		// group instead, so the shader's `blk_texture_0` is already the right one
		// and texture_list is left alone.
		u32 color_texture_id = 0;
		if (render_comp->materials().size() > 0) {
			for (const auto& param : render_comp->materials()[0].shader_params()) {
				if (param.param_name() == String("color")) {
					color = param.vector();
				}
				if (param.param_name() == String("spec")) {
					spec = param.vector();
				}
				if (param.param_name() == String("color_tex") && param.texture() != nullptr) {
					color_texture_id = param.texture()->get_texture_id();
				}
			}
		}

		Mat4 world_mat;
		world_mat.make_scale(render_comp->scale());
		world_mat *= render_comp->rotation().to_mat4();
		world_mat[3] = render_comp->position();

		DrawConstants draw = {};
		draw.world = world_mat;
		draw.mvp = world_mat * camera.view_projection_matrix;
		draw.inv_world = world_mat;
		draw.inv_world.inverse_self();
		draw.color = color;
		draw.spec = spec;
		draw.entity_id = Vec4((f32)render_comp->GetOwner()->GetEntityId(), 0.f, 0.f, 0.f);

		const u32 draw_offset = m_frame_draws * m_draw_stride;
		memcpy(m_draw_staging.data() + draw_offset, &draw, sizeof(draw));

		// Dynamic offsets go in binding order: draw constants, then bones.
		const u32 dynamic_offsets[2] = { draw_offset, bone_offset };

		wgpuRenderPassEncoderSetPipeline(pass, pipeline);
		wgpuRenderPassEncoderSetBindGroup(pass, 0, m_frame_bind_group, 0, nullptr);
		wgpuRenderPassEncoderSetBindGroup(pass, 1, material_bind_group(color_texture_id), 0, nullptr);
		wgpuRenderPassEncoderSetBindGroup(pass, 2, m_draw_bind_group, 2, dynamic_offsets);
		wgpuRenderPassEncoderSetVertexBuffer(pass, 0, vertex_buffer->buffer(), 0, vertex_buffer->buffer_size());
		wgpuRenderPassEncoderSetIndexBuffer(pass, index_buffer->buffer(), WGPUIndexFormat_Uint16, 0, index_buffer->buffer_size());
		wgpuRenderPassEncoderDrawIndexed(pass, index_buffer->num_elements(), 1, 0, 0, 0);

		m_frame_draws++;
	}

	// One upload for the whole frame's constants. It has to happen before the
	// command buffer is submitted, not before the pass is recorded - queue
	// writes are ordered against submits, not against encoding.
	if (m_frame_draws > 0) {
		wgpuQueueWriteBuffer(m_queue, m_draw_constants, 0, m_draw_staging.data(), (size_t)m_frame_draws * m_draw_stride);
	}
	if (m_frame_bone_draws > 0) {
		wgpuQueueWriteBuffer(m_queue, m_bone_constants, 0, m_bone_staging.data(), (size_t)m_frame_bone_draws * m_bone_stride);
	}

	wgpuRenderPassEncoderEnd(pass);
	wgpuRenderPassEncoderRelease(pass);
}

/// Renderer_WebGpu::blit_to_surface
void Renderer_WebGpu::blit_to_surface() {
	WGPUSurfaceTexture surface_texture = {};
	wgpuSurfaceGetCurrentTexture(m_surface, &surface_texture);
	if (surface_texture.status != WGPUSurfaceGetCurrentTextureStatus_SuccessOptimal &&
		surface_texture.status != WGPUSurfaceGetCurrentTextureStatus_SuccessSuboptimal) {
		blk::warn("Renderer_WebGpu - getCurrentTexture status %d", (int)surface_texture.status);
		return;
	}

	WGPUTextureView view = wgpuTextureCreateView(surface_texture.texture, nullptr);

	WGPURenderPassColorAttachment color_attachment = {};
	color_attachment.view = view;
	color_attachment.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
	color_attachment.loadOp = WGPULoadOp_Clear;
	color_attachment.storeOp = WGPUStoreOp_Store;
	color_attachment.clearValue = { 0.0, 0.0, 0.0, 1.0 };

	WGPURenderPassDescriptor pass_descriptor = {};
	pass_descriptor.label = label("blit");
	pass_descriptor.colorAttachmentCount = 1;
	pass_descriptor.colorAttachments = &color_attachment;

	WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(m_encoder, &pass_descriptor);
	wgpuRenderPassEncoderSetPipeline(pass, m_blit_pipeline);
	wgpuRenderPassEncoderSetBindGroup(pass, 0, m_blit_bind_group, 0, nullptr);
	wgpuRenderPassEncoderDraw(pass, 3, 1, 0, 0);
	wgpuRenderPassEncoderEnd(pass);
	wgpuRenderPassEncoderRelease(pass);

	wgpuTextureViewRelease(view);
	wgpuTextureRelease(surface_texture.texture);
}

/// Renderer_WebGpu::present
void Renderer_WebGpu::present() {
	if (m_encoder == nullptr) {
		return;
	}

	blit_to_surface();

	WGPUCommandBuffer commands = wgpuCommandEncoderFinish(m_encoder, nullptr);
	wgpuQueueSubmit(m_queue, 1, &commands);
	wgpuCommandBufferRelease(commands);
	wgpuCommandEncoderRelease(m_encoder);
	m_encoder = nullptr;

	// The browser presents the canvas itself once the frame callback returns.
#if !defined(__EMSCRIPTEN__)
	wgpuSurfacePresent(m_surface);
#endif

	// Drives Dawn's callbacks (device errors among them), so a problem is
	// reported on the frame it happens rather than at shutdown.
	wgpuInstanceProcessEvents(m_instance);

	m_frame_index++;
	if ((m_frame_index % 600) == 0) {
		blk::log("Renderer_WebGpu - frame %u, %u draws", m_frame_index, m_frame_draws);
	}
}

/// Renderer_WebGpu::load_texture
///
/// Returns an index into m_textures; 0 is the white pixel, which is also what a
/// file that cannot be read comes back as, so a missing texture draws pale
/// rather than taking the frame down.
u32 Renderer_WebGpu::load_texture(const std::string& path, LoadTextureParams& params) {
	std::ifstream file(blk::os_path(path), std::ios::binary);
	if (file.fail()) {
		blk::warn("Renderer_WebGpu - could not open texture %s", path.c_str());
		return 0;
	}

	const std::vector<u8> bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

	DdsImage image;
	if (!parse_dds(bytes, image)) {
		blk::warn("Renderer_WebGpu - unsupported texture %s (only DDS: BC1/BC3 or 32-bit uncompressed)", path.c_str());
		return 0;
	}
	if (image.compressed() && !m_block_compression) {
		return 0;
	}

	params.width = image.width;
	params.height = image.height;

	// The CPU copy the terrain builds its mesh from. Only the uncompressed
	// layout is handed back; decoding BC on the CPU is a decoder this does not
	// have. Channel order follows the file, exactly as the D3D12 path does.
	if (params.cpu_accessible && params.texture_data != nullptr) {
		if (image.compressed()) {
			blk::warn("Renderer_WebGpu - %s is block compressed; no CPU copy available", path.c_str());
		} else {
			const size_t pixels = (size_t)image.width * image.height;
			params.texture_data->reserve(pixels);
			for (size_t i = 0; i < pixels && (i * 4 + 3) < image.pixels_size; i++) {
				const u8* const texel = image.pixels + i * 4;
				params.texture_data->push_back(Vec4(texel[0] / 255.f, texel[1] / 255.f, texel[2] / 255.f, texel[3] / 255.f));
			}
		}
	}

	// WebGPU will not create a compressed texture whose size is not a whole
	// number of 4x4 blocks, where D3D12 pads the last partial block for you.
	// Rounding up gives the file's blocks somewhere to live; the cost is that the
	// image is sampled across a slightly wider extent - a quarter of a percent on
	// the widest offender - and that the mip chain no longer matches, since
	// halving 4100 and halving 4097 part company immediately. So those textures
	// get their top mip only.
	u32 texture_width = image.width;
	u32 texture_height = image.height;
	u32 mip_count = image.mip_count;
	if (image.compressed() && ((image.width % 4) != 0 || (image.height % 4) != 0)) {
		texture_width = ((image.width + 3) / 4) * 4;
		texture_height = ((image.height + 3) / 4) * 4;
		mip_count = 1;
		blk::warn("Renderer_WebGpu - %s is %ux%u, not a whole number of 4x4 blocks; padded to %ux%u, top mip only",
			path.c_str(), image.width, image.height, texture_width, texture_height);
	}

	WGPUTextureDescriptor descriptor = {};
	descriptor.dimension = WGPUTextureDimension_2D;
	descriptor.size = { texture_width, texture_height, 1 };
	descriptor.format = image.format;
	descriptor.mipLevelCount = mip_count;
	descriptor.sampleCount = 1;
	descriptor.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;

	MaterialTexture loaded = {};
	loaded.texture = wgpuDeviceCreateTexture(m_device, &descriptor);

	size_t offset = 0;
	u32 width = image.width;
	u32 height = image.height;
	for (u32 mip = 0; mip < mip_count; mip++) {
		// Compressed mips are measured in 4x4 blocks, so a 2x2 mip is still one
		// whole block - and the copy has to be that whole block, not the 2x2 the
		// mip logically holds. Uncompressed ones are plain rows of pixels.
		const u32 blocks_wide = (width + 3) / 4;
		const u32 blocks_high = (height + 3) / 4;
		const u32 rows = image.compressed() ? blocks_high : height;
		const u32 bytes_per_row = image.compressed() ? (blocks_wide * image.block_bytes) : (width * image.pixel_bytes);
		const size_t mip_size = (size_t)bytes_per_row * rows;
		if (offset + mip_size > image.pixels_size) {
			break;
		}

		WGPUTexelCopyTextureInfo destination = {};
		destination.texture = loaded.texture;
		destination.mipLevel = mip;

		WGPUTexelCopyBufferLayout layout = {};
		layout.bytesPerRow = bytes_per_row;
		layout.rowsPerImage = rows;

		WGPUExtent3D extent = image.compressed() ? WGPUExtent3D{ blocks_wide * 4, blocks_high * 4, 1 } : WGPUExtent3D{ width, height, 1 };
		wgpuQueueWriteTexture(m_queue, &destination, image.pixels + offset, mip_size, &layout, &extent);

		offset += mip_size;
		width = (std::max)(width / 2, 1u);
		height = (std::max)(height / 2, 1u);
	}

	loaded.view = wgpuTextureCreateView(loaded.texture, nullptr);

	WGPUBindGroupEntry binding = {};
	binding.binding = k_texture_binding;
	binding.textureView = loaded.view;

	WGPUBindGroupDescriptor group = {};
	group.layout = m_material_layout;
	group.entryCount = 1;
	group.entries = &binding;
	loaded.bind_group = wgpuDeviceCreateBindGroup(m_device, &group);

	m_textures.push_back(loaded);
	return (u32)(m_textures.size() - 1);
}

/// Renderer_WebGpu::create_gpu_pipeline
RenderPipeline* Renderer_WebGpu::create_gpu_pipeline(const std::string& friendly_name, const std::string& path) {
	return new RenderPipeline_WebGpu();
}

/// Renderer_WebGpu::create_compute_pipeline
RenderPipeline* Renderer_WebGpu::create_compute_pipeline(const std::string& friendly_name, const std::string& path) {
	return new RenderPipeline_WebGpu();
}

/// Renderer_WebGpu::create_render_buffer_internal
RenderBuffer* Renderer_WebGpu::create_render_buffer_internal() {
	return new RenderBuffer_WebGpu(this);
}
