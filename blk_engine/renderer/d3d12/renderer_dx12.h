/// Renderer_Dx12.h	
///
/// 2025 blk 1.0

#pragma once

#include "d3dx12_core.h"
#include <dxcapi.h>
#include <DirectXMath.h>
#include <wrl/client.h>
#include "renderer.h"

using namespace DirectX;
using Microsoft::WRL::ComPtr;

/// ERenderTarget
enum ERenderTarget {
	Color = 0,
	Normal,
	Specular,
	SceneDepth,
	Lighting,
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

	virtual void render_gbuffer_internal() override;
	virtual void render_lights_internal() override;
	virtual void render_transluency_internal() override;
	virtual void render_shadows() override;
	virtual void render_point_clouds() override;

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

extern const u32 g_max_point_cloud_points;

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
struct PointCloudSampleInstance {
	Vec4 position;
	Vec4 scale3d_opacity;
	Quat4 rotation;
	Vec4 sh0;
	Vec4 sh1;
	Vec4 sh2;
	Vec4 sh3;
	Vec4 sh4;
	Vec4 sh5;
	Vec4 sh6;
	Vec4 sh7;
	Vec4 sh8;
	Vec4 pad[20];
};
extern PointCloudSampleInstance* g_point_cloud;
extern BoneInstanceData* g_bone_array_buffers;
extern SceneInstanceData* g_scene_buffers;
extern u32* g_point_cloud_indices;

static_assert(
	sizeof(SceneInstanceData) == sizeof(GlobalUniformData) &&
	sizeof(SceneInstanceData) == sizeof(PointCloudSampleInstance) &&
	sizeof(SceneInstanceData) == sizeof(LightInstanceData)
);