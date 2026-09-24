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
#include "plane3d.h"
#include "renderer_webgpu.h"

// The camera's vertical field of view, defined in renderer.cpp. The shadow
// cascades need it to size each cascade's ortho box.
extern const f32 g_fov;

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

	/// LightConstants
	///
	/// `LightData` in common_light.hlsli - one light's entry. Same 512 bytes as
	/// DrawConstants, and it binds through the same group-2 slot, so the lights
	/// share the per-draw buffer rather than needing one of their own.
	struct LightConstants {
		Vec4 position; // .w is the radius, for a point light
		Vec4 direction;
		Vec4 color;
		Mat4 light_matrices[4];
		Vec4 cascade_distances;
		Mat4 player_inv_view_proj;
		Vec4 player_camera_pos;
		Vec4 gbuffer_srv_base; // bindless only; the web path binds the gbuffer directly
		Vec4 pad[6];
	};
	static_assert(offsetof(LightConstants, player_inv_view_proj) == 320, "player_inv_view_proj follows the cascade distances");
	static_assert(sizeof(LightConstants) == 512, "LightConstants must match LightData's 512 bytes");

	/// SplatPoint
	///
	/// `SplatPoint` in gaussian_splat_draw.hlsl. That shader's own comment
	/// explains the odd size: `half f_rest[24]` compiles at this shader's SM6.0
	/// profile as `float f_rest[24]` (no true 16-bit type without
	/// -enable-16bit-types), so the struct is 64 + 96 = 160 bytes despite
	/// looking like 112 from the field list. D3D12's CPU-side struct
	/// (PointCloudSampleInstance) still packs f_rest as 24 tightly-packed
	/// halfs to save upload bandwidth, which the shader then reads at the
	/// wrong stride - a pre-existing bug documented there. This path sidesteps
	/// it by writing plain floats, matching what the shader actually reads.
	struct SplatPoint {
		Vec4 position;
		Vec4 scale3d_opacity;
		Vec4 rotation;
		Vec4 sh0;
		f32 f_rest[24];
	};
	static_assert(sizeof(SplatPoint) == 160, "SplatPoint must match gaussian_splat_draw.hlsl's compiled 160-byte stride");

	/// The fullscreen quad every light draws, in clip space - the light vertex
	/// shader passes position straight through. Same six vertices D3D12 builds.
	struct QuadVertex {
		f32 position[3];
		f32 uv[2];
	};
	constexpr QuadVertex k_quad_vertices[] = {
		{ { -1.f, 1.f, 0.f }, { 0.f, 0.f } },
		{ { 1.f, 1.f, 0.f }, { 1.f, 0.f } },
		{ { 1.f, -1.f, 0.f }, { 1.f, 1.f } },

		{ { -1.f, 1.f, 0.f }, { 0.f, 0.f } },
		{ { 1.f, -1.f, 0.f }, { 1.f, 1.f } },
		{ { -1.f, -1.f, 0.f }, { 0.f, 1.f } },
	};

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

	/// Puts SceneColor on the screen - the backend's post_process pass. D3D12
	/// gets there with a CopyResource; a WebGPU surface texture is a render
	/// target rather than a copy target, so this draws it instead.
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

	// Straight mapping, v opposing clip-space y. The shaders keep D3D12's clip
	// space (see hlsl_to_wgsl.py's NAGA_FLAGS), so the gbuffer and SceneColor are
	// both the right way up and a raw gbuffer slot blits with the same mapping.
	out.uv = vec2<f32>((x + 1.0) * 0.5, (1.0 - y) * 0.5);
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
	// On a hybrid-GPU machine the default can be the integrated adapter; ask for
	// the discrete one and fall back to whatever exists if there is none.
	options.powerPreference = WGPUPowerPreference_HighPerformance;

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
	//
	// It is the only optional feature asked for. In particular the lighting pass
	// does NOT need float32-filterable, which only ~69% of Android and ~53% of
	// iOS report - see create_light_resources().
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

	// RGBA8 like D3D12's, so the lights' additive blending clamps the same way.
	WGPUTextureDescriptor scene_color_descriptor = {};
	scene_color_descriptor.dimension = WGPUTextureDimension_2D;
	scene_color_descriptor.size = { frame_width(), frame_height(), 1 };
	scene_color_descriptor.format = WGPUTextureFormat_RGBA8Unorm;
	scene_color_descriptor.mipLevelCount = 1;
	scene_color_descriptor.sampleCount = 1;
	scene_color_descriptor.usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_TextureBinding;

	m_scene_color = wgpuDeviceCreateTexture(m_device, &scene_color_descriptor);
	m_scene_color_view = wgpuTextureCreateView(m_scene_color, nullptr);

	// The shadow mask the composite writes and the directional light samples.
	WGPUTextureDescriptor lighting = {};
	lighting.label = label("lighting");
	lighting.dimension = WGPUTextureDimension_2D;
	lighting.size = { frame_width(), frame_height(), 1 };
	lighting.format = WGPUTextureFormat_RGBA8Unorm;
	lighting.mipLevelCount = 1;
	lighting.sampleCount = 1;
	lighting.usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_TextureBinding;
	m_lighting = wgpuDeviceCreateTexture(m_device, &lighting);
	m_lighting_view = wgpuTextureCreateView(m_lighting, nullptr);

	// The cascade atlas, and the depth buffer that only exists to depth-test it.
	WGPUTextureDescriptor atlas = {};
	atlas.label = label("shadow atlas");
	atlas.dimension = WGPUTextureDimension_2D;
	atlas.size = { k_shadow_dimensions, k_shadow_dimensions, 1 };
	atlas.format = WGPUTextureFormat_R32Float;
	atlas.mipLevelCount = 1;
	atlas.sampleCount = 1;
	atlas.usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_TextureBinding;
	m_shadow_atlas = wgpuDeviceCreateTexture(m_device, &atlas);
	m_shadow_atlas_view = wgpuTextureCreateView(m_shadow_atlas, nullptr);

	WGPUTextureDescriptor shadow_depth = {};
	shadow_depth.label = label("shadow depth");
	shadow_depth.dimension = WGPUTextureDimension_2D;
	shadow_depth.size = { k_shadow_dimensions, k_shadow_dimensions, 1 };
	shadow_depth.format = WGPUTextureFormat_Depth24Plus;
	shadow_depth.mipLevelCount = 1;
	shadow_depth.sampleCount = 1;
	shadow_depth.usage = WGPUTextureUsage_RenderAttachment;
	m_shadow_depth = wgpuDeviceCreateTexture(m_device, &shadow_depth);
	m_shadow_depth_view = wgpuTextureCreateView(m_shadow_depth, nullptr);
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

/// Renderer_WebGpu::create_light_resources
///
/// The light shaders read the gbuffer as ordinary textures at group 1, bindings
/// 16..20 - where D3D12 indexes `ResourceDescriptorHeap[gbuffer_srv_base + n]`.
/// The five are Color, Normal, Specular, SceneDepth and Lighting; the last is
/// the shadow mask, and with no shadow pass yet it gets the 1x1 white pixel,
/// which samples as "fully lit" everywhere.
///
/// These bindings are `unfilterable-float` read through a point sampler, which
/// is why the pass needs its own group-0 layout rather than reusing the frame
/// one. The alternative - declaring them filterable - would mean requiring the
/// float32-filterable feature, because SceneDepth is R32Float and a filtering
/// sampler cannot touch that format without it. Only about 69% of Android and
/// 53% of iOS report the feature, and nothing is lost by avoiding it: the quad
/// covers the viewport at the gbuffer's own resolution, so every fragment lands
/// on a texel centre and linear filtering would return the same texel anyway.
void Renderer_WebGpu::create_light_resources() {
	WGPUBufferDescriptor quad_buffer = {};
	quad_buffer.label = label("fullscreen quad");
	quad_buffer.size = sizeof(k_quad_vertices);
	quad_buffer.usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst;
	m_quad_vertices = wgpuDeviceCreateBuffer(m_device, &quad_buffer);
	wgpuQueueWriteBuffer(m_queue, m_quad_vertices, 0, k_quad_vertices, sizeof(k_quad_vertices));

	WGPUSamplerDescriptor point_sampler = {};
	point_sampler.label = label("point");
	point_sampler.addressModeU = WGPUAddressMode_ClampToEdge;
	point_sampler.addressModeV = WGPUAddressMode_ClampToEdge;
	point_sampler.addressModeW = WGPUAddressMode_ClampToEdge;
	point_sampler.magFilter = WGPUFilterMode_Nearest;
	point_sampler.minFilter = WGPUFilterMode_Nearest;
	point_sampler.mipmapFilter = WGPUMipmapFilterMode_Nearest;
	point_sampler.maxAnisotropy = 1;
	m_point_sampler = wgpuDeviceCreateSampler(m_device, &point_sampler);

	// Same shape as the frame layout, but the sampler is declared non-filtering
	// so it can be paired with the unfilterable gbuffer bindings below. The
	// light shaders never read binding 0; it is here to keep the two layouts
	// interchangeable from the shader's point of view.
	WGPUBindGroupLayoutEntry light_frame_entries[2] = {};
	light_frame_entries[0].binding = k_frame_binding;
	light_frame_entries[0].visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
	light_frame_entries[0].buffer.type = WGPUBufferBindingType_Uniform;
	light_frame_entries[0].buffer.minBindingSize = sizeof(FrameConstants);
	light_frame_entries[1].binding = k_sampler_binding;
	light_frame_entries[1].visibility = WGPUShaderStage_Fragment;
	light_frame_entries[1].sampler.type = WGPUSamplerBindingType_NonFiltering;

	WGPUBindGroupLayoutDescriptor light_frame_layout = {};
	light_frame_layout.label = label("light frame");
	light_frame_layout.entryCount = 2;
	light_frame_layout.entries = light_frame_entries;
	m_light_frame_layout = wgpuDeviceCreateBindGroupLayout(m_device, &light_frame_layout);

	WGPUBindGroupEntry light_frame_bindings[2] = {};
	light_frame_bindings[0].binding = k_frame_binding;
	light_frame_bindings[0].buffer = m_frame_constants;
	light_frame_bindings[0].size = sizeof(FrameConstants);
	light_frame_bindings[1].binding = k_sampler_binding;
	light_frame_bindings[1].sampler = m_point_sampler;

	WGPUBindGroupDescriptor light_frame_group = {};
	light_frame_group.label = label("light frame");
	light_frame_group.layout = m_light_frame_layout;
	light_frame_group.entryCount = 2;
	light_frame_group.entries = light_frame_bindings;
	m_light_frame_bind_group = wgpuDeviceCreateBindGroup(m_device, &light_frame_group);

	constexpr u32 k_light_texture_count = 5;
	WGPUBindGroupLayoutEntry texture_entries[k_light_texture_count] = {};
	for (u32 i = 0; i < k_light_texture_count; i++) {
		texture_entries[i].binding = k_texture_binding + i;
		texture_entries[i].visibility = WGPUShaderStage_Fragment;
		texture_entries[i].texture.sampleType = WGPUTextureSampleType_UnfilterableFloat;
		texture_entries[i].texture.viewDimension = WGPUTextureViewDimension_2D;
	}

	WGPUBindGroupLayoutDescriptor texture_layout = {};
	texture_layout.label = label("light gbuffer");
	texture_layout.entryCount = k_light_texture_count;
	texture_layout.entries = texture_entries;
	m_light_texture_layout = wgpuDeviceCreateBindGroupLayout(m_device, &texture_layout);

	// Group 2 is the same layout the material pipelines use - the light shaders
	// take their LightData from its dynamic slot and never touch the bone
	// binding beside it.
	WGPUBindGroupLayout groups[] = { m_light_frame_layout, m_light_texture_layout, m_draw_layout };
	WGPUPipelineLayoutDescriptor pipeline_layout = {};
	pipeline_layout.label = label("light");
	pipeline_layout.bindGroupLayoutCount = 3;
	pipeline_layout.bindGroupLayouts = groups;
	m_light_pipeline_layout = wgpuDeviceCreatePipelineLayout(m_device, &pipeline_layout);

	// Slots 0..3 are the gbuffer proper; slot 4 is the shadow mask. That is the
	// Lighting target once the shadow passes are trusted - see
	// render_shadow_cascades - and until then the 1x1 white pixel, which samples
	// as "fully lit" everywhere.
	WGPUBindGroupEntry bindings[k_light_texture_count] = {};
	for (u32 i = 0; i < k_light_texture_count; i++) {
		bindings[i].binding = k_texture_binding + i;
		bindings[i].textureView = (i < k_gbuffer_target_count - 1) ? m_gbuffer_view[i] : m_lighting_view;
	}

	WGPUBindGroupDescriptor group = {};
	group.label = label("light gbuffer");
	group.layout = m_light_texture_layout;
	group.entryCount = k_light_texture_count;
	group.entries = bindings;
	m_light_bind_group = wgpuDeviceCreateBindGroup(m_device, &group);

	m_directional_light_pipeline = create_light_pipeline("directional_light");
	m_point_light_pipeline = create_light_pipeline("point_light");
}

