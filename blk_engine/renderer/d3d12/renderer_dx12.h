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
	virtual void initialize_internal(HWND hwnd, const uint32_t frameWidth, const uint32_t frameHeight) override;

	virtual void shut_down_internal() override;

	virtual void render_gbuffer_internal() override;
	virtual void render_lights_internal() override;
	virtual void render_transluency_internal() override;
	virtual void render_shadows() override;
	virtual void render_point_clouds() override;

	virtual void present() override;

	virtual RenderPipeline* create_pipeline(const std::string& friendly_name, const std::string& path) override;
	virtual RenderBuffer* create_render_buffer_internal() override;

	virtual u32 load_texture(const std::string& path, LoadTextureParams& param) override;

	CD3DX12_VIEWPORT m_view_port;
	CD3DX12_RECT m_scissor_rect;

	ComPtr<ID3D12Device> m_device;
	ComPtr<ID3D12CommandQueue> m_queue;

	ComPtr<struct IDXGISwapChain3> m_swap_chain;
	uint32_t m_frame_index = 0;

	ComPtr<ID3D12CommandAllocator> m_command_allocator;
	ComPtr<ID3D12Resource> m_swap_chain_rtv[Renderer::max_frames()];
	ComPtr<ID3D12GraphicsCommandList> m_command_list;

	ComPtr<ID3D12Resource> m_depth_stencil_buffer[Renderer::max_frames()];
	ComPtr<ID3D12DescriptorHeap> m_depth_stencil_heap;

	// Render target
	ComPtr<ID3D12Resource> m_render_targets[ERenderTarget::Count][Renderer::max_frames()];
	ComPtr<ID3D12DescriptorHeap> m_depth_target_heap;
	uint32_t m_depth_target_descriptor_size = 0;

	ComPtr<ID3D12RootSignature> m_root_signature;

	ComPtr<ID3D12DescriptorHeap> m_rtv_heap;
	uint32_t m_rtv_descriptor_size = 0;

	ComPtr<ID3D12RootSignature> m_point_cloud_signature;
	ComPtr<ID3D12DescriptorHeap> m_point_cloud_descriptor_heap;
	ComPtr<ID3D12Resource> m_point_cloud_upload_heap;

	ComPtr<ID3D12DescriptorHeap> m_sampler_descriptor_heap;

	// Descriptors for scene instance constants, bone array constants, and shader resource view
	ComPtr<ID3D12DescriptorHeap> m_cbv_srv_descriptor_heap;

	// cbvs with corresponding descriptors in m_cbv_srv_descriptor_heap
	ComPtr<ID3D12Resource> m_scene_cbv_upload_heap;
	ComPtr<ID3D12Resource> m_bone_cbv_upload_heap;

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
	uint64_t m_fence_value = 0;
	HANDLE m_fence_event;
};

XMMATRIX& XMMATRIXFromMat4(Mat4& matrix);
Mat4& Mat4FromXMMATRIX(FXMMATRIX& matrix);

