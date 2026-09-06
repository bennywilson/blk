/// Renderer_Dx12.h	
///
/// 2025 blk

#pragma once

#include "d3dx12_core.h"
#include <dxcapi.h>
#include <DirectXMath.h>
#include <wrl/client.h>
#include "renderer.h"
#include "render_graph.h"

using namespace DirectX;
using Microsoft::WRL::ComPtr;

/// ERenderTarget
///
/// Two independent things depend on this enum, and they don't move together:
///
/// 1. SRV creation order -- determined by the order the resource-creation
///    blocks in initialize_internal's "Initialize GBuffers" loop *execute*,
///    NOT by these enum values. Several shaders hardcode literal indices
///    into that SRV array: directional_shadow.hlsl's
///    gbuffer_textures[3]/[5] and directional_light.hlsl/
///    point_light.hlsl's g_buffer[0..4]/color_tex[0..3] assume exactly
///    Color=SRV0, Normal=1, Specular=2, SceneDepth=3, Lighting=4,
///    ShadowDepth=5 in creation order. The SceneColor block runs after the
///    ShadowDepth block precisely to keep ShadowDepth at SRV5 despite
///    SceneColor sitting earlier in this enum -- see the SceneColor comment.
///
/// 2. RTV slot arithmetic -- `gbuffer_start + <ERenderTarget value>`
///    (render_shadow_composite, render_lights_internal, render_point_clouds)
///    uses the enum's raw integer value as an RTV heap offset. ShadowDepth
///    is a depth-stencil resource with no RTV, so it never participates in
///    this and can't be used as a stand-in for "how many RTV slots came
///    before me". Every *other* entry's enum value must equal its actual
///    RTV creation order (0-indexed among just the RTV-bearing entries),
///    which is why SceneColor sits at value 5 here even though its SRV is
///    created last (index 6) -- its RTV is still the 6th one created.
enum ERenderTarget {
	Color = 0,
	Normal,
	Specular,
	SceneDepth,
	Lighting,
	SceneColor,		// Full-screen lit output: lights/point-clouds/translucency
					// render into via `gbuffer_start + SceneColor` (constraint
					// 2 above) -- its SRV is still created last, after
					// ShadowDepth's (constraint 1), by physically ordering
					// its resource-creation block after the shadow block in
					// initialize_internal regardless of this enum position.
	EntityId,		// Phase 3: per-pixel entity id for viewport click-to-select,
					// written as a 5th gbuffer target by the three material
					// pipelines and read back one pixel at a time (see
					// request_entity_id_pick). Placed here for the same reason
					// SceneColor is: value 6 makes its RTV the 7th created
					// (constraint 2, ShadowDepth still contributing none),
					// while its creation block runs last of all so its SRV
					// lands at index 7 and the light/shadow shaders' hardcoded
					// 0..5 indices don't shift (constraint 1).
	ShadowDepth,
	Count
};

/// ImGuiDescriptorHeapAllocator
///
/// Minimal free-list allocator satisfying Dear ImGui's DX12 backend
/// descriptor callbacks (ImGui_ImplDX12_InitInfo::SrvDescriptorAllocFn/
/// FreeFn). Deliberately separate from m_cbv_srv_descriptor_heap's bindless
/// allocator (Phase 2) -- ImGui doesn't need to know about that heap's
/// shader-baked offset math, and vice versa. Kept ImGui-header-free so this
/// header doesn't pull ImGui into every translation unit that includes it.
struct ImGuiDescriptorHeapAllocator {
	ID3D12DescriptorHeap* heap = nullptr;
	u32 descriptor_size = 0;
	D3D12_CPU_DESCRIPTOR_HANDLE heap_start_cpu{};
	D3D12_GPU_DESCRIPTOR_HANDLE heap_start_gpu{};
	std::vector<int> free_indices;

