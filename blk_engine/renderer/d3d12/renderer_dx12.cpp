/// Renderer_Dx12.cpp
///
/// 2025 blk 1.0

#include <chrono>
#include <d3d12sdklayers.h>
#include "blk_core.h"
#include "blk_containers.h"
#include "entity_header.h"
#include "renderer_dx12.h"
#include <dxgi1_6.h>
#include <d3dcompiler.h>
#include "d3dx12.h"
#include "DDSTextureLoader12.h"
#include "d3d12_defs.h"
#include "render_component.h"
#include "Plane3d.h"

using namespace std;

static const u32 g_max_scene_constants = 1024;
static const u32 g_max_scene_srvs = 1024;
static const u32 g_shadow_tex_dimensions = 8192;
static const f32 g_near_clip_plane = 1.f;
static const f32 g_far_clip_plane = 20000.f;
static const f32 g_fov = kbToRadians(75.f);
std::vector<Mat4> light_matrices;

// Todo...
XMMATRIX& XMMATRIXFromMat4(Mat4& matrix) { return (*(XMMATRIX*)&matrix); }
Mat4& Mat4FromXMMATRIX(FXMMATRIX& matrix) { return (*(Mat4*)&matrix); }


/// GlobalUniformData
struct GlobalUniformData {
	Mat4 view_projection;
	Mat4 inv_view_proj;
	Vec4 camera;
	Vec4 pad[247];
}*g_global_uniform;

/// SceneInstanceData
struct SceneInstanceData {
	Mat4 mvp;
	Mat4 world;
	Vec4 color;
	Vec4 spec;
	Vec4 time_since_spawn;
	Vec4 pad[245];
}*g_scene_buffers;

/// LightInstanceData
struct LightInstanceData {
	Mat4 light_matrices[4];
	Vec4 position;
	Vec4 direction;
	Vec4 color;
	Vec4 pad[237];
};

/// BoneInstanceData
struct BoneInstanceData {
	Mat4 bones[64];
};

static_assert(
	sizeof(SceneInstanceData) == sizeof(GlobalUniformData) &&
	sizeof(SceneInstanceData) == sizeof(LightInstanceData) &&
	sizeof(SceneInstanceData) == sizeof(BoneInstanceData)
);

/// Renderer_Dx12::~Renderer_Dx12
Renderer_Dx12::~Renderer_Dx12() {
	shut_down();	// function is virtual but called in ~Renderer which is UB
}

/// Renderer_Dx12::initialize_internal
void Renderer_Dx12::initialize_internal(HWND hwnd, const uint32_t frame_width, const uint32_t frame_height) {
	UINT dxgiFactoryFlags = 0;

	m_view_port = CD3DX12_VIEWPORT(0.f, 0.f, (float)frame_width, (float)frame_height);
	m_scissor_rect = CD3DX12_RECT(0, 0, frame_width, frame_height);

#if defined(_DEBUG)
	ComPtr<ID3D12Debug> debugController;
	if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController)))) {
		debugController->EnableDebugLayer();
		dxgiFactoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
	}
	ComPtr<ID3D12Debug1> spDebugController1;
	debugController->QueryInterface(IID_PPV_ARGS(&spDebugController1));
	spDebugController1->SetEnableGPUBasedValidation(true);
