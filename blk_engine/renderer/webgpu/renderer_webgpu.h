/// renderer_webgpu.h
///
/// 2026 blk

#pragma once

#include <cmath>
#include <unordered_map>
#include <webgpu/webgpu.h>
#include "renderer.h"

class TerrainComponent;
class GaussianSplatComponent;

/// Renderer_WebGpu
///
/// The backend the web viewer will run on, hand-written against Dawn's
/// `webgpu.h` (Phase 5). Dawn implements that API over D3D12 here and the
/// browser implements it in the wasm build, so this is one backend for both -
/// and it is debugged natively first, where RenderDoc works.
///
/// What runs today: "gbuffer" draws static and skinned models plus terrain,
/// with their textures, into the same five targets the D3D12 backend uses;
/// "lights" accumulates the directional and point lights into SceneColor;
/// "translucency" draws sprite particles over that; "point_clouds" draws a
/// gaussian splat model, unsorted; and a blit puts SceneColor on screen.
/// Shadows are written (see render_shadow_cascades()) but opted out of in
/// `get_pass_execute()` - the projection is not correct yet. Every opted-out
/// pass returns nullptr, exactly as `Renderer_Null` does.
///
/// Shaders are the WGSL under `assets/shaders/wgsl`, generated from the same
/// HLSL D3D12 compiles by `tools/shaders/hlsl_to_wgsl.py` (`-D BLK_WEB`
/// switches common_global.hlsli's binding macros from bindless to the bound
/// form). The bind groups follow that generated layout:
///
///     group 0  per frame  b0 frame constants, s0 sampler
///     group 1  per pass   t0.. material textures, or the gbuffer for a light
///     group 2  per draw   b0 draw constants, as a dynamic offset
///
/// Selected with `-renderer=webgpu`; D3D12 stays the default.
class Renderer_WebGpu : public Renderer {
public:
	~Renderer_WebGpu() override;

	u32 load_texture(const std::string& path, LoadTextureParams& params) override;

	WGPUDevice device() const { return m_device; }
	WGPUQueue queue() const { return m_queue; }

	// Viewport click-to-select; see Renderer::request_entity_id_pick(). The read
	// is a 1x1 copy of the gbuffer's EntityId target into a mappable buffer, mapped
	// once the frame carrying the copy has been submitted, so the answer arrives a
	// frame or two later - the browser's mapAsync cannot be waited on.
	void request_entity_id_pick(const u32 backbuffer_x, const u32 backbuffer_y) override;
	bool try_take_entity_id_pick(u32& out_entity_id) override;

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

	// Terrain blends two layers by a splat map, so it needs three textures in
	// group 1 where a material needs one - a different layout, and therefore a
	// pipeline of its own.
	void create_terrain_resources();
	WGPUBindGroup terrain_bind_group(const TerrainComponent* const terrain);

	// Sprite particles: their own vertex layout (ParticleVertex, not
	// vertexLayout) and one pipeline per blend mode.
	void create_particle_resources();
	WGPURenderPipeline create_particle_pipeline(const bool additive);

	// Cascaded shadows: a four-quadrant depth atlas, then a fullscreen pass that
	// projects it into the Lighting target the directional light samples.
	void create_shadow_resources();
	WGPURenderPipeline create_shadow_depth_pipeline(const std::string& shader_name, const bool skinned);

	// Gaussian splats. Everything they bind sits in group 0, so they get their
	// own layout rather than sharing the material ones.
	void add_render_component_internal(const RenderComponent* const render_comp) override;
	void create_splat_pipeline();
	void create_splat_sort_pipeline();
	void initialize_splats(const GaussianSplatComponent* const splat);
	void sort_splats(const RenderCamera& camera);

	// The fullscreen quad every light draws, and the group-1 bind group holding
	// the gbuffer the light shaders read it back through.
	void create_light_resources();
	WGPURenderPipeline create_light_pipeline(const std::string& shader_name);

	void render_gbuffer(const RenderCamera& camera, const ERenderPassMask& render_pass_mask);
	void render_shadow_cascades(const RenderCamera& camera, const ERenderPassMask& render_pass_mask);
	void render_shadow_composite(const RenderCamera& camera);
	void render_lights(const RenderCamera& camera);
	void render_point_clouds(const RenderCamera& camera);
	void render_translucency(const RenderCamera& camera, const ERenderPassMask& render_pass_mask);
	void blit_to_surface();
	void render_ui_overlay();
	void release_frame_surface();