	void create(ID3D12Device* device, ID3D12DescriptorHeap* descriptor_heap);
	void alloc(D3D12_CPU_DESCRIPTOR_HANDLE* out_cpu, D3D12_GPU_DESCRIPTOR_HANDLE* out_gpu);
	void free(D3D12_CPU_DESCRIPTOR_HANDLE cpu, D3D12_GPU_DESCRIPTOR_HANDLE gpu);
};

///	Renderer_Dx12
class Renderer_Dx12 : public Renderer {
public:
	~Renderer_Dx12();

	ComPtr<ID3D12Device> get_device() const { return m_device; }

	void wait_on_fence();

protected:
	void init_default_pipelines();
	std::vector<ComPtr<ID3D12Resource>> m_textures;

private:
	virtual void initialize_internal(HWND hwnd, const u32 frameWidth, const u32 frameHeight) override;
	virtual void shut_down_internal() override;

	virtual void add_render_component_internal(const RenderComponent* const);
	virtual void remove_render_component_internal(const RenderComponent* const);

	// Phase 3, Milestone 1: forwards to ImGui_ImplWin32_WndProcHandler -- see
	// Renderer::handle_platform_message(). This is the editor's entire ImGui
	// input path.
	virtual bool handle_platform_message_internal(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) override;

	void initialize_gaussian_splatting(const GaussianSplatComponent* const);
	void shutdown_gaussian_splatting();

	virtual void begin_frame_resources() override;
	virtual GraphResource* resolve_graph_resource(EFrameResource target) override;
	virtual RenderGraph::ExecuteFn get_pass_execute(const std::string& pass_name, const std::vector<ViewContext>& views, size_t view_index) override;

	// Registered as passes by get_pass_execute(); ordinary member functions
	// now, not base-class overrides (barriers moved out to the graph).
	// gbuffer/shadows/translucency also take an ERenderPassMask, replacing
	// what used to be a hardcoded "render_pass() != RP_X" check in each
	// function body.
	void render_gbuffer_internal(const RenderCamera& camera, const ERenderPassMask& render_pass_mask);
	void render_lights_internal(const RenderCamera& camera);
	void render_transluency_internal(const RenderCamera& camera, const ERenderPassMask& render_pass_mask);

	// Split into two graph passes so ShadowDepth can revert to Common
	// between them -- render_shadow_composite reads it via SRV.
	void render_shadow_cascades(const RenderCamera& camera, const ERenderPassMask& render_pass_mask);
	void render_shadow_composite(const RenderCamera& camera);

	void render_point_clouds(const RenderCamera& camera);

	// Placeholder composite (straight copy) from SceneColor to the back
	// buffer -- the seam for tonemap/bloom/color-grade/etc. once those exist.
	void render_post_process(const RenderCamera& camera);

	// Draws whatever ImGui panels are registered -- see get_pass_execute()'s
	// "ui_overlay" case.
	void render_ui_overlay();

	// Translates a batch of graph-derived transitions into D3D12 barriers.
	virtual void emit_barriers(const std::vector<GraphTransition>& transitions) override;

	// PIX/RenderDoc debug event markers -- ID3D12GraphicsCommandList::BeginEvent/
	// EndEvent need no PIX runtime dependency; both tools capture them directly.
	virtual void push_debug_marker(const char* const name) override;
	virtual void pop_debug_marker() override;

	virtual void present() override;

	// Viewport click-to-select -- see Renderer's declarations for the contract.
	// The copy is recorded at the tail of render_gbuffer_internal (the pass that
	// owns the EntityId target) and consumed in present() right after
	// wait_on_fence(), which makes the result available the same frame it was
	// requested: this renderer blocks on the GPU every frame, so nothing here
	// needs to straddle frames.
	virtual void request_entity_id_pick(const u32 backbuffer_x, const u32 backbuffer_y) override;
	virtual bool try_take_entity_id_pick(u32& out_entity_id) override;

	// Records the 1x1 EntityId -> readback-buffer copy when a pick is pending.
	void copy_entity_id_pick_pixel();

	// Maps the readback buffer and turns the pixel into m_pick_result.
	void resolve_entity_id_pick();

