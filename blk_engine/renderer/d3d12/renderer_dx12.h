/// Renderer_Dx12.h	
///
/// 2025 blk 1.0

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
///    into that SRV array: directional_shadow.shader's
///    gbuffer_textures[3]/[5] and directional_light.shader/
///    point_light.shader's g_buffer[0..4]/color_tex[0..3] assume exactly
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
	ShadowDepth,
	Count
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

	// Translates a batch of graph-derived transitions into D3D12 barriers.
	virtual void emit_barriers(const std::vector<GraphTransition>& transitions) override;

	virtual void present() override;

	virtual RenderPipeline* create_gpu_pipeline(const std::string& friendly_name, const std::string& path) override;
	virtual RenderPipeline* create_compute_pipeline(const std::string& friendly_name, const std::string& path) override;
	virtual RenderBuffer* create_render_buffer_internal() override;

	virtual u32 load_texture(const std::string& path, LoadTextureParams& param) override;

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

	// Fences
	ComPtr<ID3D12Fence> m_fence;
	u64 m_fence_value = 0;
	HANDLE m_fence_event;

	GaussianSplatComponent* m_gaussian_splat = nullptr;
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
	Vec4 pad[17];
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
	Vec4 pad[13];
};
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

	Vec4 pad[7];
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
	// packed here as 48 bytes. FIXME: gaussian_splat_draw.shader's
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
