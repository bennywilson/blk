/// renderer_webgpu.cpp
///
/// 2026 blk

#include <cmath>
#include <fstream>
#include "blk_core.h"
#include "entity_header.h"
#include "renderer_webgpu.h"

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

	constexpr u32 k_frame_binding = 0;
	constexpr u32 k_sampler_binding = 32;
	constexpr u32 k_texture_binding = 16;
	constexpr u32 k_draw_binding = 0;

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

	WGPUDeviceDescriptor descriptor = {};
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

	WGPUBindGroupLayoutEntry draw_entry = {};
	draw_entry.binding = k_draw_binding;
	draw_entry.visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
	draw_entry.buffer.type = WGPUBufferBindingType_Uniform;
	draw_entry.buffer.hasDynamicOffset = true;
	draw_entry.buffer.minBindingSize = sizeof(DrawConstants);

	WGPUBindGroupLayoutDescriptor draw_layout = {};
	draw_layout.label = label("draw");
	draw_layout.entryCount = 1;
	draw_layout.entries = &draw_entry;
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

	WGPUBindGroupEntry draw_binding = {};
	draw_binding.binding = k_draw_binding;
	draw_binding.buffer = m_draw_constants;
	draw_binding.size = sizeof(DrawConstants);

	WGPUBindGroupDescriptor draw_group = {};
	draw_group.layout = m_draw_layout;
	draw_group.entryCount = 1;
	draw_group.entries = &draw_binding;
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
	m_white_texture = wgpuDeviceCreateTexture(m_device, &descriptor);

	const u8 white[4] = { 255, 255, 255, 255 };
	WGPUTexelCopyTextureInfo destination = {};
	destination.texture = m_white_texture;
	WGPUTexelCopyBufferLayout layout = {};
	layout.bytesPerRow = 4;
	layout.rowsPerImage = 1;
	WGPUExtent3D extent = { 1, 1, 1 };
	wgpuQueueWriteTexture(m_queue, &destination, white, sizeof(white), &layout, &extent);

	WGPUTextureView view = wgpuTextureCreateView(m_white_texture, nullptr);

	WGPUBindGroupEntry binding = {};
	binding.binding = k_texture_binding;
	binding.textureView = view;

	WGPUBindGroupDescriptor group = {};
	group.label = label("default material");
	group.layout = m_material_layout;
	group.entryCount = 1;
	group.entries = &binding;
	m_default_material_bind_group = wgpuDeviceCreateBindGroup(m_device, &group);
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
WGPURenderPipeline Renderer_WebGpu::create_material_pipeline(const std::string& shader_name) {
	WGPUShaderModule vertex_module = load_wgsl(shader_name + ".vertex_shader.wgsl");
	WGPUShaderModule fragment_module = load_wgsl(shader_name + ".pixel_shader.wgsl");
	if (vertex_module == nullptr || fragment_module == nullptr) {
		return nullptr;
	}

	WGPUVertexAttribute attributes[2] = {};
	attributes[0].format = WGPUVertexFormat_Float32x3;
	attributes[0].offset = offsetof(vertexLayout, position);
	attributes[0].shaderLocation = 0;
	attributes[1].format = WGPUVertexFormat_Float32x2;
	attributes[1].offset = offsetof(vertexLayout, uv);
	attributes[1].shaderLocation = 1;

	WGPUVertexBufferLayout vertex_buffer = {};
	vertex_buffer.arrayStride = sizeof(vertexLayout);
	vertex_buffer.stepMode = WGPUVertexStepMode_Vertex;
	vertex_buffer.attributeCount = 2;
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
	WGPUSurfaceSourceWindowsHWND from_hwnd = {};
	from_hwnd.chain.sType = WGPUSType_SurfaceSourceWindowsHWND;
	from_hwnd.hinstance = GetModuleHandle(nullptr);
	from_hwnd.hwnd = hwnd;

	WGPUSurfaceDescriptor surface_descriptor = {};
	surface_descriptor.nextInChain = &from_hwnd.chain;
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

	m_material_pipelines["static_model"] = create_material_pipeline("static_model");

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
/// per-draw constants, the same draw order. Static models only for now -
/// skinned models need their bone table bound and particles and terrain have
/// their own pipelines.
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

	WGPURenderPipeline static_model_pipeline = m_material_pipelines["static_model"];
	m_frame_draws = 0;

	for (auto& render_comp : render_components()) {
		if (m_frame_draws >= m_max_draws || static_model_pipeline == nullptr) {
			break;
		}

		if (!render_pass_in_mask(render_comp->render_pass(), render_pass_mask)) {
			continue;
		}

		if (!render_comp->IsA(StaticModelComponent::GetType())) {
			continue;
		}

		const StaticModelComponent* const model_comp = static_cast<const StaticModelComponent*>(render_comp);
		const Model* const model = model_comp->model();
		if (model == nullptr) {
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
		if (render_comp->materials().size() > 0) {
			for (const auto& param : render_comp->materials()[0].shader_params()) {
				if (param.param_name() == String("color")) {
					color = param.vector();
				}
				if (param.param_name() == String("spec")) {
					spec = param.vector();
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

		wgpuRenderPassEncoderSetPipeline(pass, static_model_pipeline);
		wgpuRenderPassEncoderSetBindGroup(pass, 0, m_frame_bind_group, 0, nullptr);
		wgpuRenderPassEncoderSetBindGroup(pass, 1, m_default_material_bind_group, 0, nullptr);
		wgpuRenderPassEncoderSetBindGroup(pass, 2, m_draw_bind_group, 1, &draw_offset);
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

	wgpuSurfacePresent(m_surface);

	// Drives Dawn's callbacks (device errors among them), so a problem is
	// reported on the frame it happens rather than at shutdown.
	wgpuInstanceProcessEvents(m_instance);

	m_frame_index++;
	if ((m_frame_index % 600) == 0) {
		blk::log("Renderer_WebGpu - frame %u, %u draws", m_frame_index, m_frame_draws);
	}
}

/// Renderer_WebGpu::load_texture
u32 Renderer_WebGpu::load_texture(const std::string& path, LoadTextureParams& params) {
	return 0;
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