/// Renderer_WebGpu::create_light_pipeline
///
/// Additive with depth off, matching D3D12's `blend_type == 1` path: the lights
/// accumulate on top of one another into SceneColor.
WGPURenderPipeline Renderer_WebGpu::create_light_pipeline(const std::string& shader_name) {
	WGPUShaderModule vertex_module = load_wgsl(shader_name + ".vertex_shader.wgsl");
	WGPUShaderModule fragment_module = load_wgsl(shader_name + ".pixel_shader.wgsl");
	if (vertex_module == nullptr || fragment_module == nullptr) {
		return nullptr;
	}

	WGPUVertexAttribute attributes[2] = {};
	attributes[0].format = WGPUVertexFormat_Float32x3;
	attributes[0].offset = offsetof(QuadVertex, position);
	attributes[0].shaderLocation = 0;
	attributes[1].format = WGPUVertexFormat_Float32x2;
	attributes[1].offset = offsetof(QuadVertex, uv);
	attributes[1].shaderLocation = 1;

	WGPUVertexBufferLayout vertex_buffer = {};
	vertex_buffer.arrayStride = sizeof(QuadVertex);
	vertex_buffer.stepMode = WGPUVertexStepMode_Vertex;
	vertex_buffer.attributeCount = 2;
	vertex_buffer.attributes = attributes;

	WGPUBlendState blend = {};
	blend.color.operation = WGPUBlendOperation_Add;
	blend.color.srcFactor = WGPUBlendFactor_One;
	blend.color.dstFactor = WGPUBlendFactor_One;
	blend.alpha.operation = WGPUBlendOperation_Add;
	blend.alpha.srcFactor = WGPUBlendFactor_One;
	blend.alpha.dstFactor = WGPUBlendFactor_One;

	WGPUColorTargetState target = {};
	target.format = WGPUTextureFormat_RGBA8Unorm;
	target.blend = &blend;
	target.writeMask = WGPUColorWriteMask_All;

	WGPUFragmentState fragment = {};
	fragment.module = fragment_module;
	fragment.entryPoint = label("pixel_shader");
	fragment.targetCount = 1;
	fragment.targets = &target;

	WGPURenderPipelineDescriptor descriptor = {};
	descriptor.label = label(shader_name.c_str());
	descriptor.layout = m_light_pipeline_layout;
	descriptor.vertex.module = vertex_module;
	descriptor.vertex.entryPoint = label("vertex_shader");
	descriptor.vertex.bufferCount = 1;
	descriptor.vertex.buffers = &vertex_buffer;
	descriptor.primitive.topology = WGPUPrimitiveTopology_TriangleList;
	descriptor.primitive.cullMode = WGPUCullMode_None;
	descriptor.multisample.count = 1;
	descriptor.multisample.mask = 0xFFFFFFFF;
	descriptor.fragment = &fragment;

	return wgpuDeviceCreateRenderPipeline(m_device, &descriptor);
}

/// Renderer_WebGpu::create_terrain_resources
///
/// Terrain reads three textures - the splat map plus the two layers it blends -
/// so group 1 is wider than a material's, which is the whole reason it needs its
/// own layout and pipeline. Everything else matches the material pipelines: the
/// same five gbuffer targets and the same depth state.
void Renderer_WebGpu::create_terrain_resources() {
	constexpr u32 k_terrain_texture_count = 3;
	WGPUBindGroupLayoutEntry texture_entries[k_terrain_texture_count] = {};
	for (u32 i = 0; i < k_terrain_texture_count; i++) {
		texture_entries[i].binding = k_texture_binding + i;
		texture_entries[i].visibility = WGPUShaderStage_Fragment;
		texture_entries[i].texture.sampleType = WGPUTextureSampleType_Float;
		texture_entries[i].texture.viewDimension = WGPUTextureViewDimension_2D;
	}

	WGPUBindGroupLayoutDescriptor texture_layout = {};
	texture_layout.label = label("terrain");
	texture_layout.entryCount = k_terrain_texture_count;
	texture_layout.entries = texture_entries;
	m_terrain_texture_layout = wgpuDeviceCreateBindGroupLayout(m_device, &texture_layout);

	WGPUBindGroupLayout groups[] = { m_frame_layout, m_terrain_texture_layout, m_draw_layout };
	WGPUPipelineLayoutDescriptor pipeline_layout = {};
	pipeline_layout.label = label("terrain");
	pipeline_layout.bindGroupLayoutCount = 3;
	pipeline_layout.bindGroupLayouts = groups;
	m_terrain_pipeline_layout = wgpuDeviceCreatePipelineLayout(m_device, &pipeline_layout);

	WGPUShaderModule vertex_module = load_wgsl("terrain.vertex_shader.wgsl");
	WGPUShaderModule fragment_module = load_wgsl("terrain.pixel_shader.wgsl");
	if (vertex_module == nullptr || fragment_module == nullptr) {
		return;
	}

	// Locations again follow the HLSL's declaration order, and terrain declares
	// COLOR before NORMAL - so the normal is location 3 here, where the skinned
	// shader puts it at 2.
	WGPUVertexAttribute attributes[3] = {};
	attributes[0].format = WGPUVertexFormat_Float32x3;
	attributes[0].offset = offsetof(vertexLayout, position);
	attributes[0].shaderLocation = 0;
	attributes[1].format = WGPUVertexFormat_Float32x2;
	attributes[1].offset = offsetof(vertexLayout, uv);
	attributes[1].shaderLocation = 1;
	attributes[2].format = WGPUVertexFormat_Unorm8x4;
	attributes[2].offset = offsetof(vertexLayout, normal);
	attributes[2].shaderLocation = 3;

	WGPUVertexBufferLayout vertex_buffer = {};
	vertex_buffer.arrayStride = sizeof(vertexLayout);
	vertex_buffer.stepMode = WGPUVertexStepMode_Vertex;
	vertex_buffer.attributeCount = 3;
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
	descriptor.label = label("terrain");
	descriptor.layout = m_terrain_pipeline_layout;
	descriptor.vertex.module = vertex_module;
	descriptor.vertex.entryPoint = label("vertex_shader");
	descriptor.vertex.bufferCount = 1;
	descriptor.vertex.buffers = &vertex_buffer;
	descriptor.primitive.topology = WGPUPrimitiveTopology_TriangleList;
	descriptor.primitive.cullMode = WGPUCullMode_None;
	descriptor.multisample.count = 1;
	descriptor.multisample.mask = 0xFFFFFFFF;
	descriptor.depthStencil = &depth;
	descriptor.fragment = &fragment;

	m_terrain_pipeline = wgpuDeviceCreateRenderPipeline(m_device, &descriptor);
}

/// Renderer_WebGpu::terrain_bind_group
///
/// Slot 0 is the splat map, slots 1 and 2 the layers it blends between - the
/// same three D3D12 puts in texture_list[4], [0] and [1].
WGPUBindGroup Renderer_WebGpu::terrain_bind_group(const TerrainComponent* const terrain) {
	u32 texture_ids[3] = { 0, 0, 0 };
	if (terrain->splat_map() != nullptr) {
		texture_ids[0] = terrain->splat_map()->get_texture_id();
	}
	if (terrain->materials().size() > 0) {
		for (const auto& param : terrain->materials()[0].shader_params()) {
			if (param.texture() == nullptr) {
				continue;
			}
			if (param.param_name() == String("color_tex")) {
				texture_ids[1] = param.texture()->get_texture_id();
			}
			if (param.param_name() == String("color_tex_2")) {
				texture_ids[2] = param.texture()->get_texture_id();
			}
		}
	}

	const u64 key = ((u64)texture_ids[0] << 40) | ((u64)texture_ids[1] << 20) | (u64)texture_ids[2];
	const auto existing = m_terrain_bind_groups.find(key);
	if (existing != m_terrain_bind_groups.end()) {
		return existing->second;
	}

	WGPUBindGroupEntry bindings[3] = {};
	for (u32 i = 0; i < 3; i++) {
		const u32 id = (texture_ids[i] < m_textures.size()) ? texture_ids[i] : 0;
		bindings[i].binding = k_texture_binding + i;
		bindings[i].textureView = m_textures[id].view;
	}

	WGPUBindGroupDescriptor group = {};
	group.label = label("terrain");
	group.layout = m_terrain_texture_layout;
	group.entryCount = 3;
	group.entries = bindings;

	WGPUBindGroup bind_group = wgpuDeviceCreateBindGroup(m_device, &group);
	m_terrain_bind_groups[key] = bind_group;
	return bind_group;
}