#endif
	ComPtr<IDXGIFactory4> factory;
	CreateDXGIFactory2(dxgiFactoryFlags, IID_PPV_ARGS(&factory));

	ComPtr<IDXGIAdapter1> hw_adapter;
	get_hardware_adapter(factory.Get(), &hw_adapter, true);

	// Device
	blk::error_check(D3D12CreateDevice(
		hw_adapter.Get(),
		D3D_FEATURE_LEVEL_11_0,
		IID_PPV_ARGS(&m_device)
	));

	// Queue
	D3D12_COMMAND_QUEUE_DESC queue_desc = {};
	queue_desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
	queue_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
	blk::error_check(m_device->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&m_queue)));

	// Swap Chain
	DXGI_SWAP_CHAIN_DESC1 swap_chain_desc = {};
	swap_chain_desc.BufferCount = Renderer::max_frames();
	swap_chain_desc.Width = m_frame_width;
	swap_chain_desc.Height = m_frame_height;
	swap_chain_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	swap_chain_desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	swap_chain_desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
	swap_chain_desc.SampleDesc.Count = 1;

	ComPtr<IDXGISwapChain1> swap_chain;
	blk::error_check(factory->CreateSwapChainForHwnd(
		m_queue.Get(),        // Swap chain needs the queue so that it can force a flush on it.
		hwnd,
		&swap_chain_desc, nullptr,
		nullptr,
		&swap_chain
	));
	blk::error_check(swap_chain.As(&m_swap_chain));
	m_frame_index = m_swap_chain->GetCurrentBackBufferIndex();

	// Disable fullscreen
	blk::error_check(factory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER));

	// RTV descriptor heap
	D3D12_DESCRIPTOR_HEAP_DESC rtv_heap_desc = {};
	rtv_heap_desc.NumDescriptors = Renderer::max_frames() + ERenderTarget::Count;
	rtv_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
	rtv_heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	blk::error_check(m_device->CreateDescriptorHeap(&rtv_heap_desc, IID_PPV_ARGS(&m_rtv_heap)));
	m_rtv_descriptor_size = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

	// SRV descriptor heap
	D3D12_DESCRIPTOR_HEAP_DESC srv_heap_desc = {};
	srv_heap_desc.NumDescriptors = g_max_scene_constants + g_max_scene_srvs;
	srv_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	srv_heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	blk::error_check(m_device->CreateDescriptorHeap(&srv_heap_desc, IID_PPV_ARGS(&m_cbv_srv_heap)));
	m_cbv_srv_heap->SetName(L"Renderer_Dx12::m_cbv_srv_heap");

	// Depth target descriptor heap
	D3D12_DESCRIPTOR_HEAP_DESC depth_heap_desc = {};
	depth_heap_desc.NumDescriptors = 2;
	depth_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
	depth_heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	blk::error_check(m_device->CreateDescriptorHeap(&depth_heap_desc, IID_PPV_ARGS(&m_depth_stencil_heap)));
	m_depth_target_descriptor_size = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);

	// Sampler heap
	D3D12_DESCRIPTOR_HEAP_DESC sampler_heap_desc = {};
	sampler_heap_desc.NumDescriptors = 1;
	sampler_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
	sampler_heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	blk::error_check(m_device->CreateDescriptorHeap(&sampler_heap_desc, IID_PPV_ARGS(&m_sampler_heap)));
	m_sampler_heap->SetName(L"Renderer_Dx12::m_sampler_heap");

	// Sampler
	D3D12_SAMPLER_DESC sampler_desc = {};
	sampler_desc.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
	sampler_desc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	sampler_desc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	sampler_desc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	sampler_desc.MinLOD = 0;
	sampler_desc.MaxLOD = D3D12_FLOAT32_MAX;
	sampler_desc.MipLODBias = 0.0f;
	sampler_desc.MaxAnisotropy = 1;
	sampler_desc.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
	m_device->CreateSampler(&sampler_desc, m_sampler_heap->GetCPUDescriptorHandleForHeapStart());

	// Constants
	const auto CBV_SRV_DESCRIPTOR_SIZE = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	// cbv upload heap
	const auto cbv_heap_props = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
	const auto cbv_buffer_size = CD3DX12_RESOURCE_DESC::Buffer(g_max_scene_constants * sizeof(SceneInstanceData));
	blk::error_check(m_device->CreateCommittedResource(
		&cbv_heap_props,
		D3D12_HEAP_FLAG_NONE,
		&cbv_buffer_size,
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(&m_cbv_upload_heap)));

	CD3DX12_RANGE readRange(0, 0);        // We do not intend to read from this resource on the CPU.
	g_scene_buffers = nullptr;
	blk::error_check(m_cbv_upload_heap->Map(0, &readRange, reinterpret_cast<void**>(&g_scene_buffers)));
	g_global_uniform = (GlobalUniformData*)g_scene_buffers;

	// Constant buffer view
	UINT64 cb_offset = 0;
	CD3DX12_CPU_DESCRIPTOR_HANDLE cbvSrvHandle(m_cbv_srv_heap->GetCPUDescriptorHandleForHeapStart(), 0, CBV_SRV_DESCRIPTOR_SIZE);    // Move past the SRVs.

	for (u32 i = 0; i < g_max_scene_srvs; i++) {
		D3D12_CONSTANT_BUFFER_VIEW_DESC cbv_desc = {};
		cbv_desc.BufferLocation = m_cbv_upload_heap->GetGPUVirtualAddress() + cb_offset;
		cbv_desc.SizeInBytes = sizeof(SceneInstanceData);
		cb_offset += cbv_desc.SizeInBytes;

		m_device->CreateConstantBufferView(&cbv_desc, cbvSrvHandle);
		cbvSrvHandle.Offset(CBV_SRV_DESCRIPTOR_SIZE);
	}

	// Frame resources
	CD3DX12_CPU_DESCRIPTOR_HANDLE rtv_handle(m_rtv_heap->GetCPUDescriptorHandleForHeapStart());
	CD3DX12_CPU_DESCRIPTOR_HANDLE depth_target_handle(m_depth_stencil_heap->GetCPUDescriptorHandleForHeapStart());

	// Depth Stencil Buffer
	{
		CD3DX12_DEPTH_STENCIL_DESC depth_stencil_desc = {};
		depth_stencil_desc.DepthEnable = true;
		depth_stencil_desc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
		depth_stencil_desc.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
		depth_stencil_desc.StencilEnable = false;
		depth_stencil_desc.StencilReadMask = D3D12_DEFAULT_STENCIL_READ_MASK;
		depth_stencil_desc.StencilWriteMask = D3D12_DEFAULT_STENCIL_WRITE_MASK;

		const D3D12_DEPTH_STENCILOP_DESC defaultStencilOp = {
			D3D12_STENCIL_OP_KEEP, D3D12_STENCIL_OP_KEEP, D3D12_STENCIL_OP_KEEP, D3D12_COMPARISON_FUNC_ALWAYS
		};
		depth_stencil_desc.FrontFace = defaultStencilOp;
		depth_stencil_desc.BackFace = defaultStencilOp;

		// create a depth stencil descriptor heap so we can get a pointer to the depth stencil buffer
		/*D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc = {};
		dsvHeapDesc.NumDescriptors = 1;
		dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
		dsvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
		blk::error_check(m_device->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(&m_depth_stencil_heap)));*/

		D3D12_DEPTH_STENCIL_VIEW_DESC depthStencilDesc = {};
		depthStencilDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
		depthStencilDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
		depthStencilDesc.Flags = D3D12_DSV_FLAG_NONE;

		D3D12_CLEAR_VALUE depthOptimizedClearValue = {};
		depthOptimizedClearValue.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
		depthOptimizedClearValue.DepthStencil.Depth = 1.0f;
		depthOptimizedClearValue.DepthStencil.Stencil = 0;

		auto ds_heap_prop = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
		auto resource_desc = CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_D24_UNORM_S8_UINT, m_frame_width, m_frame_height, 1, 0, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL);
		m_device->CreateCommittedResource(
			&ds_heap_prop,
			D3D12_HEAP_FLAG_NONE,
			&resource_desc,
			D3D12_RESOURCE_STATE_DEPTH_WRITE,
			&depthOptimizedClearValue,
			IID_PPV_ARGS(&m_depth_stencil_buffer)
		);
		m_depth_stencil_heap->SetName(L"Depth/Stencil Resource Heap");
		m_device->CreateDepthStencilView(m_depth_stencil_buffer.Get(), &depthStencilDesc, depth_target_handle);
		depth_target_handle.Offset(1, m_depth_target_descriptor_size);
	}

	// Create a RTV for each frame.
	for (uint32_t i = 0; i < Renderer::max_frames(); i++) {
		blk::error_check(m_swap_chain->GetBuffer(i, IID_PPV_ARGS(&m_swap_chain_rtv[i])));
		m_device->CreateRenderTargetView(m_swap_chain_rtv[i].Get(), nullptr, rtv_handle);
		rtv_handle.Offset(1, m_rtv_descriptor_size);
	}

	// Render Targets
	{
		// Color
		{
			const auto format = DXGI_FORMAT_R8G8B8A8_UNORM;
			D3D12_RESOURCE_DESC desc = CD3DX12_RESOURCE_DESC::Tex2D(format,
				(u64)m_frame_width,
				(u32)m_frame_height,
				1, 1, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET);
			D3D12_CLEAR_VALUE clearValue = { format, {0.f, 0.f, 0.f, 0.f} };

			// Color
			const auto heap_properties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
			blk::error_check(
				m_device->CreateCommittedResource(
					&heap_properties, D3D12_HEAP_FLAG_ALLOW_ALL_BUFFERS_AND_TEXTURES,
					&desc,
					D3D12_RESOURCE_STATE_RENDER_TARGET,
					&clearValue,
					IID_PPV_ARGS(m_render_targets[ERenderTarget::Color].ReleaseAndGetAddressOf()
					)
				)
			);

			m_device->CreateRenderTargetView(m_render_targets[ERenderTarget::Color].Get(), nullptr, rtv_handle);
			rtv_handle.Offset(1, m_rtv_descriptor_size);

			m_device->CreateShaderResourceView(m_render_targets[ERenderTarget::Color].Get(), nullptr, cbvSrvHandle);
			cbvSrvHandle.Offset(CBV_SRV_DESCRIPTOR_SIZE);
		}

		// Normal
		{
			const auto format = DXGI_FORMAT_R8G8B8A8_UNORM;
			D3D12_RESOURCE_DESC desc = CD3DX12_RESOURCE_DESC::Tex2D(format,
				(u64)m_frame_width,
				(u32)m_frame_height,
				1, 1, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET);
			D3D12_CLEAR_VALUE clearValue = { format, {0.f, 0.f, 0.f, 0.f} };

			const auto heap_properties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
			blk::error_check(
				m_device->CreateCommittedResource(
					&heap_properties, D3D12_HEAP_FLAG_ALLOW_ALL_BUFFERS_AND_TEXTURES,
					&desc,
					D3D12_RESOURCE_STATE_RENDER_TARGET,
					&clearValue,
					IID_PPV_ARGS(m_render_targets[ERenderTarget::Normal].ReleaseAndGetAddressOf()
					)
				)
			);

			m_device->CreateRenderTargetView(m_render_targets[ERenderTarget::Normal].Get(), nullptr, rtv_handle);
			rtv_handle.Offset(1, m_rtv_descriptor_size);

			m_device->CreateShaderResourceView(m_render_targets[ERenderTarget::Normal].Get(), nullptr, cbvSrvHandle);
			cbvSrvHandle.Offset(CBV_SRV_DESCRIPTOR_SIZE);
		}

		// Buff 2
		{
			const auto format = DXGI_FORMAT_R8G8B8A8_UNORM;
			D3D12_RESOURCE_DESC desc = CD3DX12_RESOURCE_DESC::Tex2D(format,
				(u64)m_frame_width,
				(u32)m_frame_height,
				1, 1, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET);
			D3D12_CLEAR_VALUE clearValue = { format, {0.f, 0.f, 0.f, 0.f} };

			// Color
			const auto heap_properties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
			blk::error_check(
				m_device->CreateCommittedResource(
					&heap_properties, D3D12_HEAP_FLAG_ALLOW_ALL_BUFFERS_AND_TEXTURES,
					&desc,
					D3D12_RESOURCE_STATE_RENDER_TARGET,
					&clearValue,
					IID_PPV_ARGS(m_render_targets[ERenderTarget::Specular].ReleaseAndGetAddressOf()
					)
				)
			);

			m_device->CreateRenderTargetView(m_render_targets[ERenderTarget::Specular].Get(), nullptr, rtv_handle);
			rtv_handle.Offset(1, m_rtv_descriptor_size);

			m_device->CreateShaderResourceView(m_render_targets[ERenderTarget::Specular].Get(), nullptr, cbvSrvHandle);
			cbvSrvHandle.Offset(CBV_SRV_DESCRIPTOR_SIZE);
		}

		// Buff 1
		{
			const auto format = DXGI_FORMAT_R32_FLOAT;
			D3D12_RESOURCE_DESC desc = CD3DX12_RESOURCE_DESC::Tex2D(format,
				(u64)m_frame_width,
				(u32)m_frame_height,
				1, 1, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET);
			D3D12_CLEAR_VALUE clearValue = { format, {0.f, 0.f, 0.f, 0.f} };

			// Color
			const auto heap_properties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
			blk::error_check(
				m_device->CreateCommittedResource(
					&heap_properties, D3D12_HEAP_FLAG_ALLOW_ALL_BUFFERS_AND_TEXTURES,
					&desc,
					D3D12_RESOURCE_STATE_RENDER_TARGET,
					&clearValue,
					IID_PPV_ARGS(m_render_targets[ERenderTarget::Depth].ReleaseAndGetAddressOf()
					)
				)
			);

			m_device->CreateRenderTargetView(m_render_targets[ERenderTarget::Depth].Get(), nullptr, rtv_handle);
			rtv_handle.Offset(1, m_rtv_descriptor_size);

			m_device->CreateShaderResourceView(m_render_targets[ERenderTarget::Depth].Get(), nullptr, cbvSrvHandle);
			cbvSrvHandle.Offset(CBV_SRV_DESCRIPTOR_SIZE);
		}

		// Lighting
		{
			const auto format = DXGI_FORMAT_R8G8B8A8_UNORM;
			D3D12_RESOURCE_DESC desc = CD3DX12_RESOURCE_DESC::Tex2D(format,
				(u64)m_frame_width,
				(u32)m_frame_height,
				1, 1, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET);
			D3D12_CLEAR_VALUE clearValue = { format, {0.f, 0.f, 0.f, 0.f} };

			// Color
			const auto heap_properties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
			blk::error_check(
				m_device->CreateCommittedResource(
					&heap_properties, D3D12_HEAP_FLAG_ALLOW_ALL_BUFFERS_AND_TEXTURES,
					&desc,
					D3D12_RESOURCE_STATE_RENDER_TARGET,
					&clearValue,
					IID_PPV_ARGS(m_render_targets[ERenderTarget::Lighting].ReleaseAndGetAddressOf())
				)
			);

			m_device->CreateRenderTargetView(m_render_targets[ERenderTarget::Lighting].Get(), nullptr, rtv_handle);
			rtv_handle.Offset(1, m_rtv_descriptor_size);

			m_device->CreateShaderResourceView(m_render_targets[ERenderTarget::Lighting].Get(), nullptr, cbvSrvHandle);
			cbvSrvHandle.Offset(CBV_SRV_DESCRIPTOR_SIZE);
		}

		// SHADOW
		{
			D3D12_DEPTH_STENCIL_VIEW_DESC depthStencilDesc = {};
			depthStencilDesc.Format = DXGI_FORMAT_D32_FLOAT;
			depthStencilDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
			depthStencilDesc.Flags = D3D12_DSV_FLAG_NONE;

			D3D12_CLEAR_VALUE depthOptimizedClearValue = {};
			depthOptimizedClearValue.Format = DXGI_FORMAT_D32_FLOAT;
			depthOptimizedClearValue.DepthStencil.Depth = 1.0f;
			depthOptimizedClearValue.DepthStencil.Stencil = 0;

			auto ds_heap_prop = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
			auto resource_desc = CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_D32_FLOAT, g_shadow_tex_dimensions, g_shadow_tex_dimensions, 1, 0, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL);
			m_device->CreateCommittedResource(
				&ds_heap_prop,
				D3D12_HEAP_FLAG_NONE,
				&resource_desc,
				D3D12_RESOURCE_STATE_DEPTH_WRITE,
				&depthOptimizedClearValue,
				IID_PPV_ARGS(m_render_targets[ERenderTarget::ShadowDepth].ReleaseAndGetAddressOf()));

			m_device->CreateDepthStencilView(m_render_targets[ERenderTarget::ShadowDepth].Get(), &depthStencilDesc, depth_target_handle);
			depth_target_handle.Offset(1, m_depth_target_descriptor_size);

			D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc = {};
			srv_desc.Format = DXGI_FORMAT_R32_FLOAT;
			srv_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
			srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
			srv_desc.Texture2D.MipLevels = 1;
			m_device->CreateShaderResourceView(m_render_targets[ERenderTarget::ShadowDepth].Get(), &srv_desc, cbvSrvHandle);
			cbvSrvHandle.Offset(CBV_SRV_DESCRIPTOR_SIZE);
		}
	}

	// Quad
	{
		struct QuadVert {
			QuadVert(Vec3 p, Vec2 _uv) :
				pos(p),
				uv(_uv) {}
			Vec3 pos;
			Vec2 uv;
		};
		const QuadVert verts[] = {
			QuadVert(Vec3(-1.f, 1.f, 0.0f), Vec2(0.0f, 0.0f)),
			QuadVert(Vec3(1.f, 1.f, 0.0f), Vec2(1.0f, 0.0f)),
			QuadVert(Vec3(1.f, -1.f, 0.0f), Vec2(1.0f, 1.0f)),

			QuadVert(Vec3(-1.f, 1.f, 0.0f), Vec2(0.0f, 0.0f)),
			QuadVert(Vec3(1.f, -1.f, 0.0f), Vec2(1.0f, 1.0f)),
			QuadVert(Vec3(-1.f, -1.f, 0.0f), Vec2(0.0f, 1.0f)),
		};
		const UINT vertexBufferSize = sizeof(verts);


		auto upload_heap_props = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
		auto vb_size_desc = CD3DX12_RESOURCE_DESC::Buffer(vertexBufferSize);
		blk::error_check(m_device->CreateCommittedResource(
			&upload_heap_props,
			D3D12_HEAP_FLAG_NONE,
			&vb_size_desc,
			D3D12_RESOURCE_STATE_GENERIC_READ,
			nullptr,
			IID_PPV_ARGS(&m_quad_vb)));

		// Copy the triangle data to the vertex buffer.
		u8* vertex_data_ptr = nullptr;
		CD3DX12_RANGE readRange(0, 0);        // We do not intend to read from this resource on the CPU.
		blk::error_check(m_quad_vb->Map(0, &readRange, reinterpret_cast<void**>(&vertex_data_ptr)));
		memcpy(vertex_data_ptr, verts, sizeof(verts));
		m_quad_vb->Unmap(0, nullptr);

		// Initialize the vertex buffer view.
		m_quad_vb_view.BufferLocation = m_quad_vb->GetGPUVirtualAddress();
		m_quad_vb_view.StrideInBytes = sizeof(QuadVert);
		m_quad_vb_view.SizeInBytes = vertexBufferSize;
	}

	blk::error_check(m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_command_allocator)));
	blk::error_check(m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_command_allocator.Get(), nullptr, IID_PPV_ARGS(&m_command_list)));
	m_command_list->Close();

	// The root signature determines what kind of data the shader should expect.
	CD3DX12_DESCRIPTOR_RANGE1 ranges[4] = {};
	ranges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, g_max_scene_srvs, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC);
	ranges[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER, 1, 0);
	ranges[2].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, g_max_scene_constants, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC);
	ranges[3].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 4, 0, 1, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC);

	// Root parameters are entries in the root signature
	CD3DX12_ROOT_PARAMETER1 root_parameters[5] = {};
	root_parameters[0].InitAsDescriptorTable(1, &ranges[0], D3D12_SHADER_VISIBILITY_ALL);		// scene_constants
	root_parameters[1].InitAsDescriptorTable(1, &ranges[1], D3D12_SHADER_VISIBILITY_PIXEL);		// sampler
	root_parameters[2].InitAsDescriptorTable(1, &ranges[2], D3D12_SHADER_VISIBILITY_PIXEL);		// srv
	root_parameters[3].InitAsConstants(1, 0, 1, D3D12_SHADER_VISIBILITY_ALL);					// scene_indices
	root_parameters[4].InitAsDescriptorTable(1, &ranges[3], D3D12_SHADER_VISIBILITY_PIXEL);		// srv

	const D3D12_ROOT_SIGNATURE_FLAGS signature_flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
		D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
		D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
		D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS;

	CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC root_signature_desc = {};
	root_signature_desc.Init_1_1(_countof(root_parameters), root_parameters, 0, nullptr, signature_flags);

	ComPtr<ID3DBlob> signature;
	ComPtr<ID3DBlob> error;
	if (!blk::warn_check(D3DX12SerializeVersionedRootSignature(&root_signature_desc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &error))) {
		blk::error("%s", error->GetBufferPointer());
	}
	blk::error_check(m_device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&m_root_signature)));

	// Fences	
	blk::error_check(m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence)));
	m_fence_value = 1;

	// Create an event handle to use for frame synchronization.
	m_fence_event = CreateEvent(nullptr, FALSE, FALSE, nullptr);
	if (m_fence_event == nullptr) {
		blk::error_check(HRESULT_FROM_WIN32(GetLastError()));
	}

	todo_create_texture();

	blk::log("Renderer_Dx12 initialized");
}