	void copy_entity_id_pick_pixel();
	void begin_entity_id_pick_readback();

	// 256 bytes is the smallest a buffer-to-texture row can be, and the one float
	// the copy writes sits at its start.
	WGPUBuffer m_pick_readback = nullptr;
	bool m_pick_requested = false;
	bool m_pick_copy_recorded = false;
	bool m_pick_mapping = false;
	bool m_pick_result_ready = false;
	u32 m_pick_x = 0;
	u32 m_pick_y = 0;
	u32 m_pick_result = Renderer::invalid_entity_id();

	// The frame's surface texture, held from the blit until the ui_overlay
	// pass has drawn over it (or released straight away when there is no
	// overlay). The browser hands back the same texture for the whole frame.
	WGPUTexture m_frame_surface_texture = nullptr;
	WGPUTextureView m_frame_surface_view = nullptr;
	bool m_imgui_ready = false;
	double m_imgui_last_time_ms = 0.0;

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

	// What the lights accumulate into, and what the blit now puts on screen.
	// Separate from the gbuffer's Color, which stays the raw albedo the light
	// shaders sample.
	WGPUTexture m_scene_color = nullptr;
	WGPUTextureView m_scene_color_view = nullptr;

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

	// The lights are fullscreen quads over the gbuffer, blended additively, so
	// they share one vertex buffer and one set of texture bindings and differ
	// only in which shader runs.
	//
	// They sample the gbuffer 1:1, so they read it through a point sampler and
	// declare the bindings unfilterable - see create_light_resources() for why
	// that is worth a second sampler and a second group-0 layout.
	WGPUBuffer m_quad_vertices = nullptr;
	WGPUSampler m_point_sampler = nullptr;
	WGPUBindGroupLayout m_light_frame_layout = nullptr;
	WGPUBindGroup m_light_frame_bind_group = nullptr;
	WGPUBindGroupLayout m_light_texture_layout = nullptr;
	WGPUBindGroup m_light_bind_group = nullptr;
	WGPUPipelineLayout m_light_pipeline_layout = nullptr;
	WGPURenderPipeline m_directional_light_pipeline = nullptr;
	WGPURenderPipeline m_point_light_pipeline = nullptr;

	WGPURenderPipeline m_particle_additive_pipeline = nullptr;
	WGPURenderPipeline m_particle_alpha_pipeline = nullptr;

	// The cascades render into one atlas, four quadrants of it, as a colour
	// target rather than a depth one: the composite samples it as an ordinary
	// texture_2d<f32>, and WebGPU will not give a depth format that kind of
	// view. The depth texture beside it is only there to do the depth test.
	// Matches D3D12's g_shadow_tex_dimensions. Each cascade gets a quadrant, so
	// this is 2048 per cascade; dropping it to 2048 total visibly self-shadows
	// the floor in the far cascades, which is the depth slope across a texel
	// outrunning the shader's fixed 0.0001 bias.
	static constexpr u32 k_shadow_dimensions = 4096;
	WGPUTexture m_shadow_atlas = nullptr;
	WGPUTextureView m_shadow_atlas_view = nullptr;
	WGPUTexture m_shadow_depth = nullptr;
	WGPUTextureView m_shadow_depth_view = nullptr;
	WGPURenderPipeline m_shadow_static_pipeline = nullptr;
	WGPURenderPipeline m_shadow_skinned_pipeline = nullptr;

	// What the composite writes and the directional light reads as its shadow
	// mask - the gbuffer's fifth texture.
	WGPUTexture m_lighting = nullptr;
	WGPUTextureView m_lighting_view = nullptr;
	WGPUBindGroupLayout m_shadow_composite_layout = nullptr;
	WGPUBindGroup m_shadow_composite_bind_group = nullptr;
	WGPUPipelineLayout m_shadow_composite_pipeline_layout = nullptr;
	WGPURenderPipeline m_shadow_composite_pipeline = nullptr;

	// Filled by the cascade pass, read by the composite and by every light.
	Mat4 m_light_matrices[4];
	Vec4 m_cascade_distances;
	bool m_shadows_valid = false;