/// Renderer_WebGpu::create_particle_resources
void Renderer_WebGpu::create_particle_resources() {
	m_particle_additive_pipeline = create_particle_pipeline(true);
	m_particle_alpha_pipeline = create_particle_pipeline(false);
}

/// Renderer_WebGpu::create_particle_pipeline
///
/// Draws into SceneColor after the lights, so it tests depth against the
/// gbuffer pass's buffer but never writes it - D3D12 does the same by zeroing
/// DepthWriteMask for every blended pipeline.
///
/// The vertices are `ParticleVertex`, not `vertexLayout`: same 32 bytes, but the
/// last eight are a rotation and a scale where a mesh keeps its normal and
/// tangent. The particle shader reads them as NORMAL and TANGENT anyway, which
/// is why the semantics look wrong and the layout is right.
WGPURenderPipeline Renderer_WebGpu::create_particle_pipeline(const bool additive) {
	WGPUShaderModule vertex_module = load_wgsl("sprite_particle.vertex_shader.wgsl");
	WGPUShaderModule fragment_module = load_wgsl("sprite_particle.pixel_shader.wgsl");
	if (vertex_module == nullptr || fragment_module == nullptr) {
		return nullptr;
	}

	WGPUVertexAttribute attributes[5] = {};
	attributes[0].format = WGPUVertexFormat_Float32x3;
	attributes[0].offset = 0;
	attributes[0].shaderLocation = 0;
	attributes[1].format = WGPUVertexFormat_Float32x2;
	attributes[1].offset = 12;
	attributes[1].shaderLocation = 1;
	attributes[2].format = WGPUVertexFormat_Unorm8x4;
	attributes[2].offset = 20;
	attributes[2].shaderLocation = 2;
	attributes[3].format = WGPUVertexFormat_Float32;
	attributes[3].offset = 24;
	attributes[3].shaderLocation = 3;
	attributes[4].format = WGPUVertexFormat_Float32;
	attributes[4].offset = 28;
	attributes[4].shaderLocation = 4;

	WGPUVertexBufferLayout vertex_buffer = {};
	vertex_buffer.arrayStride = sizeof(ParticleVertex);
	vertex_buffer.stepMode = WGPUVertexStepMode_Vertex;
	vertex_buffer.attributeCount = 5;
	vertex_buffer.attributes = attributes;

	WGPUBlendState blend = {};
	blend.color.operation = WGPUBlendOperation_Add;
	blend.color.srcFactor = additive ? WGPUBlendFactor_One : WGPUBlendFactor_SrcAlpha;
	blend.color.dstFactor = additive ? WGPUBlendFactor_One : WGPUBlendFactor_OneMinusSrcAlpha;
	blend.alpha.operation = WGPUBlendOperation_Add;
	blend.alpha.srcFactor = WGPUBlendFactor_One;
	blend.alpha.dstFactor = additive ? WGPUBlendFactor_One : WGPUBlendFactor_OneMinusSrcAlpha;

	WGPUColorTargetState target = {};
	target.format = WGPUTextureFormat_RGBA8Unorm;
	target.blend = &blend;
	target.writeMask = WGPUColorWriteMask_All;

	WGPUFragmentState fragment = {};
	fragment.module = fragment_module;
	fragment.entryPoint = label("pixel_shader");
	fragment.targetCount = 1;
	fragment.targets = &target;

	WGPUDepthStencilState depth = {};
	depth.format = WGPUTextureFormat_Depth24Plus;
	depth.depthWriteEnabled = WGPUOptionalBool_False;
	depth.depthCompare = WGPUCompareFunction_Less;
	depth.stencilFront.compare = WGPUCompareFunction_Always;
	depth.stencilBack.compare = WGPUCompareFunction_Always;

	WGPURenderPipelineDescriptor descriptor = {};
	descriptor.label = label(additive ? "sprite_particle_add" : "sprite_particle_blend");
	// The same three groups a material uses; the particle shaders simply never
	// read the per-draw one, because the vertices arrive in world space.
	descriptor.layout = m_material_pipeline_layout;
	descriptor.vertex.module = vertex_module;
	descriptor.vertex.entryPoint = label("vertex_shader");
	descriptor.vertex.bufferCount = 1;
	descriptor.vertex.buffers = &vertex_buffer;
	descriptor.primitive.topology = WGPUPrimitiveTopology_TriangleList;
	descriptor.primitive.cullMode = WGPUCullMode_None;
	descriptor.multisample.count = 1;
	descriptor.multisample.mask = 0xFFFFFFFF;
	descriptor.depthStencil = &depth;
	descriptor.fragment = &fragment;

	return wgpuDeviceCreateRenderPipeline(m_device, &descriptor);
}

/// Renderer_WebGpu::create_shadow_depth_pipeline
///
/// The material vertex shader again - it transforms by whatever `mvp` the draw
/// constants hold, so the cascade pass just writes the light's view-projection
/// there instead of the camera's - paired with `shadow_depth_ps`, which writes
/// the depth out as a colour so the composite can sample it.
WGPURenderPipeline Renderer_WebGpu::create_shadow_depth_pipeline(const std::string& shader_name, const bool skinned) {
	WGPUShaderModule vertex_module = load_wgsl(shader_name + ".vertex_shader.wgsl");
	WGPUShaderModule fragment_module = load_wgsl(shader_name + ".shadow_depth_ps.wgsl");
	if (vertex_module == nullptr || fragment_module == nullptr) {
		return nullptr;
	}

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

	WGPUColorTargetState target = {};
	target.format = WGPUTextureFormat_R32Float;
	target.writeMask = WGPUColorWriteMask_All;

	WGPUFragmentState fragment = {};
	fragment.module = fragment_module;
	fragment.entryPoint = label("shadow_depth_ps");
	fragment.targetCount = 1;
	fragment.targets = &target;

	WGPUDepthStencilState depth = {};
	depth.format = WGPUTextureFormat_Depth24Plus;
	depth.depthWriteEnabled = WGPUOptionalBool_True;
	depth.depthCompare = WGPUCompareFunction_Less;
	depth.stencilFront.compare = WGPUCompareFunction_Always;
	depth.stencilBack.compare = WGPUCompareFunction_Always;

	WGPURenderPipelineDescriptor descriptor = {};
	descriptor.label = label((shader_name + " shadow").c_str());
	descriptor.layout = m_material_pipeline_layout;
	descriptor.vertex.module = vertex_module;
	descriptor.vertex.entryPoint = label("vertex_shader");
	descriptor.vertex.bufferCount = 1;
	descriptor.vertex.buffers = &vertex_buffer;
	descriptor.primitive.topology = WGPUPrimitiveTopology_TriangleList;
	// The one pass D3D12 culls in, and it culls what ITS default winding calls
	// the back face - which is clockwise-is-front. WebGPU defaults the other way
	// round, so both fields have to be set or the cull takes the opposite
	// triangles and the shadow map stores the far surface instead of the near.
	descriptor.primitive.frontFace = WGPUFrontFace_CW;
	descriptor.primitive.cullMode = WGPUCullMode_Back;
	descriptor.multisample.count = 1;
	descriptor.multisample.mask = 0xFFFFFFFF;
	descriptor.depthStencil = &depth;
	descriptor.fragment = &fragment;

	return wgpuDeviceCreateRenderPipeline(m_device, &descriptor);
}

/// Renderer_WebGpu::create_shadow_resources
void Renderer_WebGpu::create_shadow_resources() {
	m_shadow_static_pipeline = create_shadow_depth_pipeline("static_model", false);
	m_shadow_skinned_pipeline = create_shadow_depth_pipeline("skinned_model", true);

	// The composite reads SceneDepth and the atlas - both R32Float, so both
	// unfilterable, and it gets the point sampler for the same reason the lights
	// do.
	WGPUBindGroupLayoutEntry texture_entries[2] = {};
	for (u32 i = 0; i < 2; i++) {
		texture_entries[i].binding = k_texture_binding + i;
		texture_entries[i].visibility = WGPUShaderStage_Fragment;
		texture_entries[i].texture.sampleType = WGPUTextureSampleType_UnfilterableFloat;
		texture_entries[i].texture.viewDimension = WGPUTextureViewDimension_2D;
	}

	WGPUBindGroupLayoutDescriptor texture_layout = {};
	texture_layout.label = label("shadow composite");
	texture_layout.entryCount = 2;
	texture_layout.entries = texture_entries;
	m_shadow_composite_layout = wgpuDeviceCreateBindGroupLayout(m_device, &texture_layout);

	WGPUBindGroupLayout groups[] = { m_light_frame_layout, m_shadow_composite_layout, m_draw_layout };
	WGPUPipelineLayoutDescriptor pipeline_layout = {};
	pipeline_layout.label = label("shadow composite");
	pipeline_layout.bindGroupLayoutCount = 3;
	pipeline_layout.bindGroupLayouts = groups;
	m_shadow_composite_pipeline_layout = wgpuDeviceCreatePipelineLayout(m_device, &pipeline_layout);

	WGPUBindGroupEntry bindings[2] = {};
	bindings[0].binding = k_texture_binding;
	bindings[0].textureView = m_gbuffer_view[Slot_SceneDepth];
	bindings[1].binding = k_texture_binding + 1;
	bindings[1].textureView = m_shadow_atlas_view;

	WGPUBindGroupDescriptor group = {};
	group.label = label("shadow composite");
	group.layout = m_shadow_composite_layout;
	group.entryCount = 2;
	group.entries = bindings;
	m_shadow_composite_bind_group = wgpuDeviceCreateBindGroup(m_device, &group);

	WGPUShaderModule vertex_module = load_wgsl("directional_shadow.vertex_shader.wgsl");
	WGPUShaderModule fragment_module = load_wgsl("directional_shadow.pixel_shader.wgsl");
	if (vertex_module == nullptr || fragment_module == nullptr) {
		return;
	}

	WGPUVertexAttribute quad_attributes[2] = {};
	quad_attributes[0].format = WGPUVertexFormat_Float32x3;
	quad_attributes[0].offset = offsetof(QuadVertex, position);
	quad_attributes[0].shaderLocation = 0;
	quad_attributes[1].format = WGPUVertexFormat_Float32x2;
	quad_attributes[1].offset = offsetof(QuadVertex, uv);
	quad_attributes[1].shaderLocation = 1;

	WGPUVertexBufferLayout quad_buffer = {};
	quad_buffer.arrayStride = sizeof(QuadVertex);
	quad_buffer.stepMode = WGPUVertexStepMode_Vertex;
	quad_buffer.attributeCount = 2;
	quad_buffer.attributes = quad_attributes;

	WGPUColorTargetState target = {};
	target.format = WGPUTextureFormat_RGBA8Unorm;
	target.writeMask = WGPUColorWriteMask_All;

	WGPUFragmentState fragment = {};
	fragment.module = fragment_module;
	fragment.entryPoint = label("pixel_shader");
	fragment.targetCount = 1;
	fragment.targets = &target;

	WGPURenderPipelineDescriptor descriptor = {};
	descriptor.label = label("directional_shadow");
	descriptor.layout = m_shadow_composite_pipeline_layout;
	descriptor.vertex.module = vertex_module;
	descriptor.vertex.entryPoint = label("vertex_shader");
	descriptor.vertex.bufferCount = 1;
	descriptor.vertex.buffers = &quad_buffer;
	descriptor.primitive.topology = WGPUPrimitiveTopology_TriangleList;
	descriptor.primitive.cullMode = WGPUCullMode_None;
	descriptor.multisample.count = 1;
	descriptor.multisample.mask = 0xFFFFFFFF;
	descriptor.fragment = &fragment;

	m_shadow_composite_pipeline = wgpuDeviceCreateRenderPipeline(m_device, &descriptor);
}