	virtual RenderPipeline* create_gpu_pipeline(const std::string& friendly_name, const std::string& path) override;
	virtual RenderPipeline* create_compute_pipeline(const std::string& friendly_name, const std::string& path) override;
	virtual RenderBuffer* create_render_buffer_internal() override;

	virtual u32 load_texture(const std::string& path, LoadTextureParams& param) override;

	ID3D12PipelineState* get_pipeline_state(const std::string& name);

	CD3DX12_VIEWPORT m_view_port;
	CD3DX12_RECT m_scissor_rect;

	ComPtr<ID3D12Device> m_device;
	ComPtr<ID3D12CommandQueue> m_queue;

	ComPtr<struct IDXGISwapChain3> m_swap_chain;
	u32 m_frame_index = 0;

	ComPtr<ID3D12CommandAllocator> m_command_allocator;
	ComPtr<ID3D12Resource> m_swap_chain_rtv[Renderer::max_frames()];
	ComPtr<ID3D12GraphicsCommandList> m_command_list;

	ComPtr<ID3D12Resource> m_depth_stencil_buffer[Renderer::max_frames()];
	ComPtr<ID3D12DescriptorHeap> m_depth_stencil_heap;

	// Render target
	ComPtr<ID3D12Resource> m_render_targets[ERenderTarget::Count][Renderer::max_frames()];

	// This frame's GraphResource for each ERenderTarget, refreshed in
	// begin_frame_resources() and handed out by resolve_graph_resource().
	GraphResource m_frame_graph_resources[ERenderTarget::Count];

	// Click-to-select readback. One row of D3D12_TEXTURE_DATA_PITCH_ALIGNMENT
	// (256) bytes is the smallest a CopyTextureRegion destination can be, even
	// for the single pixel actually wanted.
	ComPtr<ID3D12Resource> m_entity_id_readback_buffer;
	u32 m_pick_x = 0;
	u32 m_pick_y = 0;

	// Three states, not two, because of where in the frame each end sits:
	// the request is raised from the UI pass, which runs LAST, while the copy
	// happens in the gbuffer pass, which runs FIRST. So a request always waits
	// for the next frame's gbuffer, and only once that copy is recorded may
	// present() map the buffer -- resolving on m_pick_requested alone reads
	// bytes no copy this frame wrote.
	bool m_pick_requested = false;
	bool m_pick_copy_recorded = false;
	bool m_pick_result_ready = false;
	u32 m_pick_result = Renderer::invalid_entity_id();

	ComPtr<ID3D12DescriptorHeap> m_depth_target_heap;
	u32 m_depth_target_descriptor_size = 0;

	ComPtr<ID3D12RootSignature> m_root_signature;

	ComPtr<ID3D12DescriptorHeap> m_rtv_heap;
	u32 m_rtv_descriptor_size = 0;

	ComPtr<ID3D12RootSignature> m_point_cloud_signature;
	ComPtr<ID3D12DescriptorHeap> m_point_cloud_descriptor_heap;
	ComPtr<ID3D12Resource> m_point_cloud_upload_heap;
	ComPtr<ID3D12Resource> m_point_cloud_default_heap;

	ComPtr<ID3D12Resource> m_point_cloud_index_upload_heap;
	ComPtr<ID3D12Resource> m_point_cloud_index_default_heap;

	ComPtr<ID3D12DescriptorHeap> m_sampler_descriptor_heap;

	// Descriptors for scene instance constants, bone array constants, and shader resource view
	ComPtr<ID3D12DescriptorHeap> m_cbv_srv_descriptor_heap;

	// cbvs with corresponding descriptors in m_cbv_srv_descriptor_heap
	ComPtr<ID3D12Resource> m_scene_cbv_upload_heap;
	ComPtr<ID3D12Resource> m_bone_cbv_upload_heap;

	// GS Sort
	ComPtr<ID3D12RootSignature> m_gs_sort_signature;
	ComPtr<ID3D12DescriptorHeap> m_gs_sort_desc_heap;
	ComPtr<ID3D12Resource> m_gs_sort_buffer;
	ComPtr<ID3D12Resource> m_gs_sort_upload_buffer;