	// Gaussian splats. D3D12 sorts them back-to-front on a CPU thread
	// (splat_sort_thread() in gaussian_splat_dx12.cpp); that thread does not
	// exist here (wasm is single-threaded, no SharedArrayBuffer/COOP-COEP
	// deployment assumed), so this runs a GPU radix sort instead - ported from
	// black_splat's gaussian_splat_radix.wgsl into gaussian_splat_radix.hlsl,
	// the engine's own source of truth, compiled to WGSL the same way every
	// other shader is. Not the bitonic sort this engine used to have (O(n
	// log^2 n) dispatches); this is O(n) per digit, four 8-bit digits.
	// Verified against a CPU reference sort at N = 1, 137, 256, 151391 and
	// 262144 with zero mismatches before this was wired in - see
	// gaussian_splat_radix.hlsl's own header for the phase order.
	const GaussianSplatComponent* m_splat_component = nullptr;
	u32 m_splat_count = 0;
	WGPUBuffer m_splat_points = nullptr;
	WGPUBindGroupLayout m_splat_draw_layout = nullptr;
	WGPUBindGroup m_splat_draw_bind_group = nullptr;
	WGPURenderPipeline m_splat_draw_pipeline = nullptr;

	// Sort state. keys_a/vals_a hold the sorted result after an EVEN number of
	// digit passes (4, here), which is why the draw's g_sorted_indices binds
	// vals_a permanently rather than whichever buffer happened to finish last.
	static constexpr u32 k_splat_sort_wg = 256;
	static constexpr u32 k_splat_radix = 256;
	static constexpr u32 k_splat_radix_passes = 4;
	u32 m_splat_num_tiles = 0;
	u32 m_splat_alloc = 0;  // m_splat_num_tiles * k_splat_sort_wg
	WGPUBuffer m_splat_keys_a = nullptr;
	WGPUBuffer m_splat_keys_b = nullptr;
	WGPUBuffer m_splat_vals_a = nullptr;
	WGPUBuffer m_splat_vals_b = nullptr;
	WGPUBuffer m_splat_hist = nullptr;
	WGPUBuffer m_splat_block_sums = nullptr;
	WGPUBuffer m_splat_sort_globals = nullptr;
	WGPUBuffer m_splat_pass_info = nullptr;
	u32 m_splat_pass_stride = 0;

	WGPUBindGroupLayout m_splat_sort_group0_layout = nullptr;
	WGPUBindGroupLayout m_splat_sort_group1_layout = nullptr;
	WGPUPipelineLayout m_splat_sort_pipeline_layout = nullptr;
	// bg_a_to_b reads the A buffers and writes B; bg_b_to_a is the reverse.
	// cs_compute_keys always runs with bg_b_to_a (its "out" side is A), and the
	// four digit passes alternate starting from bg_a_to_b, so after an even
	// pass count the result lands back in A.
	WGPUBindGroup m_splat_sort_bg_a_to_b = nullptr;
	WGPUBindGroup m_splat_sort_bg_b_to_a = nullptr;
	WGPUBindGroup m_splat_sort_pass_bind_group = nullptr;

	WGPUComputePipeline m_splat_sort_compute_keys = nullptr;
	WGPUComputePipeline m_splat_sort_histogram = nullptr;
	WGPUComputePipeline m_splat_sort_scan_reduce = nullptr;
	WGPUComputePipeline m_splat_sort_scan_spine = nullptr;
	WGPUComputePipeline m_splat_sort_scan_add = nullptr;
	WGPUComputePipeline m_splat_sort_scatter = nullptr;

	// Sort order depends only on the camera's view DIRECTION (the first three
	// components of zc below), not on translation - shifting every splat's
	// depth equally never changes their relative order - so a resort only runs
	// when the camera rotates. NAN in [0] marks "never sorted".
	Vec4 m_splat_last_sort_zc = Vec4(NAN, 0.f, 0.f, 0.f);

	WGPUBindGroupLayout m_terrain_texture_layout = nullptr;
	WGPUPipelineLayout m_terrain_pipeline_layout = nullptr;
	WGPURenderPipeline m_terrain_pipeline = nullptr;
	// Keyed by the three texture ids, since a level can hold several terrains
	// and the group only has to be built once for each combination.
	std::unordered_map<u64, WGPUBindGroup> m_terrain_bind_groups;

	WGPURenderPipeline m_blit_pipeline = nullptr;
	WGPUBindGroupLayout m_blit_layout = nullptr;
	WGPUBindGroup m_blit_bind_group = nullptr;

	u32 m_frame_index = 0;
	u32 m_frame_draws = 0;
};