/// Renderer_WebGpu::create_splat_pipeline
///
/// Everything gaussian_splat_draw.hlsl binds sits in group 0 - the uniform at
/// binding 0 is the same FrameConstants buffer every other pipeline reads (see
/// render_gbuffer()'s comment), and the splat/index storage buffers sit beside
/// it at 16/17, matching k_texture_binding's numbering. No vertex buffer: the
/// shader builds each billboard from @builtin(vertex_index) alone.
void Renderer_WebGpu::create_splat_pipeline() {
	WGPUBindGroupLayoutEntry entries[3] = {};
	entries[0].binding = k_frame_binding;
	entries[0].visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
	entries[0].buffer.type = WGPUBufferBindingType_Uniform;
	entries[0].buffer.minBindingSize = sizeof(FrameConstants);
	entries[1].binding = k_texture_binding;
	entries[1].visibility = WGPUShaderStage_Vertex;
	entries[1].buffer.type = WGPUBufferBindingType_ReadOnlyStorage;
	entries[2].binding = k_texture_binding + 1;
	entries[2].visibility = WGPUShaderStage_Vertex;
	entries[2].buffer.type = WGPUBufferBindingType_ReadOnlyStorage;

	WGPUBindGroupLayoutDescriptor layout_descriptor = {};
	layout_descriptor.label = label("splat draw");
	layout_descriptor.entryCount = 3;
	layout_descriptor.entries = entries;
	m_splat_draw_layout = wgpuDeviceCreateBindGroupLayout(m_device, &layout_descriptor);

	WGPUPipelineLayoutDescriptor pipeline_layout = {};
	pipeline_layout.label = label("splat draw");
	pipeline_layout.bindGroupLayoutCount = 1;
	pipeline_layout.bindGroupLayouts = &m_splat_draw_layout;
	WGPUPipelineLayout splat_pipeline_layout = wgpuDeviceCreatePipelineLayout(m_device, &pipeline_layout);

	WGPUShaderModule vertex_module = load_wgsl("gaussian_splat_draw.vertex_shader.wgsl");
	WGPUShaderModule fragment_module = load_wgsl("gaussian_splat_draw.pixel_shader.wgsl");
	if (vertex_module == nullptr || fragment_module == nullptr) {
		wgpuPipelineLayoutRelease(splat_pipeline_layout);
		return;
	}

	// D3D12's blend for gs_draw: colour is premultiplied (src ONE, dst
	// INV_SRC_ALPHA - the shader already multiplies rgb by alpha), alpha is the
	// ordinary SRC_ALPHA/INV_SRC_ALPHA over.
	WGPUBlendState blend = {};
	blend.color.operation = WGPUBlendOperation_Add;
	blend.color.srcFactor = WGPUBlendFactor_One;
	blend.color.dstFactor = WGPUBlendFactor_OneMinusSrcAlpha;
	blend.alpha.operation = WGPUBlendOperation_Add;
	blend.alpha.srcFactor = WGPUBlendFactor_SrcAlpha;
	blend.alpha.dstFactor = WGPUBlendFactor_OneMinusSrcAlpha;

	WGPUColorTargetState target = {};
	target.format = WGPUTextureFormat_RGBA8Unorm;
	target.blend = &blend;
	target.writeMask = WGPUColorWriteMask_All;

	WGPUFragmentState fragment = {};
	fragment.module = fragment_module;
	fragment.entryPoint = label("pixel_shader");
	fragment.targetCount = 1;
	fragment.targets = &target;

	// Depth-tested against the gbuffer's depth (splats sit behind opaque
	// geometry correctly) but never written, same as D3D12's DepthWriteMask
	// ZERO - a splat's own draw order isn't sorted yet, so letting splats
	// occlude each other via the depth buffer would be meaningless.
	WGPUDepthStencilState depth = {};
	depth.format = WGPUTextureFormat_Depth24Plus;
	depth.depthWriteEnabled = WGPUOptionalBool_False;
	depth.depthCompare = WGPUCompareFunction_Less;
	depth.stencilFront.compare = WGPUCompareFunction_Always;
	depth.stencilBack.compare = WGPUCompareFunction_Always;

	WGPURenderPipelineDescriptor descriptor = {};
	descriptor.label = label("gaussian_splat_draw");
	descriptor.layout = splat_pipeline_layout;
	descriptor.vertex.module = vertex_module;
	descriptor.vertex.entryPoint = label("vertex_shader");
	descriptor.primitive.topology = WGPUPrimitiveTopology_TriangleList;
	descriptor.primitive.cullMode = WGPUCullMode_None;
	descriptor.multisample.count = 1;
	descriptor.multisample.mask = 0xFFFFFFFF;
	descriptor.depthStencil = &depth;
	descriptor.fragment = &fragment;

	m_splat_draw_pipeline = wgpuDeviceCreateRenderPipeline(m_device, &descriptor);
	wgpuPipelineLayoutRelease(splat_pipeline_layout);
}

/// Renderer_WebGpu::create_splat_sort_pipeline
///
/// One pipeline layout shared by all six kernels: group 0 is the sort's eight
/// resources (see gaussian_splat_radix.hlsl's register list), group 1 is the
/// one-uniform PassInfo bound with a dynamic offset so the four digit passes
/// reuse one small buffer instead of four bind groups. A shader that only
/// touches some of group 0 - cs_compute_keys never reads g_keys_in, say - is
/// fine binding the full layout anyway; WebGPU only requires what a module
/// actually uses to be a subset of what the layout declares.
void Renderer_WebGpu::create_splat_sort_pipeline() {
	WGPUBindGroupLayoutEntry group0_entries[8] = {};
	group0_entries[0].binding = 0;
	group0_entries[0].visibility = WGPUShaderStage_Compute;
	group0_entries[0].buffer.type = WGPUBufferBindingType_Uniform;
	group0_entries[1].binding = k_texture_binding;  // g_splats
	group0_entries[1].visibility = WGPUShaderStage_Compute;
	group0_entries[1].buffer.type = WGPUBufferBindingType_ReadOnlyStorage;
	group0_entries[2].binding = k_texture_binding + 1;  // g_keys_in
	group0_entries[2].visibility = WGPUShaderStage_Compute;
	group0_entries[2].buffer.type = WGPUBufferBindingType_ReadOnlyStorage;
	group0_entries[3].binding = k_texture_binding + 2;  // g_vals_in
	group0_entries[3].visibility = WGPUShaderStage_Compute;
	group0_entries[3].buffer.type = WGPUBufferBindingType_ReadOnlyStorage;
	group0_entries[4].binding = 48;  // g_keys_out
	group0_entries[4].visibility = WGPUShaderStage_Compute;
	group0_entries[4].buffer.type = WGPUBufferBindingType_Storage;
	group0_entries[5].binding = 49;  // g_vals_out
	group0_entries[5].visibility = WGPUShaderStage_Compute;
	group0_entries[5].buffer.type = WGPUBufferBindingType_Storage;
	group0_entries[6].binding = 50;  // g_hist
	group0_entries[6].visibility = WGPUShaderStage_Compute;
	group0_entries[6].buffer.type = WGPUBufferBindingType_Storage;
	group0_entries[7].binding = 51;  // g_block_sums
	group0_entries[7].visibility = WGPUShaderStage_Compute;
	group0_entries[7].buffer.type = WGPUBufferBindingType_Storage;

	WGPUBindGroupLayoutDescriptor group0_descriptor = {};
	group0_descriptor.label = label("splat sort group0");
	group0_descriptor.entryCount = 8;
	group0_descriptor.entries = group0_entries;
	m_splat_sort_group0_layout = wgpuDeviceCreateBindGroupLayout(m_device, &group0_descriptor);

	WGPUBindGroupLayoutEntry group1_entry = {};
	group1_entry.binding = 0;
	group1_entry.visibility = WGPUShaderStage_Compute;
	group1_entry.buffer.type = WGPUBufferBindingType_Uniform;
	group1_entry.buffer.hasDynamicOffset = true;
	group1_entry.buffer.minBindingSize = 16;

	WGPUBindGroupLayoutDescriptor group1_descriptor = {};
	group1_descriptor.label = label("splat sort group1");
	group1_descriptor.entryCount = 1;
	group1_descriptor.entries = &group1_entry;
	m_splat_sort_group1_layout = wgpuDeviceCreateBindGroupLayout(m_device, &group1_descriptor);

	WGPUBindGroupLayout groups[] = { m_splat_sort_group0_layout, m_splat_sort_group1_layout };
	WGPUPipelineLayoutDescriptor pipeline_layout_descriptor = {};
	pipeline_layout_descriptor.label = label("splat sort");
	pipeline_layout_descriptor.bindGroupLayoutCount = 2;
	pipeline_layout_descriptor.bindGroupLayouts = groups;
	m_splat_sort_pipeline_layout = wgpuDeviceCreatePipelineLayout(m_device, &pipeline_layout_descriptor);

	const auto make_pipeline = [this](const char* const entry) -> WGPUComputePipeline {
		WGPUShaderModule module = load_wgsl(std::string("gaussian_splat_radix.") + entry + ".wgsl");
		if (module == nullptr) {
			return nullptr;
		}
		WGPUComputePipelineDescriptor descriptor = {};
		descriptor.label = label(entry);
		descriptor.layout = m_splat_sort_pipeline_layout;
		descriptor.compute.module = module;
		descriptor.compute.entryPoint = label(entry);
		return wgpuDeviceCreateComputePipeline(m_device, &descriptor);
	};

	m_splat_sort_compute_keys = make_pipeline("cs_compute_keys");
	m_splat_sort_histogram = make_pipeline("cs_histogram");
	m_splat_sort_scan_reduce = make_pipeline("cs_scan_reduce");
	m_splat_sort_scan_spine = make_pipeline("cs_scan_spine");
	m_splat_sort_scan_add = make_pipeline("cs_scan_add");
	m_splat_sort_scatter = make_pipeline("cs_scatter");
}

