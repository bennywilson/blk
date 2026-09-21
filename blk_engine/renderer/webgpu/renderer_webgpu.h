/// renderer_webgpu.h
///
/// 2026 blk

#pragma once

#include <unordered_map>
#include <webgpu/webgpu.h>
#include "renderer.h"

/// Renderer_WebGpu
///
/// The backend the web viewer will run on, hand-written against Dawn's
/// `webgpu.h` (Phase 5). Dawn implements that API over D3D12 here and the
/// browser implements it in the wasm build, so this is one backend for both -
/// and it is debugged natively first, where RenderDoc works.
///
/// What runs today: the "gbuffer" pass draws static and skinned models, with
/// their textures, into the same five targets the D3D12 backend uses, then a
/// blit puts the Color target on screen. Terrain and particles are not in it
/// yet, and every other pass is opted out of by returning nullptr from
/// `get_pass_execute()`, exactly as `Renderer_Null` does.
///
/// Shaders are the WGSL under `assets/shaders/wgsl`, generated from the same
/// HLSL D3D12 compiles by `tools/shaders/hlsl_to_wgsl.py` (`-D BLK_WEB`
/// switches common_global.hlsli's binding macros from bindless to the bound
/// form). The bind groups follow that generated layout:
///
///     group 0  per frame  b0 frame constants, s0 sampler
///     group 1  per pass   t0.. material textures
///     group 2  per draw   b0 draw constants, as a dynamic offset
///
/// Selected with `-renderer=webgpu`; D3D12 stays the default.
class Renderer_WebGpu : public Renderer {
public:
	~Renderer_WebGpu() override;

	u32 load_texture(const std::string& path, LoadTextureParams& params) override;

	WGPUDevice device() const { return m_device; }
	WGPUQueue queue() const { return m_queue; }

private:
	void initialize_internal(HWND hwnd, const uint32_t frame_width, const uint32_t frame_height) override;
	void shut_down_internal() override;

	// One command encoder per frame, opened here and submitted in present(), so
	// a frame is a single command buffer as it is on D3D12.
	void begin_frame_resources() override;
	RenderGraph::ExecuteFn get_pass_execute(const std::string& pass_name, const std::vector<ViewContext>& views, size_t view_index) override;
	void present() override;

	RenderPipeline* create_gpu_pipeline(const std::string& friendly_name, const std::string& path) override;
	RenderPipeline* create_compute_pipeline(const std::string& friendly_name, const std::string& path) override;
	RenderBuffer* create_render_buffer_internal() override;

	// Each blocks on wgpuInstanceProcessEvents until its callback fires. Fine at
	// startup, which is the only place they run.
	bool request_adapter();
	bool request_device();

	void create_frame_targets();
	void create_bind_group_layouts();
	void create_default_material();
	WGPUShaderModule load_wgsl(const std::string& file_name);

	// `skinned` picks the vertex layout: the skinned shaders read the bone
	// indices and weights the static ones ignore.
	WGPURenderPipeline create_material_pipeline(const std::string& shader_name, const bool skinned);
	void create_blit_pipeline();

	void render_gbuffer(const RenderCamera& camera, const ERenderPassMask& render_pass_mask);
	void blit_to_surface();

	WGPUInstance m_instance = nullptr;
	WGPUAdapter m_adapter = nullptr;
	WGPUDevice m_device = nullptr;
	WGPUQueue m_queue = nullptr;
	WGPUSurface m_surface = nullptr;
	WGPUTextureFormat m_surface_format = WGPUTextureFormat_Undefined;

	// The pass records into this; present() submits it with the blit appended,
	// so a frame is one command buffer as it is on D3D12.
	WGPUCommandEncoder m_encoder = nullptr;

	// Color/Normal/Specular/SceneDepth/EntityId, matching ERenderTarget's order
	// and the five outputs of the material pixel shaders.
	static constexpr u32 k_gbuffer_target_count = 5;
	WGPUTexture m_gbuffer[k_gbuffer_target_count] = {};
	WGPUTextureView m_gbuffer_view[k_gbuffer_target_count] = {};
	WGPUTexture m_depth_target = nullptr;
	WGPUTextureView m_depth_view = nullptr;

	// One uniform buffer for the frame's constants, one for every draw's. The
	// draw buffer is addressed by dynamic offset, which is why its stride is
	// rounded up to the device's minUniformBufferOffsetAlignment.
	WGPUBuffer m_frame_constants = nullptr;
	WGPUBuffer m_draw_constants = nullptr;
	u32 m_draw_stride = 0;
	u32 m_max_draws = 0;
	std::vector<u8> m_draw_staging;

	// Bones live in their own dynamic-offset buffer, bound beside the draw
	// constants, because a skinned draw advances both independently - the same
	// split D3D12 makes with its separate bone table.
	WGPUBuffer m_bone_constants = nullptr;
	u32 m_bone_stride = 0;
	u32 m_max_bone_draws = 0;
	std::vector<u8> m_bone_staging;
	u32 m_frame_bone_draws = 0;

	WGPUBindGroupLayout m_frame_layout = nullptr;
	WGPUBindGroupLayout m_material_layout = nullptr;
	WGPUBindGroupLayout m_draw_layout = nullptr;
	WGPUPipelineLayout m_material_pipeline_layout = nullptr;
	WGPUBindGroup m_frame_bind_group = nullptr;
	WGPUBindGroup m_draw_bind_group = nullptr;

	// Every loaded texture, with the group-1 bind group it is bound through.
	// Index 0 is a white pixel, which is what a material with no texture (or a
	// texture that failed to load) draws with - white leaves its colour
	// unmodulated, where black would swallow it.
	struct MaterialTexture {
		WGPUTexture texture = nullptr;
		WGPUTextureView view = nullptr;
		WGPUBindGroup bind_group = nullptr;
	};
	std::vector<MaterialTexture> m_textures;
	WGPUBindGroup material_bind_group(const u32 texture_id) const;

	WGPUSampler m_sampler = nullptr;
	bool m_block_compression = false;

	std::unordered_map<std::string, WGPURenderPipeline> m_material_pipelines;

	WGPURenderPipeline m_blit_pipeline = nullptr;
	WGPUBindGroupLayout m_blit_layout = nullptr;
	WGPUBindGroup m_blit_bind_group = nullptr;

	u32 m_frame_index = 0;
	u32 m_frame_draws = 0;
};