	// Compiler
	ComPtr<IDxcCompiler3> m_dxc_compiler;
	ComPtr<IDxcUtils> m_dxc_utils;
	ComPtr<IDxcIncludeHandler> m_dxc_include_handler;

	// Quad
	ComPtr<ID3D12Resource> m_quad_vb;
	D3D12_VERTEX_BUFFER_VIEW m_quad_vb_view;

	u32 m_frame_draws = 0;
	u32 m_bone_draws = 0;

	// Per-frame draw budgets. Both index fixed-size mapped upload heaps AND
	// fixed-size descriptor ranges declared in the root signature, so running
	// past either one corrupts the heap on the CPU side before the GPU-based
	// validation layer ever gets to complain about it.
	bool scene_slot_available();
	bool bone_slot_available();

	// Fences
	ComPtr<ID3D12Fence> m_fence;
	u64 m_fence_value = 0;
	HANDLE m_fence_event;

	GaussianSplatComponent* m_gaussian_splat = nullptr;

	// Dear ImGui. hwnd is retained here since ImGui_ImplWin32_Init() needs it
	// after initialize_internal returns.
	HWND m_hwnd = nullptr;
	ComPtr<ID3D12DescriptorHeap> m_imgui_srv_heap;
	ImGuiDescriptorHeapAllocator m_imgui_srv_heap_allocator;
};

XMMATRIX& XMMATRIXFromMat4(Mat4& matrix);
Mat4& Mat4FromXMMATRIX(FXMMATRIX& matrix);


// Scene Config
extern const u32 g_max_scene_constants;
extern const u32 g_max_scene_bone_arrays;
extern const u32 g_max_scene_srvs;

extern const u64 g_max_point_cloud_points;

extern const u32 g_bone_array_descriptor_start;
extern const u32 g_srv_descriptor_start;

extern const f32 g_near_clip_plane;
extern const f32 g_far_clip_plane;
extern const f32 g_fov;


extern const bool g_high_performance_adapter ;

extern const u32 g_shadow_tex_dimensions;

extern CD3DX12_HEAP_PROPERTIES g_D3D12_HEAP_TYPE_UPLOAD;
extern CD3DX12_HEAP_PROPERTIES g_D3D12_HEAP_TYPE_DEFAULT;

/// GlobalUniformData
struct GlobalUniformData {
	Mat4 view;
	Mat4 view_projection;
	Mat4 inv_view_proj;
	Vec4 camera_pos;
	Vec4 splat_params;
	Vec4 splat_params_2;
	// .x = g_srv_descriptor_start: the absolute SRV slot (in the shared
	// CBV/SRV/UAV-type heap) where material textures begin. Material shaders
	// add this to their (still relative) texture_list id to get the absolute
	// ResourceDescriptorHeap[] index -- see the bindless SRV conversion in
	// Renderer_Dx12.
	Vec4 srv_heap_base;
	Vec4 pad[16];
};
extern GlobalUniformData* g_global_uniform;

/// SceneInstanceData
struct SceneInstanceData {
	Mat4 mvp;
	Mat4 world;
	Mat4 inv_world;
	Vec4 color;
	Vec4 spec;
	Vec4 time_since_spawn;
	f32 texture_list[16];

	// Phase 3 (click-to-select): .x is the owning entity's GetEntityId(), which
	// the three gbuffer material shaders write straight out to
	// ERenderTarget::EntityId. It must sit IMMEDIATELY after texture_list, at
	// offset 304, taking the first Vec4 of the old pad[13].
	//
	// The shaders reach this through `(SceneData)base_instance`, where BaseData
	// is a homogeneous `matrix pad0[8]`. That cast assigns element-wise down the
	// flattened scalar stream -- it is NOT a byte-level reinterpret and HLSL's
	// cbuffer packing rules never enter into it. So SceneData's members line up
	// with this tightly-packed C++ layout one scalar at a time, which is why
	// `float texture_list[16]` matches f32[16] here rather than burning a
	// 16-byte register per element. Confirmed against live behaviour: terrain
	// samples texture_list[4] and texture_list[1] and renders its splat map
	// correctly, which only holds under element-wise flattening.
	Vec4 entity_id;