/// Renderer_WebGpu::initialize_splats
///
/// Mirrors Renderer_Dx12::initialize_gaussian_splatting for the point data;
/// the sort itself is sort_splats(), run once a frame from render_point_clouds
/// rather than here, since it depends on the camera. Sized to the real point
/// count rather than a fixed maximum - D3D12 pre-allocates for 15 million
/// points; this allocates only what the loaded model actually has.
void Renderer_WebGpu::initialize_splats(const GaussianSplatComponent* const splat) {
	m_splat_component = splat;

	const std::vector<PointCloudSample>* const point_cloud = splat->point_cloud();
	if (point_cloud == nullptr || point_cloud->empty()) {
		blk::warn("Renderer_WebGpu - gaussian splat component has no points (its .ply failed to load?)");
		return;
	}

	m_splat_count = (u32)point_cloud->size();

	std::vector<SplatPoint> points(m_splat_count);
	for (u32 i = 0; i < m_splat_count; i++) {
		const PointCloudSample& sample = (*point_cloud)[i];
		SplatPoint& out = points[i];

		out.position = Vec4(sample.position.x, sample.position.y, sample.position.z, 0.f);

		// Log-space scale, raw opacity logit - same conversions render_point_clouds
		// (dx12) applies before upload.
		const Vec3 linear_scale(expf(sample.scale.x), expf(sample.scale.y), expf(sample.scale.z));
		const f32 opacity = blk::clamp(1.0f / (1.0f + expf(-sample.opacity)), 0.f, 1.f);
		out.scale3d_opacity = Vec4(linear_scale.x, linear_scale.y, linear_scale.z, opacity);

		out.rotation = Vec4(sample.rotation.x, sample.rotation.y, sample.rotation.z, sample.rotation.w);
		out.sh0 = Vec4(sample.f_dc.x, sample.f_dc.y, sample.f_dc.z, 0.f);

		// f_rest is stored [all 15 red, all 15 green, all 15 blue]; the shader
		// wants it interleaved per-coefficient (R,G,B for coefficient 0, then
		// coefficient 1, ...). Only the first 8 of each channel's 15 are used -
		// degree 2 is as far as evaluate_sh() goes.
		for (u32 n = 0; n < 8; n++) {
			out.f_rest[n * 3 + 0] = sample.f_rest[n];
			out.f_rest[n * 3 + 1] = sample.f_rest[n + 15];
			out.f_rest[n * 3 + 2] = sample.f_rest[n + 30];
		}
	}

	WGPUBufferDescriptor points_descriptor = {};
	points_descriptor.label = label("splat points");
	points_descriptor.size = points.size() * sizeof(SplatPoint);
	points_descriptor.usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst;
	m_splat_points = wgpuDeviceCreateBuffer(m_device, &points_descriptor);
	wgpuQueueWriteBuffer(m_queue, m_splat_points, 0, points.data(), points_descriptor.size);

	// Sort buffers. Sized to whole tiles, same as the functional test this was
	// verified against: the kernels guard every read/write past num_elements,
	// so the padding is never touched, just kept in-bounds.
	m_splat_num_tiles = (m_splat_count + k_splat_sort_wg - 1) / k_splat_sort_wg;
	m_splat_alloc = m_splat_num_tiles * k_splat_sort_wg;

	const auto make_sort_storage = [this](const char* const name, const u64 size) -> WGPUBuffer {
		WGPUBufferDescriptor descriptor = {};
		descriptor.label = label(name);
		descriptor.size = size;
		descriptor.usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst | WGPUBufferUsage_CopySrc;
		return wgpuDeviceCreateBuffer(m_device, &descriptor);
	};
	m_splat_keys_a = make_sort_storage("splat keys a", (u64)m_splat_alloc * 4);
	m_splat_keys_b = make_sort_storage("splat keys b", (u64)m_splat_alloc * 4);
	m_splat_vals_a = make_sort_storage("splat vals a", (u64)m_splat_alloc * 4);
	m_splat_vals_b = make_sort_storage("splat vals b", (u64)m_splat_alloc * 4);
	m_splat_hist = make_sort_storage("splat hist", (u64)k_splat_radix * m_splat_num_tiles * 4);
	m_splat_block_sums = make_sort_storage("splat block sums", (u64)(std::max)(m_splat_num_tiles, 1u) * 4);

	// Identity order, so the very first frame (before sort_splats() has run)
	// draws the unsorted-but-valid order rather than whatever garbage a fresh
	// GPU allocation holds.
	std::vector<u32> identity(m_splat_count);
	for (u32 i = 0; i < m_splat_count; i++) {
		identity[i] = i;
	}
	wgpuQueueWriteBuffer(m_queue, m_splat_vals_a, 0, identity.data(), identity.size() * sizeof(u32));

	WGPUBufferDescriptor sort_globals_descriptor = {};
	sort_globals_descriptor.label = label("splat sort globals");
	sort_globals_descriptor.size = sizeof(Vec4) + 4 * sizeof(u32);
	sort_globals_descriptor.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
	m_splat_sort_globals = wgpuDeviceCreateBuffer(m_device, &sort_globals_descriptor);

	// One PassInfo slot per digit pass (shift 0/8/16/24), each aligned for use
	// as a dynamic-offset binding - the same stride pattern m_draw_stride uses.
	WGPULimits limits = {};
	wgpuDeviceGetLimits(m_device, &limits);
	const u32 alignment = (limits.minUniformBufferOffsetAlignment > 0) ? limits.minUniformBufferOffsetAlignment : 256;
	m_splat_pass_stride = ((u32)16 + alignment - 1) & ~(alignment - 1);

	WGPUBufferDescriptor pass_info_descriptor = {};
	pass_info_descriptor.label = label("splat sort pass info");
	pass_info_descriptor.size = (u64)m_splat_pass_stride * k_splat_radix_passes;
	pass_info_descriptor.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
	m_splat_pass_info = wgpuDeviceCreateBuffer(m_device, &pass_info_descriptor);
	for (u32 p = 0; p < k_splat_radix_passes; p++) {
		const u32 shift[4] = { p * 8, 0, 0, 0 };
		wgpuQueueWriteBuffer(m_queue, m_splat_pass_info, (u64)p * m_splat_pass_stride, shift, sizeof(shift));
	}

	const auto make_sort_group0 = [this](WGPUBuffer keys_in, WGPUBuffer keys_out, WGPUBuffer vals_in, WGPUBuffer vals_out) -> WGPUBindGroup {
		WGPUBindGroupEntry entries[8] = {};
		entries[0].binding = 0;
		entries[0].buffer = m_splat_sort_globals;
		entries[0].size = sizeof(Vec4) + 4 * sizeof(u32);
		entries[1].binding = k_texture_binding;
		entries[1].buffer = m_splat_points;
		entries[1].size = (u64)m_splat_count * sizeof(SplatPoint);
		entries[2].binding = k_texture_binding + 1;
		entries[2].buffer = keys_in;
		entries[2].size = (u64)m_splat_alloc * 4;
		entries[3].binding = k_texture_binding + 2;
		entries[3].buffer = vals_in;
		entries[3].size = (u64)m_splat_alloc * 4;
		entries[4].binding = 48;
		entries[4].buffer = keys_out;
		entries[4].size = (u64)m_splat_alloc * 4;
		entries[5].binding = 49;
		entries[5].buffer = vals_out;
		entries[5].size = (u64)m_splat_alloc * 4;
		entries[6].binding = 50;
		entries[6].buffer = m_splat_hist;
		entries[6].size = (u64)k_splat_radix * m_splat_num_tiles * 4;
		entries[7].binding = 51;
		entries[7].buffer = m_splat_block_sums;
		entries[7].size = (u64)(std::max)(m_splat_num_tiles, 1u) * 4;

		WGPUBindGroupDescriptor descriptor = {};
		descriptor.label = label("splat sort group0");
		descriptor.layout = m_splat_sort_group0_layout;
		descriptor.entryCount = 8;
		descriptor.entries = entries;
		return wgpuDeviceCreateBindGroup(m_device, &descriptor);
	};
	// a_to_b reads A and writes B; b_to_a is the reverse - see the header
	// comment on m_splat_sort_bg_a_to_b for how these alternate.
	m_splat_sort_bg_a_to_b = make_sort_group0(m_splat_keys_a, m_splat_keys_b, m_splat_vals_a, m_splat_vals_b);
	m_splat_sort_bg_b_to_a = make_sort_group0(m_splat_keys_b, m_splat_keys_a, m_splat_vals_b, m_splat_vals_a);

	WGPUBindGroupEntry pass_entry = {};
	pass_entry.binding = 0;
	pass_entry.buffer = m_splat_pass_info;
	pass_entry.size = 16;

	WGPUBindGroupDescriptor pass_group_descriptor = {};
	pass_group_descriptor.label = label("splat sort pass");
	pass_group_descriptor.layout = m_splat_sort_group1_layout;
	pass_group_descriptor.entryCount = 1;
	pass_group_descriptor.entries = &pass_entry;
	m_splat_sort_pass_bind_group = wgpuDeviceCreateBindGroup(m_device, &pass_group_descriptor);

	// The draw's g_sorted_indices binds vals_a permanently: after an EVEN
	// number of digit passes (4) the sorted result always lands there. See
	// sort_splats().
	WGPUBindGroupEntry bindings[3] = {};
	bindings[0].binding = k_frame_binding;
	bindings[0].buffer = m_frame_constants;
	bindings[0].size = sizeof(FrameConstants);
	bindings[1].binding = k_texture_binding;
	bindings[1].buffer = m_splat_points;
	bindings[1].size = points_descriptor.size;
	bindings[2].binding = k_texture_binding + 1;
	bindings[2].buffer = m_splat_vals_a;
	bindings[2].size = (u64)m_splat_alloc * 4;

	WGPUBindGroupDescriptor group = {};
	group.label = label("splat draw");
	group.layout = m_splat_draw_layout;
	group.entryCount = 3;
	group.entries = bindings;
	m_splat_draw_bind_group = wgpuDeviceCreateBindGroup(m_device, &group);

	blk::log("Renderer_WebGpu - loaded %u gaussian splats (%u tiles), sort pending first frame", m_splat_count, m_splat_num_tiles);
}