/// Renderer_Dx12::shut_down_internal
void Renderer_Dx12::shut_down_internal() {
	wait_on_fence();

	m_cbv_upload_heap->Unmap(0, nullptr);

	m_root_signature.Reset();
	m_cbv_upload_heap.Reset();
	m_cbv_srv_heap.Reset();
	m_sampler_heap.Reset();
	m_rtv_heap.Reset();
	m_depth_stencil_buffer.Reset();
	m_depth_stencil_heap.Reset();

	m_command_allocator.Reset();
	m_command_list.Reset();

	m_swap_chain.Reset();
	m_swap_chain_rtv[0].Reset();
	m_swap_chain_rtv[1].Reset();

	for (u32 i = 0; i < m_textures.size(); i++) {
		m_textures[i].Reset();
	}
	m_textures.clear();

	m_fence.Reset();
	m_queue.Reset();
	//ID3D12DebugDevice* d3d_debug = nullptr;
	//m_device->QueryInterface(__uuidof(ID3D12DebugDevice), reinterpret_cast<void**>(&d3d_debug));
//d3d_debug->ReportLiveDeviceObjects(D3D12_RLDO_IGNORE_INTERNAL);
	m_device.Reset();
}

/// Renderer_Dx12::get_hardware_adapter
void Renderer_Dx12::get_hardware_adapter(
	IDXGIFactory1* const factory,
	IDXGIAdapter1** const out_adapter,
	bool request_high_performance) {
	*out_adapter = nullptr;

	ComPtr<IDXGIAdapter1> adapter;
	ComPtr<IDXGIFactory6> factory6;
	if (SUCCEEDED(factory->QueryInterface(IID_PPV_ARGS(&factory6)))) {
		for (
			UINT adapter_index = 0;
			SUCCEEDED(factory6->EnumAdapterByGpuPreference(
				adapter_index,
				request_high_performance == true ? DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE : DXGI_GPU_PREFERENCE_UNSPECIFIED,
				IID_PPV_ARGS(&adapter)));
				++adapter_index)
		{
			DXGI_ADAPTER_DESC1 desc;
			adapter->GetDesc1(&desc);

			if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) {
				continue;
			}

			// Check to see whether the adapter supports Direct3D 12, but don't create the
			// actual device yet.
			if (SUCCEEDED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, _uuidof(ID3D12Device), nullptr))) {
				break;
			}
		}
	}

	if (adapter.Get() == nullptr) {
		for (UINT adapter_index = 0; SUCCEEDED(factory->EnumAdapters1(adapter_index, &adapter)); ++adapter_index) {
			DXGI_ADAPTER_DESC1 desc;
			adapter->GetDesc1(&desc);

			if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) {
				continue;
			}

			// Check to see whether the adapter supports Direct3D 12, but don't create the
			// actual device yet.
			if (SUCCEEDED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, _uuidof(ID3D12Device), nullptr))) {
				break;
			}
		}
	}

	*out_adapter = adapter.Detach();
}

/// Renderer_Dx12::create_render_buffer_internal
RenderBuffer* Renderer_Dx12::create_render_buffer_internal() {
	return new RenderBuffer_Dx12();
}