	Vec4 pad[12];
};
// 512 bytes total, matching BaseData's `matrix pad0[8]` in the material
// shaders. entity_id's offset is load-bearing, not incidental: the shaders'
// element-wise (SceneData) cast reads it positionally, so a field inserted
// above it would silently shift the picking id into a neighbouring member
// rather than fail to compile.
static_assert(sizeof(SceneInstanceData) == 512, "SceneInstanceData must stay 512 bytes to match BaseData in the material shaders");
static_assert(offsetof(SceneInstanceData, entity_id) == 304, "entity_id must stay at offset 304 -- see the comment on the field");
extern SceneInstanceData* g_scene_buffers;

/// LightInstanceData
struct LightInstanceData {
	Vec4 position;
	Vec4 direction;
	Vec4 color;
	Mat4 light_matrices[4];
	Vec4 cascade_distances;

	// todo: duplicated from GlobalUniformData until Global Constants are reworked
	Mat4 player_inv_view_proj;
	Vec4 player_camera_position;

	// .x = gbuffer_srv_start (g_srv_descriptor_start + ERenderTarget::Count *
	// m_frame_index): the absolute heap slot of this frame's gbuffer SRV
	// block. Light/shadow-composite shaders add the fixed 0..5 gbuffer
	// texture offset to this to get the absolute ResourceDescriptorHeap[]
	// index -- see the bindless SRV conversion in Renderer_Dx12.
	Vec4 gbuffer_srv_base;

	Vec4 pad[6];
};

/// BoneInstanceData
struct BoneInstanceData {
	Mat4 bones[128];
};
extern BoneInstanceData* g_bone_array_buffers;
extern SceneInstanceData* g_scene_buffers;

/// PointCloudSampleInstance
#include <DirectXPackedVector.h> // Needed for the conversion function

struct PointCloudSampleInstance {
	Vec4 position;          // 16 bytes
	Vec4 scale3d_opacity;   // 16 bytes
	Quat4 rotation;         // 16 bytes
	Vec4 sh0;               // 16 bytes (Keep DC as f32 for accurate base color)

	// 8 remaining SH coefficients * 3 color channels = 24 halfs, tightly
	// packed here as 48 bytes. FIXME: gaussian_splat_draw.hlsl's
	// SplatPoint.f_rest reads this at a 4-byte stride (compiles at SM6.0,
	// where `half` is just `float` -- no true 16-bit packing), so it
	// currently reads two adjacent packed half values' raw bits as one
	// garbage float per f_rest[k] for k<12, and pure padding for k>=12. See
	// the FIXME on SplatPoint in that shader for the full explanation.
	uint16_t sh_rest[24];   // 48 bytes

	// The 48 bytes below aren't slack for future fields -- they're required
	// padding. The shader's SplatPoint struct is really 160 bytes (see the
	// comment on it), not the 112 its own field-by-field math suggests,
	// because `half f_rest[24]` compiles as `float[24]` at this shader's
	// SM6.0 profile. This struct -- and the StructureByteStride passed when
	// creating the splat SRV (renderer_dx12.cpp) -- must match that real
	// 160, confirmed via a RenderDoc capture after a padding-removal
	// experiment silently broke splat rendering. 64 + 48 = 112; 48 more to
	// reach 160.
	uint8_t padding[48];
};

static_assert(sizeof(PointCloudSampleInstance) == 160, "Struct size must be 160 bytes for alignment");

extern PointCloudSampleInstance* g_point_cloud;
extern BoneInstanceData* g_bone_array_buffers;
extern SceneInstanceData* g_scene_buffers;
extern u32* g_point_cloud_indices;

static_assert(
	sizeof(SceneInstanceData) == sizeof(GlobalUniformData) &&
	sizeof(SceneInstanceData) == sizeof(LightInstanceData)
);