/// Renderer_WebGpu::sort_splats
///
/// GPU radix sort, run from render_point_clouds() before the draw. Verified
/// against a CPU reference sort (see the header comment on the sort fields)
/// before this was written; this is a mechanical port of that same dispatch
/// structure into Dawn's C API.
///
/// Every phase gets its own compute pass rather than sharing one: WebGPU gives
/// no ordering guarantee between dispatches inside a single pass, and several
/// phases here read a buffer another phase in the same digit pass just wrote
/// (the scan chain, and keys/vals ping-ponging between passes). A pass
/// boundary is what WebGPU synchronizes on.
void Renderer_WebGpu::sort_splats(const RenderCamera& camera) {
	// Sort order depends only on view DIRECTION: a pure translation of the
	// camera shifts every splat's depth by the same amount and never changes
	// their relative order, so re-sorting on translation alone would be work
	// spent on a result identical to what is already there.
	const Vec4 zc(camera.view_matrix[0].z, camera.view_matrix[1].z, camera.view_matrix[2].z, camera.view_matrix[3].z);
	const bool first_sort = std::isnan(m_splat_last_sort_zc.x);
	const bool needs_sort = first_sort ||
		fabsf(zc.x - m_splat_last_sort_zc.x) > 1e-6f ||
		fabsf(zc.y - m_splat_last_sort_zc.y) > 1e-6f ||
		fabsf(zc.z - m_splat_last_sort_zc.z) > 1e-6f;
	if (!needs_sort) {
		return;
	}
	m_splat_last_sort_zc = zc;

	struct SortGlobals {
		Vec4 zc;
		u32 num_elements;
		u32 num_tiles;
		u32 pad0;
		u32 pad1;
	};
	const SortGlobals globals = { zc, m_splat_count, m_splat_num_tiles, 0, 0 };
	wgpuQueueWriteBuffer(m_queue, m_splat_sort_globals, 0, &globals, sizeof(globals));

	// One compute pass per dispatch, matching the shape this was verified
	// against - see the header comment above.
	const auto dispatch = [this](WGPUComputePipeline pipeline, WGPUBindGroup group0, const u32 offset, const u32 groups) {
		WGPUComputePassDescriptor pass_descriptor = {};
		WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass(m_encoder, &pass_descriptor);
		wgpuComputePassEncoderSetPipeline(pass, pipeline);
		wgpuComputePassEncoderSetBindGroup(pass, 0, group0, 0, nullptr);
		wgpuComputePassEncoderSetBindGroup(pass, 1, m_splat_sort_pass_bind_group, 1, &offset);
		wgpuComputePassEncoderDispatchWorkgroups(pass, groups, 1, 1);
		wgpuComputePassEncoderEnd(pass);
		wgpuComputePassEncoderRelease(pass);
	};

	// Phase 0: keys/vals for every element, written into the A buffers -
	// bg_b_to_a's "out" side is A.
	dispatch(m_splat_sort_compute_keys, m_splat_sort_bg_b_to_a, 0, m_splat_num_tiles);

	// Four 8-bit digit passes; p even reads A writes B, p odd reads B writes A,
	// so after four (even) passes the result is back in A.
	for (u32 p = 0; p < k_splat_radix_passes; p++) {
		WGPUBindGroup bg = (p % 2 == 0) ? m_splat_sort_bg_a_to_b : m_splat_sort_bg_b_to_a;
		const u32 offset = p * m_splat_pass_stride;
		dispatch(m_splat_sort_histogram, bg, offset, m_splat_num_tiles);
		dispatch(m_splat_sort_scan_reduce, bg, offset, m_splat_num_tiles);
		dispatch(m_splat_sort_scan_spine, bg, offset, 1);
		dispatch(m_splat_sort_scan_add, bg, offset, m_splat_num_tiles);
		dispatch(m_splat_sort_scatter, bg, offset, m_splat_num_tiles);
	}
}

/// Renderer_WebGpu::add_render_component_internal
///
/// The only hook this backend needs off the base add_render_component(): a
/// gaussian splat component needs its point cloud uploaded once, matching
/// Renderer_Dx12::add_render_component_internal. Everything else (models,
/// lights, terrain, particles) is read straight from render_components() /
/// light_components() each frame instead, so it needs no registration step.
void Renderer_WebGpu::add_render_component_internal(const RenderComponent* const render_comp) {
	if (m_splat_component == nullptr && render_comp->IsA(GaussianSplatComponent::GetType())) {
		initialize_splats((const GaussianSplatComponent*)render_comp);
	}
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
	// The lit frame. Point this at a gbuffer slot instead to inspect one -
	// m_gbuffer_view[Slot_Normal] is the useful one for checking geometry.
	bindings[1].textureView = m_scene_color_view;

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
	// After the default material: the light bind group borrows its white pixel
	// as the stand-in shadow mask.
	create_light_resources();
	create_terrain_resources();
	create_particle_resources();
	create_splat_pipeline();
	create_splat_sort_pipeline();
	// After the lights: the composite reuses their group-0 layout.
	create_shadow_resources();
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

	// Every pass appends to the same two staging buffers and present() uploads
	// them once, so the counters belong to the frame rather than to any one pass.
	m_frame_draws = 0;
	m_frame_bone_draws = 0;
}