/// Renderer_Dx12::render_gbuffer_internal
void Renderer_Dx12::render_gbuffer_internal() {
	// Update constant buffer
	m_camera_projection.make_identity();
	m_camera_projection.create_perspective_matrix(
		g_fov,
		m_frame_width / (f32)m_frame_height,
		g_near_clip_plane,
		g_far_clip_plane
	);

	const Mat4 trans = Mat4::make_translation(-m_camera_position);
	Mat4 rot = m_camera_rotation.to_mat4();
	rot.transpose_self();

	Mat4 view_matrix = trans * rot;
	Mat4 vp_matrix =
		view_matrix *
		m_camera_projection;

	XMMATRIX inv_vp_matrix = XMMatrixInverse(nullptr, (*(XMMATRIX*)&vp_matrix));

	blk::error_check(m_command_allocator->Reset());
	blk::error_check(m_command_list->Reset(m_command_allocator.Get(), nullptr));

	m_command_list->SetGraphicsRootSignature(m_root_signature.Get());
	m_command_list->RSSetViewports(1, &m_view_port);
	m_command_list->RSSetScissorRects(1, &m_scissor_rect);

	// Indicate that the back buffer will be used as a render target.
	auto rt_barrier = CD3DX12_RESOURCE_BARRIER::Transition(m_swap_chain_rtv[m_frame_index].Get(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
	m_command_list->ResourceBarrier(1, &rt_barrier);

	rt_barrier = CD3DX12_RESOURCE_BARRIER::Transition(m_render_targets[ERenderTarget::Color].Get(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
	m_command_list->ResourceBarrier(1, &rt_barrier);

	rt_barrier = CD3DX12_RESOURCE_BARRIER::Transition(m_render_targets[ERenderTarget::Normal].Get(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
	m_command_list->ResourceBarrier(1, &rt_barrier);

	rt_barrier = CD3DX12_RESOURCE_BARRIER::Transition(m_render_targets[ERenderTarget::Specular].Get(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
	m_command_list->ResourceBarrier(1, &rt_barrier);

	rt_barrier = CD3DX12_RESOURCE_BARRIER::Transition(m_render_targets[ERenderTarget::Depth].Get(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
	m_command_list->ResourceBarrier(1, &rt_barrier);

	CD3DX12_CPU_DESCRIPTOR_HANDLE rtv_handle[] = {
		CD3DX12_CPU_DESCRIPTOR_HANDLE(m_rtv_heap->GetCPUDescriptorHandleForHeapStart(), 2, m_rtv_descriptor_size), // todo - change to 2
		CD3DX12_CPU_DESCRIPTOR_HANDLE(m_rtv_heap->GetCPUDescriptorHandleForHeapStart(), 3, m_rtv_descriptor_size),
		CD3DX12_CPU_DESCRIPTOR_HANDLE(m_rtv_heap->GetCPUDescriptorHandleForHeapStart(), 4, m_rtv_descriptor_size),
		CD3DX12_CPU_DESCRIPTOR_HANDLE(m_rtv_heap->GetCPUDescriptorHandleForHeapStart(), 5, m_rtv_descriptor_size),
	};

	CD3DX12_CPU_DESCRIPTOR_HANDLE dsv_handle(m_depth_stencil_heap->GetCPUDescriptorHandleForHeapStart(), 0, m_depth_target_descriptor_size);
	m_command_list->OMSetRenderTargets(4, rtv_handle, false, &dsv_handle);

	//const float clear_color[] = { 0.7f, 0.8f, 1.f, 1.0f };
	const float clear_color[] = { 0.0f, 0.0f, 0.f, 0.0f };
	m_command_list->ClearRenderTargetView(rtv_handle[0], clear_color, 0, nullptr);
	m_command_list->ClearRenderTargetView(rtv_handle[1], clear_color, 0, nullptr);
	m_command_list->ClearRenderTargetView(rtv_handle[2], clear_color, 0, nullptr);
	m_command_list->ClearRenderTargetView(rtv_handle[3], clear_color, 0, nullptr);

	m_command_list->ClearDepthStencilView(dsv_handle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

	ID3D12DescriptorHeap* ppHeaps[] = { m_cbv_srv_heap.Get(), m_sampler_heap.Get() };
	m_command_list->SetDescriptorHeaps(_countof(ppHeaps), ppHeaps);
	m_command_list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	auto descriptor_size = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	CD3DX12_GPU_DESCRIPTOR_HANDLE cbvSrvHandle(m_cbv_srv_heap->GetGPUDescriptorHandleForHeapStart(), 0, descriptor_size);
	m_command_list->SetGraphicsRootDescriptorTable(0, cbvSrvHandle);
	m_command_list->SetGraphicsRootDescriptorTable(1, m_sampler_heap->GetGPUDescriptorHandleForHeapStart());

	g_global_uniform->view_projection = vp_matrix;
	g_global_uniform->inv_view_proj = (*(Mat4*)&inv_vp_matrix);
	g_global_uniform->camera = Vec4(m_camera_position, 1.f);

	// The first entry in g_scene_buffers is the global const
	m_frame_draws = 1;
	for (auto& render_comp : this->render_components()) {
		RenderBuffer_Dx12* vertex_buffer = nullptr;
		RenderBuffer_Dx12* index_buffer = nullptr;
		const kbModel* model = nullptr;

		auto& scene_buffer = g_scene_buffers[m_frame_draws];

		if (render_comp->render_pass() != ERenderPass::RP_Lighting) {
			continue;
		}

		u32 constant_offset = 0;

		if (render_comp->IsA(StaticModelComponent::GetType())) {
			const StaticModelComponent* const model_comp = static_cast<const StaticModelComponent*>(render_comp);
			model = model_comp->model();

			//	blk::log("--> %d", model->GetMaterials()[0].get_shader()->GetBlendOp());
			RenderPipeline_Dx12* const pipe = (RenderPipeline_Dx12*)get_pipeline("test_shader");
			m_command_list->SetPipelineState(pipe->m_pipeline_state.Get());

			vertex_buffer = (RenderBuffer_Dx12*)model->m_vertex_buffer;
			index_buffer = (RenderBuffer_Dx12*)model->m_index_buffer;

			const auto vertex_buf_view = vertex_buffer->vertex_buffer_view();
			m_command_list->IASetVertexBuffers(0, 1, &vertex_buf_view);

			const auto index_buf_view = index_buffer->index_buffer_view();
			m_command_list->IASetIndexBuffer(&index_buf_view);
		} else if (render_comp->IsA(SkeletalModelComponent::GetType())) {
			const SkeletalModelComponent* const skel = static_cast<const SkeletalModelComponent*>(render_comp);
			model = skel->model();

			RenderPipeline_Dx12* const pipe = (skel->is_breakable()) ? (
				((RenderPipeline_Dx12*)get_pipeline("test_destructible_shader"))) :
				((RenderPipeline_Dx12*)get_pipeline("skinned_base"));

			m_command_list->SetPipelineState(pipe->m_pipeline_state.Get());

			vertex_buffer = (RenderBuffer_Dx12*)(model->m_vertex_buffer);
			index_buffer = (RenderBuffer_Dx12*)(model->m_index_buffer);
			const auto vertex_buf_view = vertex_buffer->vertex_buffer_view();
			m_command_list->IASetVertexBuffers(0, 1, &vertex_buf_view);

			const auto index_buf_view = index_buffer->index_buffer_view();
			m_command_list->IASetIndexBuffer(&index_buf_view);

			const auto& bone_list = skel->GetFinalBoneMatrices();

			BoneInstanceData& bone_data = *(BoneInstanceData*)&(g_scene_buffers[m_frame_draws + 1]);
			for (int i = 0; i < bone_list.size() && i < 128; i++) {
				bone_data.bones[i].make_identity();
				bone_data.bones[i][0] = bone_list[i].GetAxis(0);
				bone_data.bones[i][1] = bone_list[i].GetAxis(1);
				bone_data.bones[i][2] = bone_list[i].GetAxis(2);
				bone_data.bones[i][3] = bone_list[i].GetAxis(3);

				bone_data.bones[i][0].w = 0;
				bone_data.bones[i][1].w = 0;
				bone_data.bones[i][2].w = 0;
				//	bone_data.bones[i].transpose_self();
			}
			constant_offset += 4;
		} else if (render_comp->IsA(ParticleComponent::GetType())) {
			continue;
		} else {
			blk::warn("Renderer_Dx12::render() - invalid component");
			continue;
		}

		const kbTexture* color_tex = nullptr;
		Vec4 color(1.f, 1.f, 1.f, 1.f);
		Vec4 spec(0.f, 0.f, 0.f, 1.f);
		Vec4 time(0.f, 0.f, 0.f, 0.f);
		if (render_comp->materials().size() > 0) {
			const auto& shader_params = render_comp->materials()[0].shader_params();

			for (const auto& param : shader_params) {
				if (param.param_name() == kbString("color")) {
					color = param.vector();
				}

				if (param.param_name() == kbString("spec")) {
					spec = param.vector();
				}

				if (param.param_name() == kbString("color_tex")) {
					color_tex = param.texture();
				}

				if (param.param_name() == "time") {
					time = param.vector();
				}
			}
		}

		Mat4 world_mat;
		world_mat.make_scale(render_comp->scale());
		world_mat *= render_comp->rotation().to_mat4();
		world_mat[3] = render_comp->position();
		scene_buffer.world = world_mat;

		scene_buffer.mvp = (world_mat * vp_matrix);

		scene_buffer.color = color;
		scene_buffer.spec = spec;
		scene_buffer.time_since_spawn = time;

		m_command_list->SetGraphicsRoot32BitConstant(3, (u32)m_frame_draws, 0);
		m_command_list->SetGraphicsRootDescriptorTable(0, cbvSrvHandle);

		CD3DX12_GPU_DESCRIPTOR_HANDLE gpu_handle(m_cbv_srv_heap->GetGPUDescriptorHandleForHeapStart(), g_max_scene_constants, descriptor_size);
		if (color_tex != nullptr) {
			gpu_handle.Offset(descriptor_size * color_tex->get_texture_id());
		}

		m_command_list->SetGraphicsRootDescriptorTable(2, gpu_handle);
		m_command_list->DrawIndexedInstanced(index_buffer->num_elements(), 1, 0, 0, 0);
		m_frame_draws = m_frame_draws + 1 + constant_offset;
	}

	rt_barrier = CD3DX12_RESOURCE_BARRIER::Transition(m_render_targets[ERenderTarget::Color].Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
	m_command_list->ResourceBarrier(1, &rt_barrier);

	rt_barrier = CD3DX12_RESOURCE_BARRIER::Transition(m_render_targets[ERenderTarget::Normal].Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
	m_command_list->ResourceBarrier(1, &rt_barrier);

	rt_barrier = CD3DX12_RESOURCE_BARRIER::Transition(m_render_targets[ERenderTarget::Specular].Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
	m_command_list->ResourceBarrier(1, &rt_barrier);

	rt_barrier = CD3DX12_RESOURCE_BARRIER::Transition(m_render_targets[ERenderTarget::Depth].Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
	m_command_list->ResourceBarrier(1, &rt_barrier);
}

/// Renderer_Dx12::render_lights_internal
void Renderer_Dx12::render_lights_internal() {
	assert(sizeof(LightInstanceData) == sizeof(SceneInstanceData));

	// Update constant buffer
	m_camera_projection.make_identity();
	m_camera_projection.create_perspective_matrix(
		g_fov,
		m_frame_width / (f32)m_frame_height,
		g_near_clip_plane,
		g_far_clip_plane
	);

	const Mat4 trans = Mat4::make_translation(-m_camera_position);
	Mat4 rot = m_camera_rotation.to_mat4();
	rot.transpose_self();

	Mat4 view_matrix = trans * rot;
	Mat4 vp_matrix =
		view_matrix *
		m_camera_projection;

	XMMATRIX inv_vp_matrix = XMMatrixInverse(nullptr, (*(XMMATRIX*)&vp_matrix));

	CD3DX12_CPU_DESCRIPTOR_HANDLE dsv_handle(m_depth_stencil_heap->GetCPUDescriptorHandleForHeapStart(), 0, m_depth_target_descriptor_size);
	CD3DX12_CPU_DESCRIPTOR_HANDLE rtv_handle(m_rtv_heap->GetCPUDescriptorHandleForHeapStart(), m_frame_index, m_rtv_descriptor_size);

	m_command_list->OMSetRenderTargets(1, &rtv_handle, false, &dsv_handle);

	const float clear_color[] = { 0.0f, 0.0f, 0.f, 0.0f };
	m_command_list->ClearRenderTargetView(rtv_handle, clear_color, 0, nullptr);

	const auto& lights = this->light_components();
	for (auto& light : lights) {
		RenderPipeline_Dx12* pipe = nullptr;
		if (light->IsA(kbDirectionalLightComponent::GetType())) {
			pipe = (RenderPipeline_Dx12*)get_pipeline("directional_light");
		} else {
			pipe = (RenderPipeline_Dx12*)get_pipeline("point_light");
		}

		m_command_list->SetPipelineState(pipe->m_pipeline_state.Get());
		m_command_list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		m_command_list->IASetVertexBuffers(0, 1, &m_quad_vb_view);


		CD3DX12_GPU_DESCRIPTOR_HANDLE gpu_handle(m_cbv_srv_heap->GetGPUDescriptorHandleForHeapStart(), g_max_scene_constants, m_rtv_descriptor_size);
		m_command_list->SetGraphicsRootDescriptorTable(2, gpu_handle);

		LightInstanceData* light_instance_data = (LightInstanceData*)&g_scene_buffers[m_frame_draws];
		light_instance_data->position = light->owner_position();
		light_instance_data->position.w = light->radius();
		light_instance_data->color = light->GetColor();
		light_instance_data->direction = light->owner_rotation().to_mat4()[2].ToVec3();
		light_instance_data->light_matrices[0] = light_matrices[0];

		m_command_list->SetGraphicsRoot32BitConstant(3, (u32)m_frame_draws, 0);

		gpu_handle.Offset(m_rtv_descriptor_size);
		m_command_list->SetGraphicsRootDescriptorTable(4, gpu_handle);

		m_command_list->DrawInstanced(6, 1, 0, 0);
		m_frame_draws++;
	}
}

/// Renderer_Dx12::render_transluency_internal
void Renderer_Dx12::render_transluency_internal() {
	const Mat4 trans = Mat4::make_translation(-m_camera_position);
	Mat4 rot = m_camera_rotation.to_mat4();
	rot.transpose_self();

	Mat4 view_matrix = trans * rot;
	Mat4 vp_matrix =
		view_matrix *
		m_camera_projection;

	XMMATRIX inv_vp_matrix = XMMatrixInverse(nullptr, (*(XMMATRIX*)&vp_matrix));
	auto descriptor_size = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	ID3D12DescriptorHeap* ppHeaps[] = { m_cbv_srv_heap.Get(), m_sampler_heap.Get() };
	m_command_list->SetDescriptorHeaps(_countof(ppHeaps), ppHeaps);
	m_command_list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	CD3DX12_GPU_DESCRIPTOR_HANDLE cbvSrvHandle(m_cbv_srv_heap->GetGPUDescriptorHandleForHeapStart(), 0, descriptor_size);
	m_command_list->SetGraphicsRootDescriptorTable(0, cbvSrvHandle);
	m_command_list->SetGraphicsRootDescriptorTable(1, m_sampler_heap->GetGPUDescriptorHandleForHeapStart());


	g_global_uniform->view_projection = vp_matrix;
	g_global_uniform->inv_view_proj = (*(Mat4*)&inv_vp_matrix);
	g_global_uniform->camera = Vec4(m_camera_position, 1.f);

	for (auto& render_comp : this->render_components()) {
		if (render_comp->render_pass() != ERenderPass::RP_Translucent) {
			continue;
		}

		RenderBuffer_Dx12* vertex_buffer = nullptr;
		RenderBuffer_Dx12* index_buffer = nullptr;
		const kbModel* model = nullptr;

		auto& scene_buffer = g_scene_buffers[m_frame_draws];

		if (render_comp->IsA(StaticModelComponent::GetType())) {
			const StaticModelComponent* const skel = static_cast<const StaticModelComponent*>(render_comp);
			model = skel->model();

			RenderPipeline_Dx12* const pipe = (RenderPipeline_Dx12*)get_pipeline("mesh_particle_add");
			m_command_list->SetPipelineState(pipe->m_pipeline_state.Get());

			vertex_buffer = (RenderBuffer_Dx12*)model->m_vertex_buffer;
			index_buffer = (RenderBuffer_Dx12*)model->m_index_buffer;

			const auto vertex_buf_view = vertex_buffer->vertex_buffer_view();
			m_command_list->IASetVertexBuffers(0, 1, &vertex_buf_view);

			const auto index_buf_view = index_buffer->index_buffer_view();
			m_command_list->IASetIndexBuffer(&index_buf_view);
		} else if (render_comp->IsA(SkeletalModelComponent::GetType())) {
			const SkeletalModelComponent* const skel = static_cast<const SkeletalModelComponent*>(render_comp);
			model = skel->model();

			RenderPipeline_Dx12* const pipe = (skel->is_breakable()) ? (
				((RenderPipeline_Dx12*)get_pipeline("test_destructible_shader"))) :
				((RenderPipeline_Dx12*)get_pipeline("skinned_base"));

			m_command_list->SetPipelineState(pipe->m_pipeline_state.Get());

			vertex_buffer = (RenderBuffer_Dx12*)(model->m_vertex_buffer);
			index_buffer = (RenderBuffer_Dx12*)(model->m_index_buffer);
			const auto vertex_buf_view = vertex_buffer->vertex_buffer_view();
			m_command_list->IASetVertexBuffers(0, 1, &vertex_buf_view);

			const auto index_buf_view = index_buffer->index_buffer_view();
			m_command_list->IASetIndexBuffer(&index_buf_view);

			const auto& bone_list = skel->GetFinalBoneMatrices();

			BoneInstanceData& bone_data = *(BoneInstanceData*)&(g_scene_buffers[m_frame_draws + 1]);
			for (int i = 0; i < bone_list.size(); i++) {
				bone_data.bones[i].make_identity();
				bone_data.bones[i][0] = bone_list[i].GetAxis(0);
				bone_data.bones[i][1] = bone_list[i].GetAxis(1);
				bone_data.bones[i][2] = bone_list[i].GetAxis(2);
				bone_data.bones[i][3] = bone_list[i].GetAxis(3);

				bone_data.bones[i][0].w = 0;
				bone_data.bones[i][1].w = 0;
				bone_data.bones[i][2].w = 0;
				bone_data.bones[i].transpose_self();
			}
		} else if (render_comp->IsA(ParticleComponent::GetType())) {
			const ParticleComponent* const particle = static_cast<const ParticleComponent*>(render_comp);
			model = particle->get_model();

			RenderPipeline_Dx12* pipe = nullptr;
			auto& materials = particle->materials();

			if (materials.size() > 0) {
				switch (materials[0].blend_override()) {
					case EBlendMode::Additive: {
						pipe = (RenderPipeline_Dx12*)get_pipeline("sprite_particle_add");
						break;
					}
					case EBlendMode::Alpha:
					default: {
						pipe = (RenderPipeline_Dx12*)get_pipeline("sprite_particle_blend");
						break;
					}
				}
			}
			m_command_list->SetPipelineState(pipe->m_pipeline_state.Get());

			if (model == nullptr) {
				// Particle buffering might not be ready yet
				continue;
			} else {
				const auto vertex_buf_view = ((RenderBuffer_Dx12*)model->vertex_buffer())->vertex_buffer_view();
				m_command_list->IASetVertexBuffers(0, 1, &vertex_buf_view);
				index_buffer = (RenderBuffer_Dx12*)(model->m_index_buffer);
				const auto index_buf_view = index_buffer->index_buffer_view();
				m_command_list->IASetIndexBuffer(&index_buf_view);
			}
		} else {
			blk::warn("Renderer_Dx12::render() - invalid component");
			continue;
		}

		const kbTexture* color_tex = nullptr;
		Vec4 color(1.f, 1.f, 1.f, 1.f);
		Vec4 spec(0.f, 0.f, 0.f, 1.f);
		Vec4 time(0.f, 0.f, 0.f, 0.f);
		if (render_comp->materials().size() > 0) {
			const auto& shader_params = render_comp->materials()[0].shader_params();

			for (const auto& param : shader_params) {
				if (param.param_name() == kbString("color")) {
					color = param.vector();
				}

				if (param.param_name() == kbString("spec")) {
					spec = param.vector();
				}

				if (param.param_name() == kbString("color_tex")) {
					color_tex = param.texture();
				}

				if (param.param_name() == "time") {
					time = param.vector();
				}
			}
		}

		Mat4 world_mat;
		world_mat.make_scale(render_comp->scale());
		world_mat *= render_comp->rotation().to_mat4();
		world_mat[3] = render_comp->position();

		scene_buffer.mvp = (world_mat * vp_matrix);
		scene_buffer.world = world_mat;
		scene_buffer.color = color;
		scene_buffer.time_since_spawn = time;

		m_command_list->SetGraphicsRoot32BitConstant(3, (u32)m_frame_draws, 0);
		m_command_list->SetGraphicsRootDescriptorTable(0, cbvSrvHandle);

		CD3DX12_GPU_DESCRIPTOR_HANDLE gpu_handle(m_cbv_srv_heap->GetGPUDescriptorHandleForHeapStart(), g_max_scene_constants, descriptor_size);
		if (color_tex != nullptr) {
			gpu_handle.Offset(descriptor_size * color_tex->get_texture_id());
		}

		m_command_list->SetGraphicsRootDescriptorTable(2, gpu_handle);
		m_command_list->DrawIndexedInstanced(index_buffer->num_elements(), 1, 0, 0, 0);
		m_frame_draws++;
	}
}

/// Renderer_Dx12::present
void Renderer_Dx12::present() {
	// Indicate that the back buffer will now be used to present.
	auto res_barrier = CD3DX12_RESOURCE_BARRIER::Transition(m_swap_chain_rtv[m_frame_index].Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
	m_command_list->ResourceBarrier(1, &res_barrier);
	blk::error_check(m_command_list->Close());

	// Execute command lists
	ID3D12CommandList* const command_lists[] = { m_command_list.Get() };
	m_queue->ExecuteCommandLists(_countof(command_lists), command_lists);

	// Present
	blk::error_check(m_swap_chain->Present(1, 0));

	// Wait for previous frame (todo)
	wait_on_fence();

	m_frame_index = m_swap_chain->GetCurrentBackBufferIndex();
}

/// Renderer_Dx12::create_pipeline
RenderPipeline* Renderer_Dx12::create_pipeline(const string& friendly_name, const string& path) {
#if defined(_DEBUG)
	// Enable better shader debugging with the graphics debugging tools.
	UINT compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION | D3DCOMPILE_WARNINGS_ARE_ERRORS;
#else
	UINT compileFlags = 0;
#endif

	const bool is_sprite_particle = (friendly_name.find("sprite_particle") != path.npos);
	const bool is_light = (friendly_name.find("_light") != path.npos);
	const bool is_shadow_proj = blk::std_contains(friendly_name, "shadow_projection");
	const bool is_shadow_depth = blk::std_contains(friendly_name, "shadow_depth");
	u32 blend_type = 0;
	if (friendly_name.find("_blend") != friendly_name.npos) {
		blend_type = 2;
	} else if (friendly_name.find("_add") != friendly_name.npos || is_light) {
		blend_type = 1;
	}

	Microsoft::WRL::ComPtr<ID3DBlob> errors;

	wstring pipeline_path;
	WStringFromString(pipeline_path, path);

	ComPtr<ID3DBlob> vertex_shader;
	if (!blk::warn_check(D3DCompileFromFile(
		pipeline_path.c_str(),
		nullptr,
		nullptr,
		"vertex_shader",
		"vs_5_1",
		compileFlags,
		0,
		&vertex_shader,
		&errors))) {
		blk::error("%s", errors->GetBufferPointer());
	}

	ComPtr<ID3DBlob> pixel_shader;
	DXGI_FORMAT depth_stencil_fmt = DXGI_FORMAT_D24_UNORM_S8_UINT;

	if (blk::std_contains(friendly_name, "shadow_depth")) {
		if (!blk::error_check(
			D3DCompileFromFile(
				pipeline_path.c_str(),
				nullptr,
				nullptr,
				"shadow_pixel_shader",
				"ps_5_1",
				compileFlags,
				0,
				&pixel_shader,
				&errors))) {
			blk::error("%s", errors->GetBufferPointer());
		}
		depth_stencil_fmt = DXGI_FORMAT_D32_FLOAT;
	} else {
		if (!blk::error_check(
			D3DCompileFromFile(
				pipeline_path.c_str(),
				nullptr,
				nullptr,
				"pixel_shader",
				"ps_5_1",
				compileFlags,
				0,
				&pixel_shader,
				&errors))) {
			blk::error("%s", errors->GetBufferPointer());
		}
	}

	vector<D3D12_INPUT_ELEMENT_DESC> input_element_desc;
	if (is_light || is_shadow_proj) {
		input_element_desc.push_back({ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 });
		input_element_desc.push_back({ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 });
	} else if (is_sprite_particle) {
		input_element_desc.push_back({ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 });
		input_element_desc.push_back({ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 });
		input_element_desc.push_back({ "COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, 20, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 });
		input_element_desc.push_back({ "NORMAL", 0, DXGI_FORMAT_R32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 });
		input_element_desc.push_back({ "TANGENT", 0, DXGI_FORMAT_R32_FLOAT, 0, 28, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 });
	} else {
		input_element_desc.push_back({ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 });
		input_element_desc.push_back({ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 });
		input_element_desc.push_back({ "COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, 20, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 });
		input_element_desc.push_back({ "NORMAL", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 });
		input_element_desc.push_back({ "TANGENT", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, 28, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 });
	}

	auto raster = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	raster.CullMode = D3D12_CULL_MODE_NONE;
	if (is_shadow_depth) {
		raster.CullMode = D3D12_CULL_MODE_BACK;
	}
	// Describe and create the graphics pipeline state object (PSO).
	D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
	psoDesc.InputLayout = { input_element_desc.data(), (u32)input_element_desc.size() };
	psoDesc.pRootSignature = m_root_signature.Get();
	psoDesc.VS = CD3DX12_SHADER_BYTECODE(vertex_shader.Get());
	psoDesc.PS = CD3DX12_SHADER_BYTECODE(pixel_shader.Get());
	psoDesc.RasterizerState = raster;

	psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT); // a default depth stencil state
	psoDesc.DSVFormat = depth_stencil_fmt;

	if (is_light || is_shadow_proj) {
		psoDesc.DepthStencilState.DepthEnable = false;
	}

	if (blend_type == 1) {
		D3D12_BLEND_DESC blend_desc = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
		blend_desc.RenderTarget[0].BlendEnable = true;
		blend_desc.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
		blend_desc.RenderTarget[0].SrcBlend = D3D12_BLEND::D3D12_BLEND_ONE;
		blend_desc.RenderTarget[0].DestBlend = D3D12_BLEND::D3D12_BLEND_ONE;
		psoDesc.BlendState = blend_desc;

		psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
	} else if (blend_type == 2) {
		D3D12_BLEND_DESC blend_desc = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
		blend_desc.RenderTarget[0].BlendEnable = true;
		blend_desc.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
		blend_desc.RenderTarget[0].SrcBlend = D3D12_BLEND::D3D12_BLEND_SRC_ALPHA;
		blend_desc.RenderTarget[0].DestBlend = D3D12_BLEND::D3D12_BLEND_INV_SRC_ALPHA;
		psoDesc.BlendState = blend_desc;

		psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
	} else {
		psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	}

	psoDesc.SampleMask = UINT_MAX;
	psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;

	if (!is_light && blend_type == 0) {
		psoDesc.NumRenderTargets = 4;
		psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
		psoDesc.RTVFormats[1] = DXGI_FORMAT_R8G8B8A8_UNORM;
		psoDesc.RTVFormats[2] = DXGI_FORMAT_R8G8B8A8_UNORM;
		psoDesc.RTVFormats[3] = DXGI_FORMAT_R32_FLOAT;
	} else {
		psoDesc.NumRenderTargets = 1;
		psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
	}

	psoDesc.SampleDesc.Count = 1;
	RenderPipeline_Dx12* const pipe = new RenderPipeline_Dx12();
	blk::error_check(m_device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&pipe->m_pipeline_state)));

	return (RenderPipeline*)pipe;
}

/// Renderer_Dx12::load_texture
u32 Renderer_Dx12::load_texture(const std::string& path) {
	wstring texture_path;
	WStringFromString(texture_path, path);
	if (!texture_path.ends_with(L".dds")) {
		return -1;
	}

	blk::error_check(m_command_allocator->Reset());
	blk::error_check(m_command_list->Reset(m_command_allocator.Get(), nullptr));

	ComPtr<ID3D12Resource> upload_resource;
	// Load texture 
	{//for (u32 i = 0; i < 2; i++) {


		blk::log("Loading %s", path.c_str());
		ComPtr<ID3D12Resource> tex;
		std::unique_ptr<uint8_t[]> ddsData;
		std::vector<D3D12_SUBRESOURCE_DATA> subresources;
		blk::error_check(LoadDDSTextureFromFile(
			m_device.Get(),
			texture_path.c_str(),
			tex.ReleaseAndGetAddressOf(),
			ddsData,
			subresources));

		// Create gpu upload buffer
		const uint64_t upload_buff_size = GetRequiredIntermediateSize(tex.Get(), 0, (uint32_t)subresources.size());

		auto upload_heap_props = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
		auto upload_heap_buff_size = CD3DX12_RESOURCE_DESC::Buffer(upload_buff_size);
		blk::error_check(
			m_device->CreateCommittedResource(
				&upload_heap_props,
				D3D12_HEAP_FLAG_NONE,
				&upload_heap_buff_size,
				D3D12_RESOURCE_STATE_GENERIC_READ,
				nullptr,
				IID_PPV_ARGS(&upload_resource)));

		UpdateSubresources(m_command_list.Get(), tex.Get(), upload_resource.Get(),
			0, 0, static_cast<UINT>(subresources.size()), subresources.data());

		auto tex_barrier = CD3DX12_RESOURCE_BARRIER::Transition(tex.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
		m_command_list->ResourceBarrier(1, &tex_barrier);

		this->m_textures.push_back(tex);
	}

	static u32 tex_count = ERenderTarget::Count;
	const auto CBV_SRV_DESCRIPTOR_SIZE = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	static CD3DX12_CPU_DESCRIPTOR_HANDLE texHandle(m_cbv_srv_heap->GetCPUDescriptorHandleForHeapStart(), g_max_scene_constants + tex_count, m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER));

	{//for (u32 i = 0; i < g_max_instances; i++) {
		// Texture srv
		D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc = {};
		srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

		// hack
		if (path.find("smoke") != path.npos) {
			srv_desc.Format = DXGI_FORMAT_BC3_UNORM;
		} else {
			srv_desc.Format = DXGI_FORMAT_BC1_UNORM;
		}

		srv_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		srv_desc.Texture2D.MipLevels = 1;

		m_device->CreateShaderResourceView(m_textures.back().Get(), &srv_desc, texHandle);
		texHandle.Offset(CBV_SRV_DESCRIPTOR_SIZE);
	}

	// Close the command list and execute it to begin the initial GPU setup.
	blk::error_check(m_command_list->Close());
	ID3D12CommandList* ppCommandLists[] = { m_command_list.Get() };
	m_queue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);

	wait_on_fence();
	return tex_count++;
}

/// Renderer_Dx12::todo_create_texture
void Renderer_Dx12::todo_create_texture() {
	auto pipe = (RenderPipeline_Dx12*)load_pipeline("test_shader", "C:/projects/blk/cannon/cannon/assets/shaders/static_model.kbshader");
	pipe = (RenderPipeline_Dx12*)load_pipeline("static_model_shadow_depth", "C:/projects/blk/cannon/cannon/assets/shaders/static_model.kbshader");

	pipe = (RenderPipeline_Dx12*)load_pipeline("skinned_base", "C:/projects/blk/cannon/cannon/assets/shaders/skinned_model.kbshader");
	pipe = (RenderPipeline_Dx12*)load_pipeline("skinned_shadow_depth", "C:/projects/blk/cannon/cannon/assets/shaders/skinned_model.kbshader");

	pipe = (RenderPipeline_Dx12*)load_pipeline("test_destructible_shader", "C:/projects/blk/cannon/cannon/assets/shaders/destructible.kbshader");

	pipe = (RenderPipeline_Dx12*)load_pipeline("sprite_particle_blend", "C:/projects/blk/cannon/cannon/assets/shaders/sprite_particle.kbshader");
	pipe = (RenderPipeline_Dx12*)load_pipeline("sprite_particle_add", "C:/projects/blk/cannon/cannon/assets/shaders/sprite_particle.kbshader");
	pipe = (RenderPipeline_Dx12*)load_pipeline("mesh_particle_add", "C:/projects/blk/cannon/cannon/assets/shaders/mesh_particle.kbshader");

	pipe = (RenderPipeline_Dx12*)load_pipeline("directional_light", "C:/projects/blk/cannon/cannon/assets/shaders/directional_light.kbshader");
	pipe = (RenderPipeline_Dx12*)load_pipeline("point_light", "C:/projects/blk/cannon/cannon/assets/shaders/point_light.kbshader");
	pipe = (RenderPipeline_Dx12*)load_pipeline("directional_shadow_projection", "C:/projects/blk/cannon/cannon/assets/shaders/directional_shadow.kbshader");


}

/// Renderer_Dx12::wait_on_fence
void Renderer_Dx12::wait_on_fence() {
	// Wait for previous frame (todo)
	const uint64_t fence = m_fence_value;
	blk::error_check(m_queue->Signal(m_fence.Get(), fence));
	m_fence_value++;

	// Wait until the previous frame is finished.
	if (m_fence->GetCompletedValue() < fence) {
		blk::error_check(m_fence->SetEventOnCompletion(fence, m_fence_event));
		WaitForSingleObject(m_fence_event, INFINITE);
	}
}

/// Renderer_Dx12::render_shadows
void Renderer_Dx12::render_shadows() {
	const kbDirectionalLightComponent* dir_light = nullptr;
	for (const auto light : light_components()) {
		if (light->casts_shadow() && light->IsA(kbDirectionalLightComponent::GetType())) {
			dir_light = (kbDirectionalLightComponent*)light;
			break;
		}
	}

	if (dir_light == nullptr) {
		return;
	}

	light_matrices.clear();

	// Update constant buffer
	m_camera_projection.make_identity();
	m_camera_projection.create_perspective_matrix(
		g_fov,
		m_frame_width / (f32) m_frame_height,
		g_near_clip_plane,
		g_far_clip_plane
	);

	const Mat4 trans = Mat4::make_translation(-m_camera_position);
	Mat4 rot = m_camera_rotation.to_mat4();
	rot.transpose_self();

	Mat4 view_matrix = trans * rot;
	Mat4 vp_matrix =
		view_matrix *
		m_camera_projection;

	XMMATRIX inv_vp_matrix = XMMatrixInverse(nullptr, (*(XMMATRIX*)&vp_matrix));
	const Vec3 cam_dir = m_camera_rotation.to_mat4()[2].ToVec3();

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

	m_command_list->SetGraphicsRootSignature(m_root_signature.Get());

	m_command_list->RSSetScissorRects(1, &m_scissor_rect);

	// Indicate that the back buffer will be used as a render target.
	auto rt_barrier = CD3DX12_RESOURCE_BARRIER::Transition(m_render_targets[ERenderTarget::ShadowDepth].Get(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_DEPTH_WRITE);
	m_command_list->ResourceBarrier(1, &rt_barrier);

	CD3DX12_CPU_DESCRIPTOR_HANDLE dsv_handle(m_depth_stencil_heap->GetCPUDescriptorHandleForHeapStart(), 1, m_depth_target_descriptor_size);
	m_command_list->OMSetRenderTargets(0, nullptr, false, &dsv_handle);

	m_command_list->ClearDepthStencilView(dsv_handle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

	ID3D12DescriptorHeap* ppHeaps[] = { m_cbv_srv_heap.Get(), m_sampler_heap.Get() };
	m_command_list->SetDescriptorHeaps(_countof(ppHeaps), ppHeaps);
	m_command_list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	auto descriptor_size = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	CD3DX12_GPU_DESCRIPTOR_HANDLE cbvSrvHandle(m_cbv_srv_heap->GetGPUDescriptorHandleForHeapStart(), 0, descriptor_size);
	m_command_list->SetGraphicsRootDescriptorTable(0, cbvSrvHandle);
	m_command_list->SetGraphicsRootDescriptorTable(1, m_sampler_heap->GetGPUDescriptorHandleForHeapStart());

	// Cascade loop here
	const auto& cascade_dists = dir_light->cascade_start_distances();


	for (size_t i = 0; i < 1 && cascade_dists[i] < FLT_MAX; i++) {
		D3D12_VIEWPORT viewport = {};
		viewport.TopLeftX = 0;// (i % 2)* g_shadow_tex_dimensions * 0.5f;
		viewport.TopLeftY = 0;//(i / 2.f)* g_shadow_tex_dimensions * 0.5f;
		viewport.Width = g_shadow_tex_dimensions;
		viewport.Height = g_shadow_tex_dimensions;
		viewport.MinDepth = 0.0f;
		viewport.MaxDepth = 1.0f;
		m_command_list->RSSetViewports(1, &viewport);
		const auto scisscor_rect = CD3DX12_RECT(0, 0, g_shadow_tex_dimensions, g_shadow_tex_dimensions);
		m_command_list->RSSetScissorRects(1, &scisscor_rect);

		const float prev_cascade_dist = (i == 0) ? (0.0f) : (cascade_dists[i] - 1);
		const Vec3 look_at_point = m_camera_position + cam_dir * (prev_cascade_dist + (cascade_dists[i] - prev_cascade_dist) * 0.5f);
		const float half_fov = g_fov * 0.5f;
		const float dist_to_corner = cascade_dists[i] / cos(half_fov);
		Vec3 corner_vert = m_camera_position + dist_to_corner * ul;
		const float bounds_len = (look_at_point - corner_vert).length();
		/*
				const float prevDist = ( i == 0 ) ? ( 0.0f ) : ( pLight->m_CascadedShadowSplits[i] - 1 );
		const Vec3 lookAtPoint = frozenCameraPosition + camDir * ( prevDist + ( pLight->m_CascadedShadowSplits[i] - prevDist ) * 0.5f );
		const float halfFOV = kbToRadians ( 75.0f ) * 0.5f;
		const float distToCorner = pLight->m_CascadedShadowSplits[i] / ( cos( halfFOV ) );
		Vec3 cornerVert = frozenCameraPosition + distToCorner * upperLeft;
		const float boundsLength = ( lookAtPoint - cornerVert ).length();
		* */
		// Light matrices
		const Vec3 light_dir = dir_light->owner_rotation().to_mat4()[2].ToVec3();
		const Mat4 light_view = Mat4::look_at(look_at_point + light_dir * bounds_len * 10.f, look_at_point, Vec3(0.0f, 1.0f, 0.0f));
		const Mat4 light_view_proj = light_view * Mat4::ortho_lh(bounds_len * 2.0f, bounds_len * 2.0f, 10.0f, bounds_len * 40.0f);

		const f32 texel_size = 2.0f / (g_shadow_tex_dimensions * 0.5f);
		Vec4 proj_center(0.0f, 0.0f, 0.0f, 1.0f);
		proj_center = proj_center.transform_point(light_view_proj, true);

		const f32 far_corner_dist = g_far_clip_plane / (cam_dir.dot(ul));
		const f32 near_corner_dist = (g_near_clip_plane * far_corner_dist) / g_far_clip_plane;

		const float fracX = fmod(proj_center.x, texel_size);
		const float fracY = fmod(proj_center.y, texel_size);

		Mat4 offset;
		offset.make_identity();
		offset[3][0] = -fracX;
		offset[3][1] = -fracY;

		/*const Mat4 texture_matrix(
			Vec4(0.5f, 0.0f, 0.0f, 0.0f),			Vec4(0.f, -0.5f, 0.f, 0.f),
			Vec4(0.f, 0.f, 1.f, 0.f),
			Vec4(0.5f + (0.5f / g_shadow_tex_dimensions), 0.5f + (0.5f / g_shadow_tex_dimensions), 0.f, 1.f)
		);*/
		Mat4 texture_matrix;
		texture_matrix.make_identity();
		texture_matrix[0].x = 0.5f;
		texture_matrix[1].y = -0.5f;
		texture_matrix[3].x = 0.5f + (0.5f / g_shadow_tex_dimensions);
		texture_matrix[3].y = 0.5f + (0.5f / g_shadow_tex_dimensions);

		const Mat4 cascade_mat = light_view_proj * offset;

		light_matrices.push_back(cascade_mat * texture_matrix);

		// The first entry in g_scene_buffers is the global const
		//m_frame_draws += 10;	
		for (auto& render_comp : this->render_components()) {
			RenderBuffer_Dx12* vertex_buffer = nullptr;
			RenderBuffer_Dx12* index_buffer = nullptr;
			const kbModel* model = nullptr;

			auto& scene_buffer = g_scene_buffers[m_frame_draws];

			if (render_comp->render_pass() != ERenderPass::RP_Lighting) {
				continue;
			}

			u32 constant_offset = 0;

			if (render_comp->IsA(StaticModelComponent::GetType())) {
				const StaticModelComponent* const model_comp = static_cast<const StaticModelComponent*>(render_comp);
				model = model_comp->model();

				//	blk::log("--> %d", model->GetMaterials()[0].get_shader()->GetBlendOp());
				RenderPipeline_Dx12* const pipe = (RenderPipeline_Dx12*)get_pipeline("static_model_shadow_depth");
				m_command_list->SetPipelineState(pipe->m_pipeline_state.Get());

				vertex_buffer = (RenderBuffer_Dx12*)model->m_vertex_buffer;
				index_buffer = (RenderBuffer_Dx12*)model->m_index_buffer;

				const auto vertex_buf_view = vertex_buffer->vertex_buffer_view();
				m_command_list->IASetVertexBuffers(0, 1, &vertex_buf_view);

				const auto index_buf_view = index_buffer->index_buffer_view();
				m_command_list->IASetIndexBuffer(&index_buf_view);
			} else if (render_comp->IsA(SkeletalModelComponent::GetType())) {
				const SkeletalModelComponent* const skel = static_cast<const SkeletalModelComponent*>(render_comp);
				model = skel->model();

				if (skel->is_breakable()) {
					continue;
				}
				RenderPipeline_Dx12* const pipe = ((RenderPipeline_Dx12*)get_pipeline("skinned_shadow_depth"));

				m_command_list->SetPipelineState(pipe->m_pipeline_state.Get());

				vertex_buffer = (RenderBuffer_Dx12*)(model->m_vertex_buffer);
				index_buffer = (RenderBuffer_Dx12*)(model->m_index_buffer);
				const auto vertex_buf_view = vertex_buffer->vertex_buffer_view();
				m_command_list->IASetVertexBuffers(0, 1, &vertex_buf_view);

				const auto index_buf_view = index_buffer->index_buffer_view();
				m_command_list->IASetIndexBuffer(&index_buf_view);

				const auto& bone_list = skel->GetFinalBoneMatrices();

				BoneInstanceData& bone_data = *(BoneInstanceData*)&(g_scene_buffers[m_frame_draws + 1]);
				for (int i = 0; i < bone_list.size() && i < 128; i++) {
					bone_data.bones[i].make_identity();
					bone_data.bones[i][0] = bone_list[i].GetAxis(0);
					bone_data.bones[i][1] = bone_list[i].GetAxis(1);
					bone_data.bones[i][2] = bone_list[i].GetAxis(2);
					bone_data.bones[i][3] = bone_list[i].GetAxis(3);

					bone_data.bones[i][0].w = 0;
					bone_data.bones[i][1].w = 0;
					bone_data.bones[i][2].w = 0;
					//	bone_data.bones[i].transpose_self();
				}
				constant_offset += 4;
			} else if (render_comp->IsA(ParticleComponent::GetType())) {
				continue;	  
			} else {
				blk::warn("Renderer_Dx12::render() - invalid component");
				continue;
			}

			const kbTexture* color_tex = nullptr;
			Vec4 color(1.f, 1.f, 1.f, 1.f);
			Vec4 spec(0.f, 0.f, 0.f, 1.f);
			Vec4 time(0.f, 0.f, 0.f, 0.f);
			if (render_comp->materials().size() > 0) {
				const auto& shader_params = render_comp->materials()[0].shader_params();

				for (const auto& param : shader_params) {
					if (param.param_name() == kbString("color")) {
						color = param.vector();
					}

					if (param.param_name() == kbString("spec")) {
						spec = param.vector();
					}

					if (param.param_name() == kbString("color_tex")) {
						color_tex = param.texture();
					}

					if (param.param_name() == "time") {
						time = param.vector();
					}
				}
			}

			Mat4 world_mat;
			world_mat.make_scale(render_comp->scale());
			world_mat *= render_comp->rotation().to_mat4();
			world_mat[3] = render_comp->position();
			scene_buffer.world = world_mat;

			scene_buffer.mvp = world_mat * cascade_mat;//(world_mat * vp_matrix);

			scene_buffer.color = color;
			scene_buffer.spec = spec;
			scene_buffer.time_since_spawn = time;

			m_command_list->SetGraphicsRoot32BitConstant(3, (u32)m_frame_draws, 0);
			m_command_list->SetGraphicsRootDescriptorTable(0, cbvSrvHandle);

			CD3DX12_GPU_DESCRIPTOR_HANDLE gpu_handle(m_cbv_srv_heap->GetGPUDescriptorHandleForHeapStart(), g_max_scene_constants, descriptor_size);
			if (color_tex != nullptr) {
				gpu_handle.Offset(descriptor_size * color_tex->get_texture_id());
			}

			m_command_list->SetGraphicsRootDescriptorTable(2, gpu_handle);
			m_command_list->DrawIndexedInstanced(index_buffer->num_elements(), 1, 0, 0, 0);
			m_frame_draws = m_frame_draws + 1 + constant_offset;
		}
	}

	rt_barrier = CD3DX12_RESOURCE_BARRIER::Transition(m_render_targets[ERenderTarget::ShadowDepth].Get(), D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_PRESENT);
	m_command_list->ResourceBarrier(1, &rt_barrier);

	// Project Shadows
	m_command_list->RSSetViewports(1, &m_view_port);
	m_command_list->RSSetScissorRects(1, &m_scissor_rect);


	// Indicate that the back buffer will be used as a render target.
	rt_barrier = CD3DX12_RESOURCE_BARRIER::Transition(m_render_targets[ERenderTarget::Lighting].Get(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
	m_command_list->ResourceBarrier(1, &rt_barrier);


	CD3DX12_CPU_DESCRIPTOR_HANDLE rtv_handle(m_rtv_heap->GetCPUDescriptorHandleForHeapStart(), 2 + ERenderTarget::Lighting, m_rtv_descriptor_size);
/*

	CD3DX12_CPU_DESCRIPTOR_HANDLE rtv_handle[] = {
		CD3DX12_CPU_DESCRIPTOR_HANDLE(m_rtv_heap->GetCPUDescriptorHandleForHeapStart(), 2, m_rtv_descriptor_size), // todo - change to 2
		CD3DX12_CPU_DESCRIPTOR_HANDLE(m_rtv_heap->GetCPUDescriptorHandleForHeapStart(), 3, m_rtv_descriptor_size),
		CD3DX12_CPU_DESCRIPTOR_HANDLE(m_rtv_heap->GetCPUDescriptorHandleForHeapStart(), 4, m_rtv_descriptor_size),
		CD3DX12_CPU_DESCRIPTOR_HANDLE(m_rtv_heap->GetCPUDescriptorHandleForHeapStart(), 5, m_rtv_descriptor_size),
	};

	CD3DX12_CPU_DESCRIPTOR_HANDLE dsv_handle(m_depth_stencil_heap->GetCPUDescriptorHandleForHeapStart(), 0, m_depth_target_descriptor_size);
	m_command_list->OMSetRenderTargets(4, rtv_handle, false, &dsv_handle);

* */
	m_command_list->OMSetRenderTargets(1, &rtv_handle, false, nullptr);

	const float clear_color[] = { 0.0f, 0.0f, 0.f, 0.0f };
	m_command_list->ClearRenderTargetView(rtv_handle, clear_color, 0, nullptr);

	{
		RenderPipeline_Dx12* pipe = (RenderPipeline_Dx12*)get_pipeline("directional_shadow_projection");

		m_command_list->SetPipelineState(pipe->m_pipeline_state.Get());
		m_command_list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		m_command_list->IASetVertexBuffers(0, 1, &m_quad_vb_view);

		// Texture
		CD3DX12_GPU_DESCRIPTOR_HANDLE gpu_handle(m_cbv_srv_heap->GetGPUDescriptorHandleForHeapStart(), g_max_scene_constants, m_rtv_descriptor_size);
		m_command_list->SetGraphicsRootDescriptorTable(2, gpu_handle);

		LightInstanceData* light_instance_data = (LightInstanceData*)&g_scene_buffers[m_frame_draws];
		light_instance_data->position = dir_light->owner_position();
		light_instance_data->position.w = dir_light->radius();
		light_instance_data->color = dir_light->GetColor();
		light_instance_data->direction = dir_light->owner_rotation().to_mat4()[2].ToVec3();
		light_instance_data->light_matrices[0] = light_matrices[0];
		m_command_list->SetGraphicsRoot32BitConstant(3, (u32)m_frame_draws, 0);

		gpu_handle.Offset(m_rtv_descriptor_size);
		m_command_list->SetGraphicsRootDescriptorTable(4, gpu_handle);

		m_command_list->DrawInstanced(6, 1, 0, 0);
		m_frame_draws++;
	}

	rt_barrier = CD3DX12_RESOURCE_BARRIER::Transition(m_render_targets[ERenderTarget::Lighting].Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
	m_command_list->ResourceBarrier(1, &rt_barrier);
}