/// Renderer_WebGpu::get_pass_execute
///
/// Every pass but ui_overlay (D3D12/ImGui
/// only); an unhandled name returns nullptr and is skipped by
/// run_render_graph() with nothing touched.
RenderGraph::ExecuteFn Renderer_WebGpu::get_pass_execute(const std::string& pass_name, const std::vector<ViewContext>& views, size_t view_index) {
	static const ERenderPassMask opaque_mask = { ERenderPass::RP_Lighting };

	if (pass_name == "gbuffer") {
		return [this, &views, view_index]() { render_gbuffer(views[view_index].camera, opaque_mask); };
	}

	constexpr bool k_shadows_enabled = true;
	if (pass_name == "shadow_cascades") {
		return k_shadows_enabled
			? RenderGraph::ExecuteFn([this, &views, view_index]() { render_shadow_cascades(views[view_index].camera, opaque_mask); })
			: RenderGraph::ExecuteFn();
	}

	if (pass_name == "shadow_composite") {
		return k_shadows_enabled
			? RenderGraph::ExecuteFn([this, &views, view_index]() { render_shadow_composite(views[view_index].camera); })
			: RenderGraph::ExecuteFn();
	}

	if (pass_name == "lights") {
		return [this, &views, view_index]() { render_lights(views[view_index].camera); };
	}

	if (pass_name == "point_clouds") {
		return [this, &views, view_index]() { render_point_clouds(views[view_index].camera); };
	}

	static const ERenderPassMask translucent_mask = { ERenderPass::RP_Translucent };
	if (pass_name == "translucency") {
		return [this, &views, view_index]() { render_translucency(views[view_index].camera, translucent_mask); };
	}

	// D3D12's post_process is a straight CopyResource from SceneColor to the
	// back buffer; this does the same copy through a fullscreen triangle,
	// because a WebGPU surface texture is a render target, not a copy target.
	if (pass_name == "post_process") {
		return [this]() { blit_to_surface(); };
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
	// gaussian_splat_draw.hlsl's GlobalConstants is this same buffer (both are
	// GlobalUniformData's frame[0] slot on D3D12 - see g_global_uniform), so the
	// splat pass's parameters are written here rather than with a second upload
	// right before that pass runs.
	if (m_splat_component != nullptr) {
		frame.splat_params = Vec4(m_splat_component->splat_falloff(), m_splat_component->splat_scale(), m_splat_component->contrast(), (f32)m_splat_count);
		frame.splat_params_2 = Vec4((f32)m_splat_component->max_sh_degree(), 0.f, 0.f, 0.f);
	}
	wgpuQueueWriteBuffer(m_queue, m_frame_constants, 0, &frame, sizeof(frame));

	for (auto& render_comp : render_components()) {
		if (m_frame_draws >= m_max_draws) {
			break;
		}

		if (!render_pass_in_mask(render_comp->render_pass(), render_pass_mask)) {
			continue;
		}

		// Which pipeline, which model, which group-1 textures, and - for a
		// skinned draw - a slice of the bone buffer. Everything after this point
		// is common to all three.
		WGPURenderPipeline pipeline = nullptr;
		const Model* model = nullptr;
		WGPUBindGroup texture_group = nullptr;
		u32 bone_offset = 0;

		if (render_comp->IsA(TerrainComponent::GetType())) {
			const TerrainComponent* const terrain = static_cast<const TerrainComponent*>(render_comp);
			pipeline = m_terrain_pipeline;
			model = &terrain->model();
			texture_group = terrain_bind_group(terrain);
		} else if (render_comp->IsA(StaticModelComponent::GetType())) {
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

		// Terrain already picked its own; everything else binds one material
		// texture through the shared layout.
		if (texture_group == nullptr) {
			texture_group = material_bind_group(color_texture_id);
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
		wgpuRenderPassEncoderSetBindGroup(pass, 1, texture_group, 0, nullptr);
		wgpuRenderPassEncoderSetBindGroup(pass, 2, m_draw_bind_group, 2, dynamic_offsets);
		wgpuRenderPassEncoderSetVertexBuffer(pass, 0, vertex_buffer->buffer(), 0, vertex_buffer->buffer_size());
		wgpuRenderPassEncoderSetIndexBuffer(pass, index_buffer->buffer(), WGPUIndexFormat_Uint16, 0, index_buffer->buffer_size());
		wgpuRenderPassEncoderDrawIndexed(pass, index_buffer->num_elements(), 1, 0, 0, 0);

		m_frame_draws++;
	}

	wgpuRenderPassEncoderEnd(pass);
	wgpuRenderPassEncoderRelease(pass);
}

/// Renderer_WebGpu::render_shadow_cascades
///
/// Mirrors Renderer_Dx12::render_shadow_cascades, including the texel-snapping
/// that stops cascade edges swimming as the camera moves. The four cascades
/// share one atlas, a quadrant each, selected by viewport - so this is one
/// render pass with four sets of draws rather than four passes.
///
/// The web build's shadows were once wrong everywhere because the level's
/// `CascadeStartDistances` array parsed as zeros there: the CRLF level text kept
/// its `CR` off Windows, and `File::ReadComponent`'s float-array skip assumes
/// LF. `File::Open` now strips `CR`, and the cascade distances match D3D12's.
void Renderer_WebGpu::render_shadow_cascades(const RenderCamera& camera, const ERenderPassMask& render_pass_mask) {
	m_shadows_valid = false;

	const DirectionalLightComponent* dir_light = nullptr;
	for (const auto light : light_components()) {
		if (light->casts_shadow() && light->IsA(DirectionalLightComponent::GetType())) {
			dir_light = (const DirectionalLightComponent*)light;
			break;
		}
	}
	if (dir_light == nullptr || m_shadow_static_pipeline == nullptr) {
		return;
	}

	const auto& cascade_dists = dir_light->cascade_start_distances();
	if (cascade_dists.empty()) {
		return;
	}

	// The camera frustum's upper-left corner ray, which sets how wide each
	// cascade has to be.
	const Mat4& vp_matrix = camera.view_projection_matrix;
	const Vec3 cam_dir = camera.view_rotation.to_mat4()[2].ToVec3();

	Plane3d frustum_planes[6] = {};
	Vec3 ul, ur, lr, ll, extra;
	vp_matrix.left_clip_plane(frustum_planes[0]);
	vp_matrix.top_clip_plane(frustum_planes[1]);
	vp_matrix.right_clip_plane(frustum_planes[2]);
	vp_matrix.bottom_clip_plane(frustum_planes[3]);
	vp_matrix.near_clip_plane(frustum_planes[4]);
	vp_matrix.far_clip_plane(frustum_planes[5]);

	frustum_planes[1].intersects_plane(extra, ul, frustum_planes[0]);
	frustum_planes[2].intersects_plane(extra, ur, frustum_planes[1]);
	frustum_planes[3].intersects_plane(extra, lr, frustum_planes[2]);
	frustum_planes[0].intersects_plane(extra, ll, frustum_planes[3]);

	WGPURenderPassColorAttachment color_attachment = {};
	color_attachment.view = m_shadow_atlas_view;
	color_attachment.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
	color_attachment.loadOp = WGPULoadOp_Clear;
	color_attachment.storeOp = WGPUStoreOp_Store;
	// Nothing drawn means nothing occluding, which is what 1 reads as.
	color_attachment.clearValue = { 1.0, 1.0, 1.0, 1.0 };

	WGPURenderPassDepthStencilAttachment depth_attachment = {};
	depth_attachment.view = m_shadow_depth_view;
	depth_attachment.depthLoadOp = WGPULoadOp_Clear;
	depth_attachment.depthStoreOp = WGPUStoreOp_Store;
	depth_attachment.depthClearValue = 1.0f;

	WGPURenderPassDescriptor pass_descriptor = {};
	pass_descriptor.label = label("shadow_cascades");
	pass_descriptor.colorAttachmentCount = 1;
	pass_descriptor.colorAttachments = &color_attachment;
	pass_descriptor.depthStencilAttachment = &depth_attachment;

	WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(m_encoder, &pass_descriptor);

	const f32 half_dimension = (f32)(k_shadow_dimensions >> 1);
	const u32 cascade_count = (u32)(std::min)((size_t)4, cascade_dists.size());

	for (u32 i = 0; i < 4; i++) {
		m_light_matrices[i].make_identity();
	}

	for (u32 cascade = 0; cascade < cascade_count; cascade++) {
		m_cascade_distances[cascade] = cascade_dists[cascade];

		wgpuRenderPassEncoderSetViewport(pass,
			(f32)((cascade % 2) * (u32)half_dimension), (f32)((cascade / 2) * (u32)half_dimension),
			half_dimension, half_dimension, 0.f, 1.f);

		const float prev_cascade_dist = (cascade == 0) ? 0.0f : (cascade_dists[cascade] - 1);
		const Vec3 look_at_point = camera.view_position + cam_dir * (prev_cascade_dist + (cascade_dists[cascade] - prev_cascade_dist) * 0.5f);
		const float half_fov = g_fov * 0.5f;
		const float dist_to_corner = cascade_dists[cascade] / cosf(half_fov);
		const Vec3 corner_vert = camera.view_position + dist_to_corner * ul;
		const float bounds_len = (look_at_point - corner_vert).length();

		const Vec3 light_dir = dir_light->owner_rotation().to_mat4()[2].ToVec3();
		const Mat4 light_view = Mat4::look_at(look_at_point + light_dir * bounds_len * 10.f, look_at_point, Vec3(0.0f, 1.0f, 0.0f));
		const Mat4 light_view_proj = light_view * Mat4::ortho_lh(bounds_len * 2.0f, bounds_len * 2.0f, 10.0f, bounds_len * 40.0f);

		// Snap to whole shadow texels so the cascade does not swim under a
		// moving camera - see the blog post the D3D12 copy of this cites.
		const f32 texel_size = 2.0f / half_dimension;
		Vec4 proj_center(0.0f, 0.0f, 0.0f, 1.0f);
		proj_center = proj_center.transform_point(light_view_proj, true);

		const float fracX = fmodf(proj_center.x, texel_size);
		const float fracY = fmodf(proj_center.y, texel_size);

		Mat4 offset;
		offset.make_identity();
		offset[3][0] = -fracX;
		offset[3][1] = -fracY;

		// Clip space to the cascade's own quadrant of the atlas.
		Mat4 texture_matrix;
		texture_matrix.make_identity();
		texture_matrix[0].x = 0.5f;
		texture_matrix[1].y = -0.5f;
		texture_matrix[3].x = 0.5f + (0.5f / k_shadow_dimensions);
		texture_matrix[3].y = 0.5f + (0.5f / k_shadow_dimensions);

		const Mat4 cascade_mat = light_view_proj * offset;
		m_light_matrices[cascade] = cascade_mat * texture_matrix;

		for (auto& render_comp : render_components()) {
			if (m_frame_draws >= m_max_draws) {
				break;
			}
			// No casts_shadow() filter here, matching D3D12: the flag is off by
			// default on these components, and honouring it would empty the
			// atlas rather than shrink it.
			if (!render_pass_in_mask(render_comp->render_pass(), render_pass_mask)) {
				continue;
			}

			WGPURenderPipeline pipeline = nullptr;
			const Model* model = nullptr;
			u32 bone_offset = 0;

			if (render_comp->IsA(StaticModelComponent::GetType())) {
				pipeline = m_shadow_static_pipeline;
				model = static_cast<const StaticModelComponent*>(render_comp)->model();
			} else if (render_comp->IsA(SkeletalModelComponent::GetType())) {
				if (m_frame_bone_draws >= m_max_bone_draws) {
					continue;
				}

				const SkeletalModelComponent* const skeletal = static_cast<const SkeletalModelComponent*>(render_comp);
				pipeline = m_shadow_skinned_pipeline;
				model = skeletal->model();

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

			Mat4 world_mat;
			world_mat.make_scale(render_comp->scale());
			world_mat *= render_comp->rotation().to_mat4();
			world_mat[3] = render_comp->position();

			// The only thing that differs from a gbuffer draw: mvp goes through
			// the light rather than the camera.
			DrawConstants draw = {};
			draw.world = world_mat;
			draw.mvp = world_mat * cascade_mat;
			draw.inv_world = world_mat;
			draw.inv_world.inverse_self();

			const u32 draw_offset = m_frame_draws * m_draw_stride;
			memcpy(m_draw_staging.data() + draw_offset, &draw, sizeof(draw));

			const u32 dynamic_offsets[2] = { draw_offset, bone_offset };

			wgpuRenderPassEncoderSetPipeline(pass, pipeline);
			wgpuRenderPassEncoderSetBindGroup(pass, 0, m_frame_bind_group, 0, nullptr);
			wgpuRenderPassEncoderSetBindGroup(pass, 1, material_bind_group(0), 0, nullptr);
			wgpuRenderPassEncoderSetBindGroup(pass, 2, m_draw_bind_group, 2, dynamic_offsets);
			wgpuRenderPassEncoderSetVertexBuffer(pass, 0, vertex_buffer->buffer(), 0, vertex_buffer->buffer_size());
			wgpuRenderPassEncoderSetIndexBuffer(pass, index_buffer->buffer(), WGPUIndexFormat_Uint16, 0, index_buffer->buffer_size());
			wgpuRenderPassEncoderDrawIndexed(pass, index_buffer->num_elements(), 1, 0, 0, 0);

			m_frame_draws++;
		}
	}

	wgpuRenderPassEncoderEnd(pass);
	wgpuRenderPassEncoderRelease(pass);

	m_shadows_valid = true;
}

/// Renderer_WebGpu::render_shadow_composite
///
/// Projects the cascade atlas into the Lighting target, which is the shadow mask
/// the directional light then multiplies its contribution by.
void Renderer_WebGpu::render_shadow_composite(const RenderCamera& camera) {
	WGPURenderPassColorAttachment color_attachment = {};
	color_attachment.view = m_lighting_view;
	color_attachment.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
	color_attachment.loadOp = WGPULoadOp_Clear;
	color_attachment.storeOp = WGPUStoreOp_Store;
	// Unlit until proven otherwise is wrong for a viewer: with no shadow pass
	// the mask has to read "fully lit", so the clear is white.
	color_attachment.clearValue = { 1.0, 1.0, 1.0, 1.0 };

	WGPURenderPassDescriptor pass_descriptor = {};
	pass_descriptor.label = label("shadow_composite");
	pass_descriptor.colorAttachmentCount = 1;
	pass_descriptor.colorAttachments = &color_attachment;

	WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(m_encoder, &pass_descriptor);

	if (m_shadows_valid && m_shadow_composite_pipeline != nullptr && m_frame_draws < m_max_draws) {
		LightConstants light_data = {};
		for (u32 i = 0; i < 4; i++) {
			light_data.light_matrices[i] = m_light_matrices[i];
		}
		light_data.cascade_distances = m_cascade_distances;
		light_data.player_inv_view_proj = camera.inv_view_projection_matrix;
		light_data.player_camera_pos = Vec4(camera.view_position, 1.f);

		const u32 draw_offset = m_frame_draws * m_draw_stride;
		memcpy(m_draw_staging.data() + draw_offset, &light_data, sizeof(light_data));

		const u32 dynamic_offsets[2] = { draw_offset, 0 };

		wgpuRenderPassEncoderSetPipeline(pass, m_shadow_composite_pipeline);
		wgpuRenderPassEncoderSetBindGroup(pass, 0, m_light_frame_bind_group, 0, nullptr);
		wgpuRenderPassEncoderSetBindGroup(pass, 1, m_shadow_composite_bind_group, 0, nullptr);
		wgpuRenderPassEncoderSetBindGroup(pass, 2, m_draw_bind_group, 2, dynamic_offsets);
		wgpuRenderPassEncoderSetVertexBuffer(pass, 0, m_quad_vertices, 0, sizeof(k_quad_vertices));
		wgpuRenderPassEncoderDraw(pass, 6, 1, 0, 0);

		m_frame_draws++;
	}

	wgpuRenderPassEncoderEnd(pass);
	wgpuRenderPassEncoderRelease(pass);
}

/// Renderer_WebGpu::render_lights
///
/// Mirrors Renderer_Dx12::render_lights_internal: SceneColor is cleared, then
/// every light draws the same fullscreen quad over the gbuffer and blends in
/// additively. Shadows are not implemented, so the shadow mask the directional
/// shader samples is the white pixel and everything reads as lit.
void Renderer_WebGpu::render_lights(const RenderCamera& camera) {
	WGPURenderPassColorAttachment color_attachment = {};
	color_attachment.view = m_scene_color_view;
	color_attachment.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
	color_attachment.loadOp = WGPULoadOp_Clear;
	color_attachment.storeOp = WGPUStoreOp_Store;
	color_attachment.clearValue = { 0.0, 0.0, 0.0, 0.0 };

	WGPURenderPassDescriptor pass_descriptor = {};
	pass_descriptor.label = label("lights");
	pass_descriptor.colorAttachmentCount = 1;
	pass_descriptor.colorAttachments = &color_attachment;

	WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(m_encoder, &pass_descriptor);

	for (auto& light : light_components()) {
		if (m_frame_draws >= m_max_draws) {
			break;
		}

		const bool is_directional = light->IsA(DirectionalLightComponent::GetType());
		WGPURenderPipeline pipeline = is_directional ? m_directional_light_pipeline : m_point_light_pipeline;
		if (pipeline == nullptr) {
			continue;
		}

		LightConstants light_data = {};
		light_data.position = Vec4(light->owner_position(), light->radius());
		light_data.color = light->GetColor();
		light_data.direction = Vec4(light->owner_rotation().to_mat4()[2].ToVec3(), 0.f);
		for (u32 i = 0; i < 4; i++) {
			light_data.light_matrices[i].make_identity();
		}
		light_data.player_inv_view_proj = camera.inv_view_projection_matrix;
		light_data.player_camera_pos = Vec4(camera.view_position, 1.f);

		const u32 draw_offset = m_frame_draws * m_draw_stride;
		memcpy(m_draw_staging.data() + draw_offset, &light_data, sizeof(light_data));

		// Binding order again: the light constants take the draw slot, and the
		// bone offset just has to be valid - these shaders never read it.
		const u32 dynamic_offsets[2] = { draw_offset, 0 };

		wgpuRenderPassEncoderSetPipeline(pass, pipeline);
		wgpuRenderPassEncoderSetBindGroup(pass, 0, m_light_frame_bind_group, 0, nullptr);
		wgpuRenderPassEncoderSetBindGroup(pass, 1, m_light_bind_group, 0, nullptr);
		wgpuRenderPassEncoderSetBindGroup(pass, 2, m_draw_bind_group, 2, dynamic_offsets);
		wgpuRenderPassEncoderSetVertexBuffer(pass, 0, m_quad_vertices, 0, sizeof(k_quad_vertices));
		wgpuRenderPassEncoderDraw(pass, 6, 1, 0, 0);

		m_frame_draws++;
	}

	wgpuRenderPassEncoderEnd(pass);
	wgpuRenderPassEncoderRelease(pass);
}

/// Renderer_WebGpu::render_point_clouds
///
/// Mirrors Renderer_Dx12::render_point_clouds - draw order now comes from
/// sort_splats()'s GPU radix sort rather than D3D12's CPU thread, but the
/// draw itself is faithful: same blend, same depth test, one draw of
/// splat_count * 6 vertices with no vertex buffer at all (the shader builds
/// each billboard from vertex_index).
void Renderer_WebGpu::render_point_clouds(const RenderCamera& camera) {
	if (m_splat_component == nullptr || m_splat_count == 0 || m_splat_draw_pipeline == nullptr) {
		return;
	}

	sort_splats(camera);

	WGPURenderPassColorAttachment color_attachment = {};
	color_attachment.view = m_scene_color_view;
	color_attachment.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
	color_attachment.loadOp = WGPULoadOp_Load;
	color_attachment.storeOp = WGPUStoreOp_Store;

	WGPURenderPassDepthStencilAttachment depth_attachment = {};
	depth_attachment.view = m_depth_view;
	depth_attachment.depthReadOnly = true;

	WGPURenderPassDescriptor pass_descriptor = {};
	pass_descriptor.label = label("point_clouds");
	pass_descriptor.colorAttachmentCount = 1;
	pass_descriptor.colorAttachments = &color_attachment;
	pass_descriptor.depthStencilAttachment = &depth_attachment;

	WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(m_encoder, &pass_descriptor);
	wgpuRenderPassEncoderSetPipeline(pass, m_splat_draw_pipeline);
	wgpuRenderPassEncoderSetBindGroup(pass, 0, m_splat_draw_bind_group, 0, nullptr);
	wgpuRenderPassEncoderDraw(pass, m_splat_count * 6, 1, 0, 0);
	wgpuRenderPassEncoderEnd(pass);
	wgpuRenderPassEncoderRelease(pass);
}

/// Renderer_WebGpu::render_translucency
///
/// Sprite particles on top of the lit frame. SceneColor and the depth buffer are
/// both loaded rather than cleared - the lights just filled one and the gbuffer
/// pass the other - and depth is read-only, since nothing here writes it.
void Renderer_WebGpu::render_translucency(const RenderCamera& camera, const ERenderPassMask& render_pass_mask) {
	WGPURenderPassColorAttachment color_attachment = {};
	color_attachment.view = m_scene_color_view;
	color_attachment.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
	color_attachment.loadOp = WGPULoadOp_Load;
	color_attachment.storeOp = WGPUStoreOp_Store;

	WGPURenderPassDepthStencilAttachment depth_attachment = {};
	depth_attachment.view = m_depth_view;
	depth_attachment.depthReadOnly = true;

	WGPURenderPassDescriptor pass_descriptor = {};
	pass_descriptor.label = label("translucency");
	pass_descriptor.colorAttachmentCount = 1;
	pass_descriptor.colorAttachments = &color_attachment;
	pass_descriptor.depthStencilAttachment = &depth_attachment;

	WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(m_encoder, &pass_descriptor);

	for (auto& render_comp : render_components()) {
		if (!render_pass_in_mask(render_comp->render_pass(), render_pass_mask)) {
			continue;
		}
		if (!render_comp->IsA(ParticleComponent::GetType())) {
			continue;
		}

		const ParticleComponent* const particle = static_cast<const ParticleComponent*>(render_comp);
		const Model* const model = particle->get_model();
		if (model == nullptr) {
			// The double-buffered particle mesh is not filled in yet.
			continue;
		}

		const RenderBuffer_WebGpu* const vertex_buffer = (const RenderBuffer_WebGpu*)model->vertex_buffer();
		const RenderBuffer_WebGpu* const index_buffer = (const RenderBuffer_WebGpu*)model->index_buffer();
		if (vertex_buffer == nullptr || index_buffer == nullptr ||
			vertex_buffer->buffer() == nullptr || index_buffer->buffer() == nullptr) {
			continue;
		}

		u32 color_texture_id = 0;
		bool additive = false;
		if (particle->materials().size() > 0) {
			additive = particle->materials()[0].blend_override() == EBlendMode::Additive;
			for (const auto& param : particle->materials()[0].shader_params()) {
				if (param.param_name() == String("color_tex") && param.texture() != nullptr) {
					color_texture_id = param.texture()->get_texture_id();
				}
			}
		}

		WGPURenderPipeline pipeline = additive ? m_particle_additive_pipeline : m_particle_alpha_pipeline;
		if (pipeline == nullptr) {
			continue;
		}

		// Group 2 still has to be bound to satisfy the layout even though these
		// shaders never read it; slot 0 is as good as any.
		const u32 dynamic_offsets[2] = { 0, 0 };

		wgpuRenderPassEncoderSetPipeline(pass, pipeline);
		wgpuRenderPassEncoderSetBindGroup(pass, 0, m_frame_bind_group, 0, nullptr);
		wgpuRenderPassEncoderSetBindGroup(pass, 1, material_bind_group(color_texture_id), 0, nullptr);
		wgpuRenderPassEncoderSetBindGroup(pass, 2, m_draw_bind_group, 2, dynamic_offsets);
		wgpuRenderPassEncoderSetVertexBuffer(pass, 0, vertex_buffer->buffer(), 0, vertex_buffer->buffer_size());
		wgpuRenderPassEncoderSetIndexBuffer(pass, index_buffer->buffer(), WGPUIndexFormat_Uint16, 0, index_buffer->buffer_size());
		wgpuRenderPassEncoderDrawIndexed(pass, index_buffer->num_elements(), 1, 0, 0, 0);
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

	// One upload for everything every pass staged. Queue writes are ordered
	// against submits rather than against encoding, so doing this after the
	// passes are recorded but before the submit is what makes it legal.
	if (m_frame_draws > 0) {
		wgpuQueueWriteBuffer(m_queue, m_draw_constants, 0, m_draw_staging.data(), (size_t)m_frame_draws * m_draw_stride);
	}
	if (m_frame_bone_draws > 0) {
		wgpuQueueWriteBuffer(m_queue, m_bone_constants, 0, m_bone_staging.data(), (size_t)m_frame_bone_draws * m_bone_stride);
	}

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
