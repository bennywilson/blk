/// Renderer_Dx12.cpp
///
/// 2025 blk 1.0

#include <chrono>
#include <d3d12sdklayers.h>
#include <filesystem>
#include <fstream>
#include "blk_core.h"
#include "blk_containers.h"
#include "entity_header.h"
#include "renderer_dx12.h"
#include <dxgi1_6.h>
#include "d3dx12.h"
#include "DDSTextureLoader12.h"
#include "d3d12_defs.h"
#include "render_component.h"
#include "Plane3d.h"

#include <dxcapi.h>
#include <sstream>
#include <vector>
#include <wrl/client.h>

#include <dxgidebug.h>

using namespace std;
namespace fs = std::filesystem;

// Scene Config
static const u32 g_max_scene_constants = 512;
static const u32 g_max_scene_bone_arrays = 512;
static const u32 g_max_scene_srvs = 512;

static const u32 g_max_point_cloud_points = 4000000;

static const u32 g_bone_array_descriptor_start = g_max_scene_constants;
static const u32 g_srv_descriptor_start = g_max_scene_constants + g_max_scene_bone_arrays;

static const f32 g_near_clip_plane = 1.f;
static const f32 g_far_clip_plane = 20000.f;
static const f32 g_fov = kbToRadians(75.f);

// Video Config
static const bool g_high_performance_adapter = true;

static const u32 g_shadow_tex_dimensions = (g_high_performance_adapter) ? (4096) : (1024);

// Todo...
XMMATRIX& XMMATRIXFromMat4(Mat4& matrix) { return (*(XMMATRIX*)&matrix); }
Mat4& Mat4FromXMMATRIX(FXMMATRIX& matrix) { return (*(Mat4*)&matrix); }

const auto g_D3D12_HEAP_TYPE_UPLOAD = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
const auto g_D3D12_HEAP_TYPE_DEFAULT = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);

std::vector<Mat4> light_matrices;
Vec4 cascade_distances;

struct Mat4Test {
	Mat4 a[8];
};
/// GlobalUniformData
struct GlobalUniformData {
	Mat4 view;
	Mat4 view_projection;
	Mat4 inv_view_proj;
	Vec4 camera_pos;
	Vec4 splat_sharpen_scale_near_far;
	Vec4 splat_contrast;
	Vec4 pad[17];
}*g_global_uniform = nullptr;

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
}*g_scene_buffers = nullptr;

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
}*g_bone_array_buffers = nullptr;

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
}*g_point_cloud = nullptr;

uint32_t* g_point_cloud_indices = nullptr;

static_assert(
	sizeof(SceneInstanceData) == sizeof(GlobalUniformData) &&
	sizeof(SceneInstanceData) == sizeof(PointCloudSampleInstance) &&
	sizeof(SceneInstanceData) == sizeof(LightInstanceData)
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

	IDXGIInfoQueue* dxgiInfoQueue;
	if (SUCCEEDED(DXGIGetDebugInterface1(0, IID_PPV_ARGS(&dxgiInfoQueue)))) {
		dxgiInfoQueue->SetBreakOnSeverity(DXGI_DEBUG_ALL, DXGI_INFO_QUEUE_MESSAGE_SEVERITY_ERROR, TRUE);
	}
#endif

	ComPtr<IDXGIFactory4> factory;
	CreateDXGIFactory2(dxgiFactoryFlags, IID_PPV_ARGS(&factory));

	ComPtr<IDXGIAdapter1> hw_adapter;

	{
		ComPtr<IDXGIFactory6> factory6;
		blk::error_check(factory->QueryInterface(IID_PPV_ARGS(&factory6)),
			"Renderer_Dx12::initialize_internal() - Failed to query for IDXGIFactory6"
		);

		const DXGI_GPU_PREFERENCE preference = g_high_performance_adapter ? DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE : DXGI_GPU_PREFERENCE_MINIMUM_POWER;

		ComPtr<IDXGIAdapter1> preferredAdapter;
		blk::error_check(factory6->EnumAdapterByGpuPreference(0, preference, IID_PPV_ARGS(&preferredAdapter)),
			"Renderer_Dx12::initialize_internal() - Failed to query for Enumerate Adapters"
		);

		// Create the D3D12 device
		D3D12CreateDevice(preferredAdapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&m_device));
	}

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
		m_queue.Get(),
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
	rtv_heap_desc.NumDescriptors = (1 + ERenderTarget::Count) * Renderer::max_frames();		// (swap chain + render targets) * max_frames
	rtv_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
	rtv_heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	blk::error_check(m_device->CreateDescriptorHeap(&rtv_heap_desc, IID_PPV_ARGS(&m_rtv_heap)));
	m_rtv_descriptor_size = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

	// Depth target descriptor heap
	D3D12_DESCRIPTOR_HEAP_DESC depth_heap_desc = {};
	depth_heap_desc.NumDescriptors = (1 + 1) * Renderer::max_frames();						// Scene depth buffer + shadow buffer
	depth_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
	depth_heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	blk::error_check(m_device->CreateDescriptorHeap(&depth_heap_desc, IID_PPV_ARGS(&m_depth_stencil_heap)));
	m_depth_target_descriptor_size = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);

	// SRV descriptor heap
	D3D12_DESCRIPTOR_HEAP_DESC srv_heap_desc = {};
	srv_heap_desc.NumDescriptors = g_max_scene_constants + g_max_scene_srvs + g_max_scene_bone_arrays;
	srv_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	srv_heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	blk::error_check(m_device->CreateDescriptorHeap(&srv_heap_desc, IID_PPV_ARGS(&m_cbv_srv_descriptor_heap)));
	m_cbv_srv_descriptor_heap->SetName(L"Renderer_Dx12::m_cbv_srv_descriptor_heap");

	// Sampler heap
	D3D12_DESCRIPTOR_HEAP_DESC sampler_heap_desc = {};
	sampler_heap_desc.NumDescriptors = 1;
	sampler_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
	sampler_heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	blk::error_check(m_device->CreateDescriptorHeap(&sampler_heap_desc, IID_PPV_ARGS(&m_sampler_descriptor_heap)));
	m_sampler_descriptor_heap->SetName(L"Renderer_Dx12::m_sampler_heap");

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
	sampler_desc.ComparisonFunc = D3D12_COMPARISON_FUNC_NONE;
	m_device->CreateSampler(&sampler_desc, m_sampler_descriptor_heap->GetCPUDescriptorHandleForHeapStart());

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

		for (uint32_t i = 0; i < Renderer::max_frames(); i++) {
			m_device->CreateCommittedResource(
				&ds_heap_prop,
				D3D12_HEAP_FLAG_NONE,
				&resource_desc,
				D3D12_RESOURCE_STATE_DEPTH_WRITE,
				&depthOptimizedClearValue,
				IID_PPV_ARGS(&m_depth_stencil_buffer[i])
			);
			m_depth_stencil_heap->SetName(L"Depth/Stencil Resource Heap");
			m_device->CreateDepthStencilView(m_depth_stencil_buffer[i].Get(), &depthStencilDesc, depth_target_handle);
			depth_target_handle.Offset(1, m_depth_target_descriptor_size);
		}
	}

	// Quad
	{
		struct QuadVert {
			QuadVert(Vec3 p, Vec2 _uv) :
				pos(p),
				uv(_uv) {
			}
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

	// Create a RTV for each frame.
	for (uint32_t i = 0; i < Renderer::max_frames(); i++) {
		blk::error_check(m_swap_chain->GetBuffer(i, IID_PPV_ARGS(&m_swap_chain_rtv[i])));
		m_device->CreateRenderTargetView(m_swap_chain_rtv[i].Get(), nullptr, rtv_handle);
		rtv_handle.Offset(1, m_rtv_descriptor_size);
	}

	blk::error_check(m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_command_allocator)));
	blk::error_check(m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_command_allocator.Get(), nullptr, IID_PPV_ARGS(&m_command_list)));

	// Constants
	const auto CBV_SRV_DESCRIPTOR_SIZE = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	const auto cbv_heap_props = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);

	// m_cbv_srv_descriptor_heap contains:
	//		Scene Instance CBVs x g_max_scene_srvs
	//		Bone CBVs x g_max_scene_bone_arrays
	//		GBuffer SRVs x ERenderTarget::Count
	CD3DX12_CPU_DESCRIPTOR_HANDLE scene_cbv_srv_handle(m_cbv_srv_descriptor_heap->GetCPUDescriptorHandleForHeapStart(), 0, CBV_SRV_DESCRIPTOR_SIZE);

	// Scene Instance Constants
	{
		// Todo - While not ideal for perf, GPU can read directly from an upload heap
		const auto cbv_buffer_size = CD3DX12_RESOURCE_DESC::Buffer(g_max_scene_constants * sizeof(SceneInstanceData));
		blk::error_check(m_device->CreateCommittedResource(
			&cbv_heap_props,
			D3D12_HEAP_FLAG_NONE,
			&cbv_buffer_size,
			D3D12_RESOURCE_STATE_GENERIC_READ,
			nullptr,
			IID_PPV_ARGS(&m_scene_cbv_upload_heap)));

		CD3DX12_RANGE readRange(0, 0);
		g_scene_buffers = nullptr;
		blk::error_check(m_scene_cbv_upload_heap->Map(0, &readRange, reinterpret_cast<void**>(&g_scene_buffers)));
		g_global_uniform = (GlobalUniformData*)g_scene_buffers;

		// Create cbvs
		u64 cb_offset = 0;
		for (u32 i = 0; i < g_max_scene_constants; i++) {
			D3D12_CONSTANT_BUFFER_VIEW_DESC cbv_desc = {};
			cbv_desc.BufferLocation = m_scene_cbv_upload_heap->GetGPUVirtualAddress() + cb_offset;
			cbv_desc.SizeInBytes = sizeof(SceneInstanceData);
			cb_offset += cbv_desc.SizeInBytes;

			m_device->CreateConstantBufferView(&cbv_desc, scene_cbv_srv_handle);
			scene_cbv_srv_handle.Offset(CBV_SRV_DESCRIPTOR_SIZE);
		}
	}

	// Bone Heap
	{
		const auto cbv_buffer_size = CD3DX12_RESOURCE_DESC::Buffer(g_max_scene_bone_arrays * sizeof(BoneInstanceData));
		blk::error_check(m_device->CreateCommittedResource(
			&cbv_heap_props,
			D3D12_HEAP_FLAG_NONE,
			&cbv_buffer_size,
			D3D12_RESOURCE_STATE_GENERIC_READ,
			nullptr,
			IID_PPV_ARGS(&m_bone_cbv_upload_heap)));

		CD3DX12_RANGE readRange(0, 0);
		g_bone_array_buffers = nullptr;
		blk::error_check(m_bone_cbv_upload_heap->Map(0, &readRange, reinterpret_cast<void**>(&g_bone_array_buffers)));

		// Create cbvs
		u64 cb_offset = 0;
		for (u32 i = 0; i < g_max_scene_bone_arrays; i++) {
			D3D12_CONSTANT_BUFFER_VIEW_DESC cbv_desc = {};
			cbv_desc.BufferLocation = m_bone_cbv_upload_heap->GetGPUVirtualAddress() + cb_offset;
			cbv_desc.SizeInBytes = sizeof(BoneInstanceData);
			cb_offset += cbv_desc.SizeInBytes;

			m_device->CreateConstantBufferView(&cbv_desc, scene_cbv_srv_handle);
			scene_cbv_srv_handle.Offset(CBV_SRV_DESCRIPTOR_SIZE);
		}
	}

	// Initialize GBuffers
	const auto default_heap_props = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
	for (u32 frame_idx = 0; frame_idx < Renderer::max_frames(); frame_idx++) {
		// Color Buffer
		{
			const DXGI_FORMAT format = DXGI_FORMAT_R8G8B8A8_UNORM;
			const D3D12_CLEAR_VALUE clear_value = { format, {0.f, 0.f, 0.f, 0.f} };
			const D3D12_RESOURCE_DESC desc = CD3DX12_RESOURCE_DESC::Tex2D(
				format,
				(u64)m_frame_width,
				(u32)m_frame_height,
				1, 1, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET
			);

			auto& rt = m_render_targets[ERenderTarget::Color][frame_idx];
			blk::error_check(
				m_device->CreateCommittedResource(
					&default_heap_props,
					D3D12_HEAP_FLAG_ALLOW_ALL_BUFFERS_AND_TEXTURES,
					&desc,
					D3D12_RESOURCE_STATE_RENDER_TARGET,
					&clear_value,
					IID_PPV_ARGS(rt.ReleaseAndGetAddressOf())
				)
			);
			rt.Get()->SetName(L"Renderer_Dx12::Color");

			m_device->CreateRenderTargetView(rt.Get(), nullptr, rtv_handle);
			rtv_handle.Offset(1, m_rtv_descriptor_size);

			m_device->CreateShaderResourceView(rt.Get(), nullptr, scene_cbv_srv_handle);
			scene_cbv_srv_handle.Offset(CBV_SRV_DESCRIPTOR_SIZE);

			auto rt_barrier = CD3DX12_RESOURCE_BARRIER::Transition(rt.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
			m_command_list->ResourceBarrier(1, &rt_barrier);
		}

		// Normal Buffer
		{
			const DXGI_FORMAT format = DXGI_FORMAT_R8G8B8A8_UNORM;
			const D3D12_CLEAR_VALUE clear_value = { format, {0.5f, 0.5f, 0.5f, 0.f} };
			const D3D12_RESOURCE_DESC desc = CD3DX12_RESOURCE_DESC::Tex2D(format,
				(u64)m_frame_width,
				(u32)m_frame_height,
				1, 1, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET
			);

			auto& rt = m_render_targets[ERenderTarget::Normal][frame_idx];
			blk::error_check(
				m_device->CreateCommittedResource(
					&default_heap_props,
					D3D12_HEAP_FLAG_ALLOW_ALL_BUFFERS_AND_TEXTURES,
					&desc,
					D3D12_RESOURCE_STATE_RENDER_TARGET,
					&clear_value,
					IID_PPV_ARGS(rt.ReleaseAndGetAddressOf())
				)
			);
			rt.Get()->SetName(L"Renderer_Dx12::Normal");

			m_device->CreateRenderTargetView(rt.Get(), nullptr, rtv_handle);
			rtv_handle.Offset(1, m_rtv_descriptor_size);

			m_device->CreateShaderResourceView(rt.Get(), nullptr, scene_cbv_srv_handle);
			scene_cbv_srv_handle.Offset(CBV_SRV_DESCRIPTOR_SIZE);

			auto rt_barrier = CD3DX12_RESOURCE_BARRIER::Transition(rt.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
			m_command_list->ResourceBarrier(1, &rt_barrier);
		}

		// Specular Buffer
		{
			const auto format = DXGI_FORMAT_R8G8B8A8_UNORM;
			const D3D12_CLEAR_VALUE clear_value = { format, {0.f, 0.f, 0.f, 0.f} };
			const D3D12_RESOURCE_DESC desc = CD3DX12_RESOURCE_DESC::Tex2D(format,
				(u64)m_frame_width,
				(u32)m_frame_height,
				1, 1, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET
			);

			auto& rt = m_render_targets[ERenderTarget::Specular][frame_idx];
			blk::error_check(
				m_device->CreateCommittedResource(
					&default_heap_props,
					D3D12_HEAP_FLAG_ALLOW_ALL_BUFFERS_AND_TEXTURES,
					&desc,
					D3D12_RESOURCE_STATE_RENDER_TARGET,
					&clear_value,
					IID_PPV_ARGS(rt.ReleaseAndGetAddressOf())
				)
			);
			rt.Get()->SetName(L"Renderer_Dx12::Specular");

			m_device->CreateRenderTargetView(rt.Get(), nullptr, rtv_handle);
			rtv_handle.Offset(1, m_rtv_descriptor_size);

			m_device->CreateShaderResourceView(rt.Get(), nullptr, scene_cbv_srv_handle);
			scene_cbv_srv_handle.Offset(CBV_SRV_DESCRIPTOR_SIZE);

			auto rt_barrier = CD3DX12_RESOURCE_BARRIER::Transition(rt.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
			m_command_list->ResourceBarrier(1, &rt_barrier);
		}

		//  Scene Depth
		{
			const auto format = DXGI_FORMAT_R32_FLOAT;
			const D3D12_CLEAR_VALUE clear_value = { format, {0.f, 0.f, 0.f, 0.f} };
			const D3D12_RESOURCE_DESC desc = CD3DX12_RESOURCE_DESC::Tex2D(format,
				(u64)m_frame_width,
				(u32)m_frame_height,
				1, 1, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET
			);

			auto& rt = m_render_targets[ERenderTarget::SceneDepth][frame_idx];
			blk::error_check(
				m_device->CreateCommittedResource(
					&default_heap_props, D3D12_HEAP_FLAG_ALLOW_ALL_BUFFERS_AND_TEXTURES,
					&desc,
					D3D12_RESOURCE_STATE_RENDER_TARGET,
					&clear_value,
					IID_PPV_ARGS(rt.ReleaseAndGetAddressOf())
				)
			);
			rt.Get()->SetName(L"Renderer_Dx12::SceneDepth");

			m_device->CreateRenderTargetView(rt.Get(), nullptr, rtv_handle);
			rtv_handle.Offset(1, m_rtv_descriptor_size);

			m_device->CreateShaderResourceView(rt.Get(), nullptr, scene_cbv_srv_handle);
			scene_cbv_srv_handle.Offset(CBV_SRV_DESCRIPTOR_SIZE);

			auto rt_barrier = CD3DX12_RESOURCE_BARRIER::Transition(rt.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
			m_command_list->ResourceBarrier(1, &rt_barrier);
		}

		// Lighting
		{
			const auto format = DXGI_FORMAT_R8G8B8A8_UNORM;
			const D3D12_RESOURCE_DESC desc = CD3DX12_RESOURCE_DESC::Tex2D(format,
				(u64)m_frame_width,
				(u32)m_frame_height,
				1, 1, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET);
			const D3D12_CLEAR_VALUE clear_value = { format, {0.f, 0.f, 0.f, 0.f} };

			auto& rt = m_render_targets[ERenderTarget::Lighting][frame_idx];
			blk::error_check(
				m_device->CreateCommittedResource(
					&default_heap_props,
					D3D12_HEAP_FLAG_ALLOW_ALL_BUFFERS_AND_TEXTURES,
					&desc,
					D3D12_RESOURCE_STATE_RENDER_TARGET,
					&clear_value,
					IID_PPV_ARGS(rt.ReleaseAndGetAddressOf())
				)
			);
			rt.Get()->SetName(L"Renderer_Dx12::Lighting");

			m_device->CreateRenderTargetView(rt.Get(), nullptr, rtv_handle);
			rtv_handle.Offset(1, m_rtv_descriptor_size);

			m_device->CreateShaderResourceView(rt.Get(), nullptr, scene_cbv_srv_handle);
			scene_cbv_srv_handle.Offset(CBV_SRV_DESCRIPTOR_SIZE);

			auto rt_barrier = CD3DX12_RESOURCE_BARRIER::Transition(rt.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
			m_command_list->ResourceBarrier(1, &rt_barrier);
		}

		// SHADOW
		{
			D3D12_DEPTH_STENCIL_VIEW_DESC dsv_desc = {};
			dsv_desc.Format = DXGI_FORMAT_D32_FLOAT;
			dsv_desc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
			dsv_desc.Flags = D3D12_DSV_FLAG_NONE;

			D3D12_CLEAR_VALUE clear_value = {};
			clear_value.Format = DXGI_FORMAT_D32_FLOAT;
			clear_value.DepthStencil.Depth = 1.0f;
			clear_value.DepthStencil.Stencil = 0;

			auto& rt = m_render_targets[ERenderTarget::ShadowDepth][frame_idx];
			auto resource_desc = CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_D32_FLOAT, g_shadow_tex_dimensions, g_shadow_tex_dimensions, 1, 0, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL);
			m_device->CreateCommittedResource(
				&default_heap_props,
				D3D12_HEAP_FLAG_NONE,
				&resource_desc,
				D3D12_RESOURCE_STATE_DEPTH_WRITE,
				&clear_value,
				IID_PPV_ARGS(rt.ReleaseAndGetAddressOf())
			);

			m_device->CreateDepthStencilView(rt.Get(), &dsv_desc, depth_target_handle);
			depth_target_handle.Offset(1, m_depth_target_descriptor_size);

			D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc = {};
			srv_desc.Format = DXGI_FORMAT_R32_FLOAT;
			srv_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
			srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
			srv_desc.Texture2D.MipLevels = 1;
			m_device->CreateShaderResourceView(rt.Get(), &srv_desc, scene_cbv_srv_handle);
			scene_cbv_srv_handle.Offset(CBV_SRV_DESCRIPTOR_SIZE);

			auto rt_barrier = CD3DX12_RESOURCE_BARRIER::Transition(rt.Get(), D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_PRESENT);
			m_command_list->ResourceBarrier(1, &rt_barrier);
		}
	}

	// General root signature
	{
		// The root signature determines what kind of data the shader should expect.
		CD3DX12_DESCRIPTOR_RANGE1 ranges[4] = {};
		ranges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, g_max_scene_constants, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC);
		ranges[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER, 1, 0);
		ranges[2].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, g_max_scene_srvs, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC);
		ranges[3].Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, g_max_scene_bone_arrays, 0, 2, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC);

		// Root parameters are entries in the root signature
		CD3DX12_ROOT_PARAMETER1 root_parameters[6] = {};
		root_parameters[0].InitAsDescriptorTable(1, &ranges[0], D3D12_SHADER_VISIBILITY_ALL);		// scene_constants
		root_parameters[1].InitAsDescriptorTable(1, &ranges[1], D3D12_SHADER_VISIBILITY_PIXEL);		// sampler
		root_parameters[2].InitAsDescriptorTable(1, &ranges[2], D3D12_SHADER_VISIBILITY_PIXEL);		// srv
		root_parameters[3].InitAsConstants(1, 0, 1, D3D12_SHADER_VISIBILITY_ALL);					// scene_indices
		root_parameters[4].InitAsDescriptorTable(1, &ranges[3], D3D12_SHADER_VISIBILITY_VERTEX);	// bones
		root_parameters[5].InitAsConstants(1, 0, 3, D3D12_SHADER_VISIBILITY_ALL);					// bone_index

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
	}

	// Point-cloud root signature
	{
		// Descriptor Heap
		D3D12_DESCRIPTOR_HEAP_DESC heap_desc = {};
		heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		heap_desc.NumDescriptors = 2; // 1 CBV + 1 SRV
		heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
		heap_desc.NodeMask = 0;
		m_device->CreateDescriptorHeap(&heap_desc, IID_PPV_ARGS(&m_point_cloud_descriptor_heap));
		m_point_cloud_descriptor_heap->SetName(L"Renderer_Dx12::m_point_cloud_descriptor_heap");

		// Buffer
		const UINT buffer_size = sizeof(PointCloudSampleInstance) * g_max_point_cloud_points;

		// Point Cloud data heaps
		D3D12_RESOURCE_DESC desc = CD3DX12_RESOURCE_DESC::Buffer(buffer_size, D3D12_RESOURCE_FLAG_NONE);
		{
			ComPtr<ID3D12Resource> upload_resource;

			// Upload heap (CPU)
			m_device->CreateCommittedResource(
				&g_D3D12_HEAP_TYPE_UPLOAD,
				D3D12_HEAP_FLAG_NONE,
				&desc,
				D3D12_RESOURCE_STATE_GENERIC_READ,
				nullptr,
				IID_PPV_ARGS(&m_point_cloud_upload_heap));

			CD3DX12_RANGE readRange(0, 0);
			m_point_cloud_upload_heap->Map(0, &readRange, reinterpret_cast<void**>(&g_point_cloud));
			m_point_cloud_upload_heap->SetName(L"Renderer_Dx12::m_point_cloud_upload_heap");

			// Default Heap
			blk::error_check(m_device->CreateCommittedResource(
				&g_D3D12_HEAP_TYPE_DEFAULT,
				D3D12_HEAP_FLAG_NONE,
				&desc,
				D3D12_RESOURCE_STATE_COMMON,
				nullptr,
				IID_PPV_ARGS(&m_point_cloud_default_heap)
			));
			m_point_cloud_default_heap->SetName(L"Renderer_Dx12::m_point_cloud_default_heap");

			D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc = {};
			srv_desc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
			srv_desc.Format = DXGI_FORMAT_UNKNOWN; // Required for structured buffers
			srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
			srv_desc.Buffer.FirstElement = 0;
			srv_desc.Buffer.NumElements = g_max_point_cloud_points;
			srv_desc.Buffer.StructureByteStride = sizeof(PointCloudSampleInstance);
			srv_desc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;

			// Get handle from your descriptor heap
			D3D12_CPU_DESCRIPTOR_HANDLE srv_handle = m_point_cloud_descriptor_heap->GetCPUDescriptorHandleForHeapStart();
			m_device->CreateShaderResourceView(
				m_point_cloud_default_heap.Get(), // This is the GPU-resident buffer
				&srv_desc,
				srv_handle
			);
		}

		// Point Cloud index heaps
		{
			ComPtr<ID3D12Resource> upload_resource;

			// Upload heap (CPU)
			m_device->CreateCommittedResource(
				&g_D3D12_HEAP_TYPE_UPLOAD,
				D3D12_HEAP_FLAG_NONE,
				&desc,
				D3D12_RESOURCE_STATE_GENERIC_READ,
				nullptr,
				IID_PPV_ARGS(&m_point_cloud_index_upload_heap));

			CD3DX12_RANGE readRange(0, 0);
			m_point_cloud_index_upload_heap->Map(0, &readRange, reinterpret_cast<void**>(&g_point_cloud_indices));
			m_point_cloud_index_upload_heap->SetName(L"Renderer_Dx12::m_point_cloud_index_upload_heap");

			// Default Heap
			blk::error_check(m_device->CreateCommittedResource(
				&g_D3D12_HEAP_TYPE_DEFAULT,
				D3D12_HEAP_FLAG_NONE,
				&desc,
				D3D12_RESOURCE_STATE_COMMON,
				nullptr,
				IID_PPV_ARGS(&m_point_cloud_index_default_heap)
			));
			m_point_cloud_index_default_heap->SetName(L"Renderer_Dx12::m_point_cloud_index_default_heap");

			D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc = {};
			srv_desc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
			srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
			srv_desc.Buffer.FirstElement = 0;
			srv_desc.Buffer.NumElements = g_max_point_cloud_points; // Should match your splat count
			srv_desc.Buffer.StructureByteStride = sizeof(uint);
			srv_desc.Format = DXGI_FORMAT_UNKNOWN; // Required for structured buffers


			// Get handle from your descriptor heap
			CD3DX12_CPU_DESCRIPTOR_HANDLE srv_handle(m_point_cloud_descriptor_heap->GetCPUDescriptorHandleForHeapStart());
			srv_handle.Offset(1, CBV_SRV_DESCRIPTOR_SIZE);
			m_device->CreateShaderResourceView(
				m_point_cloud_default_heap.Get(), // This is the GPU-resident buffer
				&srv_desc,
				srv_handle
			);
			m_device->CreateShaderResourceView(m_point_cloud_index_default_heap.Get(), &srv_desc, srv_handle);
		}

		auto resource_barrier = CD3DX12_RESOURCE_BARRIER::Transition(
			m_point_cloud_default_heap.Get(),
			D3D12_RESOURCE_STATE_COMMON,
			D3D12_RESOURCE_STATE_COPY_DEST
		);
		m_command_list->ResourceBarrier(1, &resource_barrier);

		auto point_cloud_srv_range = CD3DX12_DESCRIPTOR_RANGE1(
			D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
			2,        // Descriptor Count 2 : PointCloudSampleInstances + Sorted Indices
			0,        // BaseShaderRegister: t0
			0,        // RegisterSpace
			D3D12_DESCRIPTOR_RANGE_FLAG_NONE
		);


		CD3DX12_ROOT_PARAMETER1 splat_root_parameters[2] = {};
		splat_root_parameters[0].InitAsConstantBufferView(0, 0, D3D12_ROOT_DESCRIPTOR_FLAG_NONE, D3D12_SHADER_VISIBILITY_VERTEX);
		splat_root_parameters[1].InitAsDescriptorTable(
			1,
			&point_cloud_srv_range,
			D3D12_SHADER_VISIBILITY_VERTEX
		);

		const D3D12_ROOT_SIGNATURE_FLAGS signature_flags =
			D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
			D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
			D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
			D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS;

		CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC root_signature_desc = {};
		root_signature_desc.Init_1_1(_countof(splat_root_parameters), splat_root_parameters, 0, nullptr, signature_flags);

		ComPtr<ID3DBlob> signature;
		ComPtr<ID3DBlob> error;
		if (!blk::warn_check(D3DX12SerializeVersionedRootSignature(&root_signature_desc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &error))) {
			blk::error("%s", error->GetBufferPointer());
		}
		blk::error_check(m_device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&m_point_cloud_signature)));
		m_point_cloud_signature->SetName(L"Renderer_Dx12::m_point_cloud_signature");
	}

	// Fences
	m_fence_value = 0;
	blk::error_check(m_device->CreateFence(m_fence_value, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence)));

	// Create an event handle to use for frame synchronization.
	m_fence_event = CreateEvent(nullptr, FALSE, FALSE, nullptr);
	if (m_fence_event == nullptr) {
		blk::error_check(HRESULT_FROM_WIN32(GetLastError()));
	}

	init_default_pipelines();

	blk::error_check(m_command_list->Close());

	// Execute command lists
	ID3D12CommandList* const command_lists[] = { m_command_list.Get() };
	m_queue->ExecuteCommandLists(_countof(command_lists), command_lists);
	wait_on_fence();

	blk::log("Renderer_Dx12 initialized");
}

/// Renderer_Dx12::shut_down_internal
void Renderer_Dx12::shut_down_internal() {
	wait_on_fence();

	m_scene_cbv_upload_heap->Unmap(0, nullptr);
	m_bone_cbv_upload_heap->Unmap(0, nullptr);

	m_quad_vb.Reset();

	for (u32 frame_idx = 0; frame_idx < Renderer::max_frames(); frame_idx++) {
		m_depth_stencil_buffer[frame_idx].Reset();
		for (u32 target_idx = 0; target_idx < ERenderTarget::Count; target_idx++) {
			m_render_targets[target_idx][frame_idx].Reset();
		}
	}

	m_root_signature.Reset();
	m_point_cloud_signature.Reset();

	m_point_cloud_default_heap.Reset();
	m_point_cloud_upload_heap.Reset();

	m_point_cloud_index_upload_heap.Reset();
	m_point_cloud_index_default_heap.Reset();

	m_scene_cbv_upload_heap.Reset();
	m_bone_cbv_upload_heap.Reset();

	m_cbv_srv_descriptor_heap.Reset();
	m_sampler_descriptor_heap.Reset();
	m_rtv_heap.Reset();
	m_depth_stencil_heap.Reset();
	m_depth_target_heap.Reset();

	m_command_allocator.Reset();
	m_command_list.Reset();

	m_swap_chain.Reset();
	m_swap_chain_rtv[0].Reset();
	m_swap_chain_rtv[1].Reset();

	for (u32 i = 0; i < m_textures.size(); i++) {
		m_textures[i].Reset();
	}
	m_textures.clear();

	m_dxc_compiler.Reset();
	m_dxc_include_handler.Reset();
	m_dxc_utils.Reset();

	m_fence.Reset();
	m_queue.Reset();

	IDXGIDebug* dxgiDebug;
	if (SUCCEEDED(DXGIGetDebugInterface1(0, IID_PPV_ARGS(&dxgiDebug)))) {
		blk::log("//----------------------------------------------------------------------//");
		blk::log("  D3D12 Live Objects Summary");
		blk::log("//----------------------------------------------------------------------//");
		dxgiDebug->ReportLiveObjects(DXGI_DEBUG_ALL, DXGI_DEBUG_RLO_SUMMARY);

		blk::log("\n\n//----------------------------------------------------------------------//");
		blk::log("  D3D12 Live Objects Detail");
		blk::log("//----------------------------------------------------------------------//");
		dxgiDebug->ReportLiveObjects(DXGI_DEBUG_ALL, DXGI_DEBUG_RLO_DETAIL);

		blk::log("\n\n//----------------------------------------------------------------------//");
		blk::log("  D3D12 Live Objects Mem Leaks");
		blk::log("//----------------------------------------------------------------------//");
		dxgiDebug->ReportLiveObjects(DXGI_DEBUG_ALL, DXGI_DEBUG_RLO_IGNORE_INTERNAL);
		blk::log("\n\n");
	}
	m_device.Reset();



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
	auto rt_barrier = CD3DX12_RESOURCE_BARRIER::Transition(m_render_targets[ERenderTarget::Color][m_frame_index].Get(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
	m_command_list->ResourceBarrier(1, &rt_barrier);

	rt_barrier = CD3DX12_RESOURCE_BARRIER::Transition(m_render_targets[ERenderTarget::Normal][m_frame_index].Get(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
	m_command_list->ResourceBarrier(1, &rt_barrier);

	rt_barrier = CD3DX12_RESOURCE_BARRIER::Transition(m_render_targets[ERenderTarget::Specular][m_frame_index].Get(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
	m_command_list->ResourceBarrier(1, &rt_barrier);

	rt_barrier = CD3DX12_RESOURCE_BARRIER::Transition(m_render_targets[ERenderTarget::SceneDepth][m_frame_index].Get(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
	m_command_list->ResourceBarrier(1, &rt_barrier);

	// Todo: Subtract 1 since the shadow render target doesn't have an associated rtv
	const u32 gbuffer_start = Renderer::max_frames() + (ERenderTarget::Count - 1) * m_frame_index;
	CD3DX12_CPU_DESCRIPTOR_HANDLE rtv_handle[] = {
		CD3DX12_CPU_DESCRIPTOR_HANDLE(m_rtv_heap->GetCPUDescriptorHandleForHeapStart(), gbuffer_start + 0, m_rtv_descriptor_size),
		CD3DX12_CPU_DESCRIPTOR_HANDLE(m_rtv_heap->GetCPUDescriptorHandleForHeapStart(), gbuffer_start + 1, m_rtv_descriptor_size),
		CD3DX12_CPU_DESCRIPTOR_HANDLE(m_rtv_heap->GetCPUDescriptorHandleForHeapStart(), gbuffer_start + 2, m_rtv_descriptor_size),
		CD3DX12_CPU_DESCRIPTOR_HANDLE(m_rtv_heap->GetCPUDescriptorHandleForHeapStart(), gbuffer_start + 3, m_rtv_descriptor_size),
	};

	CD3DX12_CPU_DESCRIPTOR_HANDLE dsv_handle(m_depth_stencil_heap->GetCPUDescriptorHandleForHeapStart(), 0, m_depth_target_descriptor_size);
	m_command_list->OMSetRenderTargets(4, rtv_handle, false, &dsv_handle);

	const f32 clear_color[] = { 0.f, 0.f, 0.f, 0.f };
	const f32 normal_color[] = { 0.5f, 0.5f, 0.5f, 0.f };
	m_command_list->ClearRenderTargetView(rtv_handle[0], clear_color, 0, nullptr);
	m_command_list->ClearRenderTargetView(rtv_handle[1], normal_color, 0, nullptr);
	m_command_list->ClearRenderTargetView(rtv_handle[2], clear_color, 0, nullptr);
	m_command_list->ClearRenderTargetView(rtv_handle[3], clear_color, 0, nullptr);

	m_command_list->ClearDepthStencilView(dsv_handle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

	ID3D12DescriptorHeap* ppHeaps[] = { m_cbv_srv_descriptor_heap.Get(), m_sampler_descriptor_heap.Get() };
	m_command_list->SetDescriptorHeaps(_countof(ppHeaps), ppHeaps);
	m_command_list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	auto descriptor_size = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	CD3DX12_GPU_DESCRIPTOR_HANDLE cbvSrvHandle(m_cbv_srv_descriptor_heap->GetGPUDescriptorHandleForHeapStart(), 0, descriptor_size);
	m_command_list->SetGraphicsRootDescriptorTable(0, cbvSrvHandle);
	m_command_list->SetGraphicsRootDescriptorTable(1, m_sampler_descriptor_heap->GetGPUDescriptorHandleForHeapStart());

	CD3DX12_GPU_DESCRIPTOR_HANDLE bone_descriptor_handle(m_cbv_srv_descriptor_heap->GetGPUDescriptorHandleForHeapStart(), g_bone_array_descriptor_start, descriptor_size);
	m_command_list->SetGraphicsRootDescriptorTable(4, bone_descriptor_handle);

	g_global_uniform->view_projection = vp_matrix;
	g_global_uniform->inv_view_proj = (*(Mat4*)&inv_vp_matrix);
	g_global_uniform->camera_pos = Vec4(m_camera_position, 1.f);
	g_global_uniform->view = view_matrix;

	// The first entry in g_scene_buffers is the global const
	m_frame_draws = 1;
	m_bone_draws = 0;
	for (auto& render_comp : this->render_components()) {
		RenderBuffer_Dx12* vertex_buffer = nullptr;
		RenderBuffer_Dx12* index_buffer = nullptr;
		const kbModel* model = nullptr;

		auto& scene_buffer = g_scene_buffers[m_frame_draws];

		if (render_comp->render_pass() != ERenderPass::RP_Lighting) {
			continue;
		}

		if (render_comp->IsA(StaticModelComponent::GetType())) {
			const StaticModelComponent* const model_comp = static_cast<const StaticModelComponent*>(render_comp);
			model = model_comp->model();

			RenderPipeline_Dx12* const pipe = (RenderPipeline_Dx12*)get_pipeline("static_model_base");
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

			RenderPipeline_Dx12* const pipe = ((RenderPipeline_Dx12*)get_pipeline("skinned_base"));

			m_command_list->SetPipelineState(pipe->m_pipeline_state.Get());

			vertex_buffer = (RenderBuffer_Dx12*)(model->m_vertex_buffer);
			index_buffer = (RenderBuffer_Dx12*)(model->m_index_buffer);
			const auto vertex_buf_view = vertex_buffer->vertex_buffer_view();
			m_command_list->IASetVertexBuffers(0, 1, &vertex_buf_view);

			const auto index_buf_view = index_buffer->index_buffer_view();
			m_command_list->IASetIndexBuffer(&index_buf_view);

			const auto& bone_list = skel->GetFinalBoneMatrices();

			BoneInstanceData& bone_data = *(BoneInstanceData*)&(g_bone_array_buffers[m_bone_draws]);
			for (int i = 0; i < bone_list.size() && i < 128; i++) {
				bone_data.bones[i].make_identity();
				bone_data.bones[i][0] = bone_list[i].GetAxis(0);
				bone_data.bones[i][1] = bone_list[i].GetAxis(1);
				bone_data.bones[i][2] = bone_list[i].GetAxis(2);
				bone_data.bones[i][3] = bone_list[i].GetAxis(3);

				bone_data.bones[i][0].w = 0;
				bone_data.bones[i][1].w = 0;
				bone_data.bones[i][2].w = 0;
				bone_data.bones->transpose_self();
			}

			m_command_list->SetGraphicsRoot32BitConstant(5, (u32)m_bone_draws, 0);
			m_bone_draws++;
		} else if (render_comp->IsA(ParticleComponent::GetType())) {
			continue;
		} else if (render_comp->IsA(TerrainComponent::GetType())) {
			const TerrainComponent* const model_comp = static_cast<const TerrainComponent*>(render_comp);
			const kbModel& model = model_comp->model();

			RenderPipeline_Dx12* const pipe = (RenderPipeline_Dx12*)get_pipeline("terrain");
			m_command_list->SetPipelineState(pipe->m_pipeline_state.Get());

			vertex_buffer = (RenderBuffer_Dx12*)model.m_vertex_buffer;
			index_buffer = (RenderBuffer_Dx12*)model.m_index_buffer;

			const auto vertex_buf_view = vertex_buffer->vertex_buffer_view();
			m_command_list->IASetVertexBuffers(0, 1, &vertex_buf_view);

			const auto index_buf_view = index_buffer->index_buffer_view();
			m_command_list->IASetIndexBuffer(&index_buf_view);

			scene_buffer.texture_list[4] = (f32)model_comp->splat_map()->get_texture_id();
		} else {
			continue;
		}

		const Texture* color_tex = nullptr;
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
					if (color_tex) {
						scene_buffer.texture_list[0] = (f32)color_tex->get_texture_id();
					}
				}

				if (param.param_name() == kbString("color_tex_2")) {
					color_tex = param.texture();
					if (color_tex) {
						scene_buffer.texture_list[1] = (f32)color_tex->get_texture_id();
					}
				}

				if (param.param_name() == kbString("color_tex_3")) {
					color_tex = param.texture();
					if (color_tex) {
						scene_buffer.texture_list[2] = (f32)color_tex->get_texture_id();
					}
				}

				if (param.param_name() == kbString("color_tex_4")) {
					color_tex = param.texture();
					if (color_tex) {
						scene_buffer.texture_list[3] = (f32)color_tex->get_texture_id();
					}
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

		CD3DX12_GPU_DESCRIPTOR_HANDLE gpu_handle(m_cbv_srv_descriptor_heap->GetGPUDescriptorHandleForHeapStart(), g_srv_descriptor_start, descriptor_size);
		m_command_list->SetGraphicsRootDescriptorTable(2, gpu_handle);
		m_command_list->DrawIndexedInstanced(index_buffer->num_elements(), 1, 0, 0, 0);
		m_frame_draws = m_frame_draws + 1;
	}

	rt_barrier = CD3DX12_RESOURCE_BARRIER::Transition(m_render_targets[ERenderTarget::Color][m_frame_index].Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
	m_command_list->ResourceBarrier(1, &rt_barrier);

	rt_barrier = CD3DX12_RESOURCE_BARRIER::Transition(m_render_targets[ERenderTarget::Normal][m_frame_index].Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
	m_command_list->ResourceBarrier(1, &rt_barrier);

	rt_barrier = CD3DX12_RESOURCE_BARRIER::Transition(m_render_targets[ERenderTarget::Specular][m_frame_index].Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
	m_command_list->ResourceBarrier(1, &rt_barrier);

	rt_barrier = CD3DX12_RESOURCE_BARRIER::Transition(m_render_targets[ERenderTarget::SceneDepth][m_frame_index].Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
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

	auto rt_barrier = CD3DX12_RESOURCE_BARRIER::Transition(m_swap_chain_rtv[m_frame_index].Get(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
	m_command_list->ResourceBarrier(1, &rt_barrier);

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

		const auto CBV_SRV_DESCRIPTOR_SIZE = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
		CD3DX12_GPU_DESCRIPTOR_HANDLE gpu_handle(m_cbv_srv_descriptor_heap->GetGPUDescriptorHandleForHeapStart(), g_srv_descriptor_start, CBV_SRV_DESCRIPTOR_SIZE);
		m_command_list->SetGraphicsRootDescriptorTable(2, gpu_handle);

		LightInstanceData* light_instance_data = (LightInstanceData*)&g_scene_buffers[m_frame_draws];
		light_instance_data->position = light->owner_position();
		light_instance_data->position.w = light->radius();
		light_instance_data->color = light->GetColor();
		light_instance_data->direction = light->owner_rotation().to_mat4()[2].ToVec3();

		int i = 0;
		for (; i < 4; i++) {
			if (i >= light_matrices.size()) {
				light_instance_data->light_matrices[i] = Mat4::identity;
			} else {
				light_instance_data->light_matrices[i] = light_matrices[i];
			}
		}

		light_instance_data->cascade_distances = cascade_distances;
		light_instance_data->player_inv_view_proj = (*(Mat4*)&inv_vp_matrix);
		light_instance_data->player_camera_position = Vec4(m_camera_position, 1);

		m_command_list->SetGraphicsRoot32BitConstant(3, (u32)m_frame_draws, 0);

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

	m_command_list->SetGraphicsRootSignature(m_root_signature.Get());

	ID3D12DescriptorHeap* ppHeaps[] = { m_cbv_srv_descriptor_heap.Get(), m_sampler_descriptor_heap.Get() };
	m_command_list->SetDescriptorHeaps(_countof(ppHeaps), ppHeaps);
	m_command_list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	CD3DX12_GPU_DESCRIPTOR_HANDLE cbvSrvHandle(m_cbv_srv_descriptor_heap->GetGPUDescriptorHandleForHeapStart(), 0, descriptor_size);
	m_command_list->SetGraphicsRootDescriptorTable(0, cbvSrvHandle);
	m_command_list->SetGraphicsRootDescriptorTable(1, m_sampler_descriptor_heap->GetGPUDescriptorHandleForHeapStart());

	CD3DX12_GPU_DESCRIPTOR_HANDLE bone_descriptor_handle(m_cbv_srv_descriptor_heap->GetGPUDescriptorHandleForHeapStart(), g_bone_array_descriptor_start, descriptor_size);
	m_command_list->SetGraphicsRootDescriptorTable(4, bone_descriptor_handle);

	g_global_uniform->view_projection = vp_matrix;
	g_global_uniform->inv_view_proj = (*(Mat4*)&inv_vp_matrix);
	g_global_uniform->camera_pos = Vec4(m_camera_position, 1.f);
	g_global_uniform->view = view_matrix;
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
				((RenderPipeline_Dx12*)get_pipeline("destructible_base"))) :
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
			continue;
		}

		const Texture* color_tex = nullptr;
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
					if (color_tex) {
						scene_buffer.texture_list[0] = (f32)color_tex->get_texture_id();
					}
				}

				if (param.param_name() == kbString("color_tex_2")) {
					color_tex = param.texture();
					if (color_tex) {
						scene_buffer.texture_list[1] = (f32)color_tex->get_texture_id();
					}
				}

				if (param.param_name() == kbString("color_tex_3")) {
					color_tex = param.texture();
					if (color_tex) {
						scene_buffer.texture_list[2] = (f32)color_tex->get_texture_id();
					}
				}

				if (param.param_name() == kbString("color_tex_4")) {
					color_tex = param.texture();
					if (color_tex) {
						scene_buffer.texture_list[3] = (f32)color_tex->get_texture_id();
					}
				}

				if (param.param_name() == "time") {
					time = param.vector();
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
		CD3DX12_GPU_DESCRIPTOR_HANDLE gpu_handle(m_cbv_srv_descriptor_heap->GetGPUDescriptorHandleForHeapStart(), g_srv_descriptor_start, descriptor_size);
		m_command_list->SetGraphicsRootDescriptorTable(2, gpu_handle);
	//	m_command_list->DrawIndexedInstanced(index_buffer->num_elements(), 1, 0, 0, 0);
		m_frame_draws++;
	}
}

/// Renderer_Dx12::present
void Renderer_Dx12::present() {
	/// Indicate that the back buffer will now be used to present.
	auto res_barrier = CD3DX12_RESOURCE_BARRIER::Transition(m_swap_chain_rtv[m_frame_index].Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
	m_command_list->ResourceBarrier(1, &res_barrier);
	blk::error_check(m_command_list->Close());

	// Execute command lists
	ID3D12CommandList* const command_lists[] = { m_command_list.Get() };
	m_queue->ExecuteCommandLists(_countof(command_lists), command_lists);

	wait_on_fence();

	// Present
	blk::error_check(m_swap_chain->Present(1, 0));

	// Wait for previous frame (todo)
	//wait_on_fence();

	m_frame_index = m_swap_chain->GetCurrentBackBufferIndex();
}

/// Renderer_Dx12::create_pipeline
RenderPipeline* Renderer_Dx12::create_pipeline(const string& friendly_name, const string& relative_shader_path) {
	string absolute_shader_path = "./";
	u32 num_iterations = 0;
	while (fs::exists(absolute_shader_path + "/blk_engine/") == false && num_iterations < 10) {
		absolute_shader_path += "../";
		num_iterations++;
	}
	absolute_shader_path = absolute_shader_path + relative_shader_path;

	const bool is_sprite_particle = (friendly_name.find("sprite_particle") != absolute_shader_path.npos);
	const bool is_light = (friendly_name.find("_light") != absolute_shader_path.npos);
	const bool is_shadow_proj = blk::std_contains(friendly_name, "shadow_projection");
	const bool is_shadow_depth = blk::std_contains(friendly_name, "shadow_depth");
	const bool is_point_cloud = blk::std_contains(friendly_name, "gaussian_splat");

	u32 blend_type = 0;
	if (friendly_name.find("_blend") != friendly_name.npos) {
		blend_type = 2;
	} else if (friendly_name.find("_add") != friendly_name.npos || is_light) {
		blend_type = 1;
	}

	Microsoft::WRL::ComPtr<ID3DBlob> errors;

	wstring pipeline_path;
	WStringFromString(pipeline_path, absolute_shader_path);

	// Initialize DirectX Shader Compiler (DXC)
	static ComPtr<IDxcCompiler3> dxcCompiler;
	static ComPtr<IDxcUtils> dxcUtils;
	static ComPtr<IDxcIncludeHandler> includeHandler;

	if (!dxcCompiler) {
		blk::error_check(DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&dxcUtils)), "Failed to create DXC Utils instance.");

		blk::error_check(DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&dxcCompiler)), "Failed to create DXC Compiler instance.");

		blk::error_check(dxcUtils->CreateDefaultIncludeHandler(&includeHandler), "Failed to create DXC Include Handler.");
	}

	std::vector<char> vertex_shader;
	std::vector<char> pixel_shader;

	const auto shader_text_write_time = fs::last_write_time(absolute_shader_path);

	// Compile vertex shader
	std::filesystem::path shader_output_file(absolute_shader_path.c_str());
	shader_output_file.replace_extension(".vso");

	if (fs::exists(shader_output_file) && fs::last_write_time(shader_output_file) > shader_text_write_time) {
		std::ifstream shader_bin(shader_output_file, std::ios::binary | std::ios::ate);
		if (!shader_bin.is_open()) {
			blk::warn("Renderer_Dx12::create_pipeline() - %s", shader_output_file.c_str());
			return nullptr;
		}

		std::streamsize file_size = shader_bin.tellg();
		shader_bin.seekg(0, std::ios::beg);

		// Read the file into a buffer
		vertex_shader.resize(file_size);
		shader_bin.read(vertex_shader.data(), file_size);
		shader_bin.close();
	} else {
		ifstream file(absolute_shader_path);
		if (!file.is_open()) {
			throw std::runtime_error("Failed to open HLSL file");
		}

		std::stringstream buffer;
		buffer << file.rdbuf();
		file.close();

		blk::log("Shader file is %s\n", friendly_name.c_str());
		std::string content = buffer.str();
		for (unsigned char c : content) {
			if (c > 127) {
				throw std::runtime_error("File contains non-UTF-8 characters. Ensure it's saved as UTF-8.");
			}
		}

		DxcBuffer sourceBuffer = {};
		sourceBuffer.Ptr = content.c_str();
		sourceBuffer.Size = content.size();
		sourceBuffer.Encoding = DXC_CP_ACP;

		std::vector<LPCWSTR> arguments = {
			L"-E", L"vertex_shader",
			L"-T", L"vs_6_0",
			L"-Zi", L"-Od",
			L"-Qembed_debug",
		};

		ComPtr<IDxcResult> result;
		if (FAILED(dxcCompiler->Compile(&sourceBuffer, arguments.data(), (UINT)arguments.size(), includeHandler.Get(), IID_PPV_ARGS(&result)))) {
			throw std::runtime_error("Shader compilation failed.");
		}

		ComPtr<IDxcBlobUtf8> errors;
		ComPtr<IDxcBlobUtf16> unused_blob;
		HRESULT hr = result->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&errors), &unused_blob);
		blk::error_check(!FAILED(hr) && (errors == nullptr || errors->GetStringLength() == 0), "Shader compilation errors: %s\n", std::string(errors->GetStringPointer()).c_str());

		ComPtr<ID3DBlob> shader_blob;
		if (FAILED(result->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&shader_blob), nullptr))) {
			throw std::runtime_error("Failed to retrieve compiled shader.");
		}

		const size_t blob_byte_size = shader_blob->GetBufferSize();
		vertex_shader.resize(blob_byte_size);
		std::memcpy(vertex_shader.data(), shader_blob->GetBufferPointer(), blob_byte_size);

		// Write binary to file
		std::ofstream ofs(shader_output_file, std::ios::binary);
		ofs.write(reinterpret_cast<const char*>(shader_blob->GetBufferPointer()), shader_blob->GetBufferSize());
		ofs.close();
	}

	if (is_shadow_depth) {
		shader_output_file.replace_extension(".shadow.pso");
	} else {
		shader_output_file.replace_extension(".color.pso");
	}

	if (fs::exists(shader_output_file) && fs::last_write_time(shader_output_file) > shader_text_write_time) {
		std::ifstream shader_bin(shader_output_file, std::ios::binary | std::ios::ate);
		if (!shader_bin.is_open()) {
			blk::warn("Renderer_Dx12::create_pipeline() - %s", shader_output_file.c_str());
			return nullptr;
		}

		std::streamsize file_size = shader_bin.tellg();
		shader_bin.seekg(0, std::ios::beg);

		// Read the file into a buffer
		pixel_shader.resize(file_size);
		shader_bin.read(pixel_shader.data(), file_size);
		shader_bin.close();
	} else {
		ifstream file(absolute_shader_path);
		if (!file.is_open()) {
			throw std::runtime_error("Failed to open HLSL file");
		}

		std::stringstream buffer;
		buffer << file.rdbuf();
		file.close();

		blk::log("Shader file is %s\n", friendly_name.c_str());
		std::string content = buffer.str();
		for (unsigned char c : content) {
			if (c > 127) {
				throw std::runtime_error("File contains non-UTF-8 characters. Ensure it's saved as UTF-8.");
			}
		}

		DxcBuffer sourceBuffer = {};
		sourceBuffer.Ptr = content.c_str();
		sourceBuffer.Size = content.size();
		sourceBuffer.Encoding = DXC_CP_ACP;

		wstring entry_point;
		if (is_shadow_depth) {
			entry_point = L"shadow_depth_ps";
		} else {
			entry_point = L"pixel_shader";
		}
		// Prepare shader compilation arguments
		std::vector<LPCWSTR> arguments;
		arguments.push_back(L"-E");
		arguments.push_back(entry_point.c_str());
		arguments.push_back(L"-T");
		arguments.push_back(L"ps_6_0");
		arguments.push_back(L"-Zi");
		arguments.push_back(L"-Od");
		arguments.push_back(L"-Qembed_debug");

		// Compile the shader
		ComPtr<IDxcResult> result;
		blk::error_check(
			dxcCompiler->Compile(
				&sourceBuffer,
				arguments.data(),
				(u32)arguments.size(),
				includeHandler.Get(),
				IID_PPV_ARGS(&result)
			)
			, "Shader compilation failed."
		);

		// Verify the compilation status
		ComPtr<IDxcBlobUtf8> errors;
		result->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&errors), nullptr);
		blk::error_check(errors == nullptr || errors->GetStringLength() == 0, "Shader compilation errors: %s\n", std::string(errors->GetStringPointer()).c_str());

		ComPtr<ID3DBlob> shader_blob;
		if (FAILED(result->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&shader_blob), nullptr))) {
			throw std::runtime_error("Failed to retrieve compiled shader.");
		}

		const size_t blob_byte_size = shader_blob->GetBufferSize();
		pixel_shader.resize(blob_byte_size);
		std::memcpy(pixel_shader.data(), shader_blob->GetBufferPointer(), blob_byte_size);

		// Write binary to file
		std::ofstream ofs(shader_output_file, std::ios::binary);
		ofs.write(reinterpret_cast<const char*>(shader_blob->GetBufferPointer()), shader_blob->GetBufferSize());
		ofs.close();
	}
	DXGI_FORMAT depth_stencil_fmt = DXGI_FORMAT_D24_UNORM_S8_UINT;
	if (is_shadow_depth) {
		depth_stencil_fmt = DXGI_FORMAT_D32_FLOAT;
	}

	vector<D3D12_INPUT_ELEMENT_DESC> input_element_desc;

	ComPtr<ID3D12RootSignature> signature = m_root_signature;
	D3D12_PRIMITIVE_TOPOLOGY_TYPE topology_type = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;

	if (is_point_cloud) {
		signature = m_point_cloud_signature;
	} else if (is_light || is_shadow_proj) {
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

	D3D12_INPUT_LAYOUT_DESC input_layout = { input_element_desc.data(), (u32)input_element_desc.size() };
	if (is_point_cloud) {
		input_layout = { nullptr, 0 };
	}
	auto raster = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	raster.CullMode = D3D12_CULL_MODE_NONE;
	if (is_shadow_depth) {
		raster.CullMode = D3D12_CULL_MODE_BACK;
	}

	// Describe and create the graphics pipeline state object (PSO).
	D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
	psoDesc.InputLayout = input_layout;
	psoDesc.pRootSignature = signature.Get();
	psoDesc.VS = CD3DX12_SHADER_BYTECODE(vertex_shader.data(), vertex_shader.size());
	psoDesc.PS = CD3DX12_SHADER_BYTECODE(pixel_shader.data(), pixel_shader.size());
	psoDesc.RasterizerState = raster;
	psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT); // a default depth stencil state
	psoDesc.DSVFormat = depth_stencil_fmt;
	psoDesc.SampleMask = UINT_MAX;
	psoDesc.PrimitiveTopologyType = topology_type;
	psoDesc.SampleDesc.Count = 1;

	if (is_light || is_shadow_proj) {
		psoDesc.DepthStencilState.DepthEnable = false;
	}

	if (is_point_cloud) {
		D3D12_BLEND_DESC blend_desc = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
		blend_desc.RenderTarget[0].BlendEnable = true;
		blend_desc.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
		blend_desc.RenderTarget[0].SrcBlend = D3D12_BLEND::D3D12_BLEND_ONE;
		blend_desc.RenderTarget[0].DestBlend = D3D12_BLEND::D3D12_BLEND_INV_SRC_ALPHA;
		blend_desc.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_SRC_ALPHA;
		blend_desc.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
		blend_desc.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
		blend_desc.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

		psoDesc.BlendState = blend_desc;
		psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
	} else if (blend_type == 1) {
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

	if (is_shadow_depth) {
		psoDesc.NumRenderTargets = 1;
		psoDesc.RTVFormats[0] = DXGI_FORMAT_R32_FLOAT;
	} else if (!is_light && blend_type == 0) {
		psoDesc.NumRenderTargets = 4;
		psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
		psoDesc.RTVFormats[1] = DXGI_FORMAT_R8G8B8A8_UNORM;
		psoDesc.RTVFormats[2] = DXGI_FORMAT_R8G8B8A8_UNORM;
		psoDesc.RTVFormats[3] = DXGI_FORMAT_R32_FLOAT;
	} else {
		psoDesc.NumRenderTargets = 1;
		psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
	}

	RenderPipeline_Dx12* const pipe = new RenderPipeline_Dx12();
	blk::error_check(m_device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&pipe->m_pipeline_state)));

	return (RenderPipeline*)pipe;
}

/// Renderer_Dx12::load_texture
u32 Renderer_Dx12::load_texture(const std::string& path, LoadTextureParams& params) {
	wstring texture_path;
	WStringFromString(texture_path, path);
	if (!texture_path.ends_with(L".dds")) {
		return -1;
	}

	blk::error_check(m_command_allocator->Reset());
	blk::error_check(m_command_list->Reset(m_command_allocator.Get(), nullptr));

	ComPtr<ID3D12Resource> upload_resource;
	// Load texture params
	ComPtr<ID3D12Resource> tex;
	{
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

	params.width = (u32)tex.Get()->GetDesc().Width;
	params.height = tex.Get()->GetDesc().Height;

	static u32 tex_count = ERenderTarget::Count * Renderer::max_frames();
	const auto CBV_SRV_DESCRIPTOR_SIZE = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	static CD3DX12_CPU_DESCRIPTOR_HANDLE texHandle(m_cbv_srv_descriptor_heap->GetCPUDescriptorHandleForHeapStart(), g_max_scene_constants + g_max_scene_bone_arrays + tex_count, CBV_SRV_DESCRIPTOR_SIZE);

	{
		D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc = {};
		srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

		// TODO: HACK
		if (path.find("height_map") != path.npos) {
			srv_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		} else if (path.find("smoke") != path.npos ||
			path.find("green.dds") != path.npos ||
			path.find("pink.dds") != path.npos ||
			path.find("light_blue.dds") != path.npos ||
			path.find("grass.dds") != path.npos ||
			path.find("splat_map") != path.npos) {
			srv_desc.Format = DXGI_FORMAT_BC3_UNORM;
		} else {
			srv_desc.Format = DXGI_FORMAT_BC1_UNORM;
		}

		srv_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		srv_desc.Texture2D.MipLevels = 1;

		m_device->CreateShaderResourceView(m_textures.back().Get(), &srv_desc, texHandle);
		texHandle.Offset(CBV_SRV_DESCRIPTOR_SIZE);
	}

	ComPtr<ID3D12Resource> staging_buffer;

	if (params.cpu_accessible) {
		auto tex_barrier = CD3DX12_RESOURCE_BARRIER::Transition(tex.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_SOURCE);
		m_command_list->ResourceBarrier(1, &tex_barrier);

		const size_t num_pixels = tex->GetDesc().Width * tex->GetDesc().Height;

		D3D12_RESOURCE_DESC buffer_desc = {};
		buffer_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
		buffer_desc.Width = num_pixels * sizeof(u8[4]);
		buffer_desc.Height = 1;	// Height and Depth must be 1 when using D3D12_RESOURCE_DIMENSION_BUFFER
		buffer_desc.DepthOrArraySize = 1;
		buffer_desc.MipLevels = 1;
		buffer_desc.SampleDesc.Count = 1;
		buffer_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
		buffer_desc.Flags = D3D12_RESOURCE_FLAG_NONE;

		D3D12_HEAP_PROPERTIES heap_props = {};
		heap_props.Type = D3D12_HEAP_TYPE_READBACK;
		blk::error_check(m_device->CreateCommittedResource(
			&heap_props,
			D3D12_HEAP_FLAG_NONE,
			&buffer_desc,
			D3D12_RESOURCE_STATE_COPY_DEST,
			nullptr,
			IID_PPV_ARGS(&staging_buffer)
		));

		D3D12_TEXTURE_COPY_LOCATION dst_location = {};
		dst_location.pResource = staging_buffer.Get();
		dst_location.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
		auto desc = tex.Get()->GetDesc();
		m_device->GetCopyableFootprints(&desc, 0, 1, 0, &dst_location.PlacedFootprint, nullptr, nullptr, nullptr);

		D3D12_TEXTURE_COPY_LOCATION src_location = {};
		src_location.pResource = tex.Get();
		src_location.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
		src_location.SubresourceIndex = 0;

		m_command_list->CopyTextureRegion(&dst_location, 0, 0, 0, &src_location, nullptr);

		{
			blk::error_check(m_command_list->Close());
			ID3D12CommandList* command_lists[] = { m_command_list.Get() };
			m_queue->ExecuteCommandLists(_countof(command_lists), command_lists);
			wait_on_fence();
			blk::error_check(m_command_allocator->Reset());
			blk::error_check(m_command_list->Reset(m_command_allocator.Get(), nullptr));
		}

		void* mapped_data = nullptr;
		blk::error_check(staging_buffer->Map(0, nullptr, &mapped_data));
		staging_buffer->Unmap(0, nullptr);
		const u8* texture_data = static_cast<u8*>(mapped_data);

		// Write to cpu accessible memory
		for (size_t i = 0; i < num_pixels; i++) {
			const size_t tex_idx = i * 4;
			params.texture_data->push_back(
				Vec4(
					texture_data[tex_idx + 0] / 255.f,
					texture_data[tex_idx + 1] / 255.f,
					texture_data[tex_idx + 2] / 255.f,
					texture_data[tex_idx + 3] / 255.f
				)
			);
		}

		tex_barrier = CD3DX12_RESOURCE_BARRIER::Transition(tex.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
		m_command_list->ResourceBarrier(1, &tex_barrier);
	}

	// Close the command list and execute it to begin the initial GPU setup.
	blk::error_check(m_command_list->Close());
	ID3D12CommandList* ppCommandLists[] = { m_command_list.Get() };
	m_queue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);

	wait_on_fence();
	return tex_count++;
}

/// Renderer_Dx12::init_default_pipelines
void Renderer_Dx12::init_default_pipelines() {
	// Create DXC Compiler
	blk::error_check(
		DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&m_dxc_utils)),
		"Renderer_Dx12::init_default_pipelines() - Failed to create m_dxc_utils"
	);

	blk::error_check(
		DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&m_dxc_compiler)),
		"Renderer_Dx12::init_default_pipelines() - Failed to create m_dxc_compiler"
	);

	blk::error_check(
		m_dxc_utils->CreateDefaultIncludeHandler(&m_dxc_include_handler),
		"Renderer_Dx12::init_default_pipelines() - Failed to create m_dxc_include_handler"
	);

	auto pipe = (RenderPipeline_Dx12*)load_pipeline("static_model_base", "/blk_engine/assets/shaders/static_model.shader");
	pipe = (RenderPipeline_Dx12*)load_pipeline("static_model_shadow_depth", "/blk_engine/assets/shaders/static_model.shader");

	pipe = (RenderPipeline_Dx12*)load_pipeline("skinned_base", "/blk_engine/assets/shaders/skinned_model.shader");
	pipe = (RenderPipeline_Dx12*)load_pipeline("skinned_shadow_depth", "/blk_engine/assets/shaders/skinned_model.shader");

	pipe = (RenderPipeline_Dx12*)load_pipeline("sprite_particle_blend", "/blk_engine/assets/shaders/sprite_particle.shader");
	pipe = (RenderPipeline_Dx12*)load_pipeline("sprite_particle_add", "/blk_engine/assets/shaders/sprite_particle.shader");
	pipe = (RenderPipeline_Dx12*)load_pipeline("mesh_particle_add", "/blk_engine/assets/shaders/mesh_particle.shader");

	pipe = (RenderPipeline_Dx12*)load_pipeline("directional_light", "/blk_engine/assets/shaders/directional_light.shader");
	pipe = (RenderPipeline_Dx12*)load_pipeline("point_light", "/blk_engine/assets/shaders/point_light.shader");
	pipe = (RenderPipeline_Dx12*)load_pipeline("directional_shadow_projection", "/blk_engine/assets/shaders/directional_shadow.shader");

	pipe = (RenderPipeline_Dx12*)load_pipeline("terrain", "/blk_engine/assets/shaders/terrain.shader");


	pipe = (RenderPipeline_Dx12*)load_pipeline("gaussian_splat", "/blk_engine/assets/shaders/gaussian_splat.shader");
}

/// Renderer_Dx12::wait_on_fence
void Renderer_Dx12::wait_on_fence() {
	// Wait for previous frame (todo)
	const uint64_t fence = m_fence_value;
	blk::error_check(m_queue->Signal(m_fence.Get(), fence));
	m_fence_value++;

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
	auto rt_barrier = CD3DX12_RESOURCE_BARRIER::Transition(m_render_targets[ERenderTarget::ShadowDepth][m_frame_index].Get(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_DEPTH_WRITE);
	m_command_list->ResourceBarrier(1, &rt_barrier);

	const u32 shadow_buffer_start = Renderer::max_frames();
	CD3DX12_CPU_DESCRIPTOR_HANDLE dsv_handle(m_depth_stencil_heap->GetCPUDescriptorHandleForHeapStart(), shadow_buffer_start + m_frame_index, m_depth_target_descriptor_size);
	m_command_list->OMSetRenderTargets(0, nullptr, false, &dsv_handle);

	m_command_list->ClearDepthStencilView(dsv_handle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

	ID3D12DescriptorHeap* ppHeaps[] = { m_cbv_srv_descriptor_heap.Get(), m_sampler_descriptor_heap.Get() };
	m_command_list->SetDescriptorHeaps(_countof(ppHeaps), ppHeaps);
	m_command_list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	auto descriptor_size = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	CD3DX12_GPU_DESCRIPTOR_HANDLE cbvSrvHandle(m_cbv_srv_descriptor_heap->GetGPUDescriptorHandleForHeapStart(), 0, descriptor_size);
	m_command_list->SetGraphicsRootDescriptorTable(0, cbvSrvHandle);
	m_command_list->SetGraphicsRootDescriptorTable(1, m_sampler_descriptor_heap->GetGPUDescriptorHandleForHeapStart());

	CD3DX12_GPU_DESCRIPTOR_HANDLE bone_descriptor_handle(m_cbv_srv_descriptor_heap->GetGPUDescriptorHandleForHeapStart(), g_bone_array_descriptor_start, descriptor_size);
	m_command_list->SetGraphicsRootDescriptorTable(4, bone_descriptor_handle);

	// Cascade loop here
	const auto& cascade_dists = dir_light->cascade_start_distances();

	const auto scisscor_rect = CD3DX12_RECT(0, 0, g_shadow_tex_dimensions, g_shadow_tex_dimensions);
	m_command_list->RSSetScissorRects(1, &scisscor_rect);

	const u32 half_shadow_dim = g_shadow_tex_dimensions >> 1;
	for (u32 i = 0; i < 4 && i < cascade_dists.size(); i++) {
		cascade_distances[i] = cascade_dists[i];

		D3D12_VIEWPORT viewport = {};
		viewport.TopLeftX = (f32)((i % 2) * half_shadow_dim);
		viewport.TopLeftY = (f32)((i / 2) * half_shadow_dim);
		viewport.Width = half_shadow_dim;
		viewport.Height = half_shadow_dim;
		viewport.MinDepth = 0.0f;
		viewport.MaxDepth = 1.0f;
		m_command_list->RSSetViewports(1, &viewport);

		const float prev_cascade_dist = (i == 0) ? (0.0f) : (cascade_dists[i] - 1);
		const Vec3 look_at_point = m_camera_position + cam_dir * (prev_cascade_dist + (cascade_dists[i] - prev_cascade_dist) * 0.5f);
		const float half_fov = g_fov * 0.5f;
		const float dist_to_corner = cascade_dists[i] / cos(half_fov);
		Vec3 corner_vert = m_camera_position + dist_to_corner * ul;
		const float bounds_len = (look_at_point - corner_vert).length();

		// Light matrices
		const Vec3 light_dir = dir_light->owner_rotation().to_mat4()[2].ToVec3();
		const Mat4 light_view = Mat4::look_at(look_at_point + light_dir * bounds_len * 10.f, look_at_point, Vec3(0.0f, 1.0f, 0.0f));
		const Mat4 light_view_proj = light_view * Mat4::ortho_lh(bounds_len * 2.0f, bounds_len * 2.0f, 10.0f, bounds_len * 40.0f);

		// Fix swimming edges by projecting the zero vec into the shadow map and finding the delta to the nearest texel
		// Shifting all pixels by this amount keeps them aligned to the shadow map's texels instead of sliding around.
		// See Cascaded Shadow Maps in the Black Engine: https://www.benny-wilson.com/blog/game-development/dynamic-shadows-in-the-black-engine/
		const f32 texel_size = 2.0f / (g_shadow_tex_dimensions * 0.5f);
		Vec4 proj_center(0.0f, 0.0f, 0.0f, 1.0f);
		proj_center = proj_center.transform_point(light_view_proj, true);

		const float fracX = fmod(proj_center.x, texel_size);
		const float fracY = fmod(proj_center.y, texel_size);

		Mat4 offset;
		offset.make_identity();
		offset[3][0] = -fracX;
		offset[3][1] = -fracY;

		Mat4 texture_matrix;
		texture_matrix.make_identity();
		texture_matrix[0].x = 0.5f;
		texture_matrix[1].y = -0.5f;
		texture_matrix[3].x = 0.5f + (0.5f / g_shadow_tex_dimensions);
		texture_matrix[3].y = 0.5f + (0.5f / g_shadow_tex_dimensions);

		const Mat4 cascade_mat = light_view_proj * offset;

		light_matrices.push_back(cascade_mat * texture_matrix);

		for (auto& render_comp : this->render_components()) {
			RenderBuffer_Dx12* vertex_buffer = nullptr;
			RenderBuffer_Dx12* index_buffer = nullptr;
			const kbModel* model = nullptr;

			auto& scene_buffer = g_scene_buffers[m_frame_draws];

			if (render_comp->render_pass() != ERenderPass::RP_Lighting) {
				continue;
			}

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

				RenderPipeline_Dx12* const pipe = ((RenderPipeline_Dx12*)get_pipeline("skinned_shadow_depth"));

				m_command_list->SetPipelineState(pipe->m_pipeline_state.Get());

				vertex_buffer = (RenderBuffer_Dx12*)(model->m_vertex_buffer);
				index_buffer = (RenderBuffer_Dx12*)(model->m_index_buffer);
				const auto vertex_buf_view = vertex_buffer->vertex_buffer_view();
				m_command_list->IASetVertexBuffers(0, 1, &vertex_buf_view);

				const auto index_buf_view = index_buffer->index_buffer_view();
				m_command_list->IASetIndexBuffer(&index_buf_view);

				const auto& bone_list = skel->GetFinalBoneMatrices();

				BoneInstanceData& bone_data = g_bone_array_buffers[m_bone_draws];
				for (int i = 0; i < bone_list.size() && i < 128; i++) {
					bone_data.bones[i].make_identity();
					bone_data.bones[i][0] = bone_list[i].GetAxis(0);
					bone_data.bones[i][1] = bone_list[i].GetAxis(1);
					bone_data.bones[i][2] = bone_list[i].GetAxis(2);
					bone_data.bones[i][3] = bone_list[i].GetAxis(3);

					bone_data.bones[i][0].w = 0;
					bone_data.bones[i][1].w = 0;
					bone_data.bones[i][2].w = 0;
				}
				m_command_list->SetGraphicsRoot32BitConstant(5, (u32)m_bone_draws, 0);
				m_bone_draws++;
			} else if (render_comp->IsA(ParticleComponent::GetType())) {
				continue;
			} else {
				continue;
			}

			const Texture* color_tex = nullptr;
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

			CD3DX12_GPU_DESCRIPTOR_HANDLE gpu_handle(m_cbv_srv_descriptor_heap->GetGPUDescriptorHandleForHeapStart(), g_srv_descriptor_start, descriptor_size);
			if (color_tex != nullptr) {
				gpu_handle.Offset(descriptor_size * color_tex->get_texture_id());
			}

			m_command_list->SetGraphicsRootDescriptorTable(2, gpu_handle);
		//	m_command_list->DrawIndexedInstanced(index_buffer->num_elements(), 1, 0, 0, 0);
			m_frame_draws++;
		}
	}

	rt_barrier = CD3DX12_RESOURCE_BARRIER::Transition(m_render_targets[ERenderTarget::ShadowDepth][m_frame_index].Get(), D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_PRESENT);
	m_command_list->ResourceBarrier(1, &rt_barrier);

	// Project Shadows
	m_command_list->RSSetViewports(1, &m_view_port);
	m_command_list->RSSetScissorRects(1, &m_scissor_rect);

	// Indicate that the back buffer will be used as a render target.
	rt_barrier = CD3DX12_RESOURCE_BARRIER::Transition(m_render_targets[ERenderTarget::Lighting][m_frame_index].Get(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
	m_command_list->ResourceBarrier(1, &rt_barrier);

	// Set Lighting Buffer
	const u32 gbuffer_start = Renderer::max_frames() + (ERenderTarget::Count - 1) * m_frame_index;
	CD3DX12_CPU_DESCRIPTOR_HANDLE rtv_handle(m_rtv_heap->GetCPUDescriptorHandleForHeapStart(), gbuffer_start + Lighting, m_rtv_descriptor_size);
	m_command_list->OMSetRenderTargets(1, &rtv_handle, false, nullptr);

	const float clear_color[] = { 0.0f, 0.0f, 0.f, 0.0f };
	m_command_list->ClearRenderTargetView(rtv_handle, clear_color, 0, nullptr);

	{
		RenderPipeline_Dx12* pipe = (RenderPipeline_Dx12*)get_pipeline("directional_shadow_projection");

		m_command_list->SetPipelineState(pipe->m_pipeline_state.Get());
		m_command_list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		m_command_list->IASetVertexBuffers(0, 1, &m_quad_vb_view);

		// Texture
		auto descriptor_size = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
		CD3DX12_GPU_DESCRIPTOR_HANDLE gpu_handle(m_cbv_srv_descriptor_heap->GetGPUDescriptorHandleForHeapStart(), g_srv_descriptor_start, descriptor_size);
		m_command_list->SetGraphicsRootDescriptorTable(2, gpu_handle);

		LightInstanceData* const light_instance_data = (LightInstanceData*)&g_scene_buffers[m_frame_draws];
		light_instance_data->position = dir_light->owner_position();
		light_instance_data->position.w = dir_light->radius();
		light_instance_data->color = dir_light->GetColor();
		light_instance_data->direction = dir_light->owner_rotation().to_mat4()[2].ToVec3();
		light_instance_data->light_matrices[0] = light_matrices[0];
		light_instance_data->light_matrices[1] = light_matrices[1];
		light_instance_data->light_matrices[2] = light_matrices[2];
		light_instance_data->light_matrices[3] = light_matrices[3];
		light_instance_data->cascade_distances = cascade_distances;
		light_instance_data->player_inv_view_proj = (*(Mat4*)&inv_vp_matrix);
		light_instance_data->player_camera_position = Vec4(m_camera_position, 1);
		m_command_list->SetGraphicsRoot32BitConstant(3, (u32)m_frame_draws, 0);

		m_command_list->DrawInstanced(6, 1, 0, 0);
		m_frame_draws++;
	}

	rt_barrier = CD3DX12_RESOURCE_BARRIER::Transition(m_render_targets[ERenderTarget::Lighting][m_frame_index].Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
	m_command_list->ResourceBarrier(1, &rt_barrier);
}

/// Renderer_Dx12::render_point_clouds
void Renderer_Dx12::render_point_clouds() {
	const Mat4 trans = Mat4::make_translation(-m_camera_position);
	Mat4 rot = m_camera_rotation.to_mat4();
	rot.transpose_self();

	Mat4 view_matrix = trans * rot;
	Mat4 vp_matrix =
		view_matrix *
		m_camera_projection;

	XMMATRIX inv_vp_matrix = XMMatrixInverse(nullptr, (*(XMMATRIX*)&vp_matrix));
	auto descriptor_size = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);


	ID3D12DescriptorHeap* ppHeaps[] = { m_point_cloud_descriptor_heap.Get() };
	m_command_list->SetDescriptorHeaps(_countof(ppHeaps), ppHeaps);

	m_command_list->SetGraphicsRootSignature(m_point_cloud_signature.Get());

	m_command_list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_POINTLIST);

	m_command_list->SetGraphicsRootConstantBufferView(0, m_scene_cbv_upload_heap->GetGPUVirtualAddress());

	CD3DX12_GPU_DESCRIPTOR_HANDLE gpu_handle(
		m_point_cloud_descriptor_heap->GetGPUDescriptorHandleForHeapStart(),
		0,
		m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV));

	m_command_list->SetGraphicsRootDescriptorTable(1, gpu_handle);

	g_global_uniform->view_projection = vp_matrix;
	g_global_uniform->inv_view_proj = (*(Mat4*)&inv_vp_matrix);
	g_global_uniform->camera_pos = Vec4(m_camera_position, 1.f);
	g_global_uniform->view = view_matrix;

	const std::vector<PointCloudData>* point_cloud = nullptr;
	GaussianSplatComponent* gaussian_splat = nullptr;

	for (auto& render_comp : render_components()) {
		if (render_comp->render_pass() != ERenderPass::RP_PreTranslucent) {
			continue;
		}
		if (render_comp->IsA(GaussianSplatComponent::GetType())) {
			gaussian_splat = (GaussianSplatComponent*)(render_comp);
			if (gaussian_splat->point_cloud() && gaussian_splat->point_cloud()->size() > 0) {
				point_cloud = gaussian_splat->point_cloud();
				break;
			}
		}
		break;
	}

	if (!point_cloud) {
		return;
	}

	if (gaussian_splat->splat_dirty()) {
		for (int i = 0; i < point_cloud->size(); i++) {
			const PointCloudData& cur_point = (*point_cloud)[i];
			g_point_cloud[i].position.set(cur_point.position.x, cur_point.position.y, cur_point.position.z, 0.f);
			g_point_cloud[i].rotation = cur_point.rotation;
			g_point_cloud[i].scale3d_opacity.set(cur_point.scale.x, cur_point.scale.y, cur_point.scale.z, cur_point.opacity);
			g_point_cloud[i].sh0.set(cur_point.sh[0].x, cur_point.sh[0].y, cur_point.sh[0].z, 0.f);
			g_point_cloud[i].sh1.set(cur_point.sh[1].x, cur_point.sh[1].y, cur_point.sh[1].z, 0.f);
			g_point_cloud[i].sh2.set(cur_point.sh[2].x, cur_point.sh[2].y, cur_point.sh[2].z, 0.f);
			g_point_cloud[i].sh3.set(cur_point.sh[3].x, cur_point.sh[3].y, cur_point.sh[3].z, 0.f);
			g_point_cloud[i].sh4.set(cur_point.sh[4].x, cur_point.sh[4].y, cur_point.sh[4].z, 0.f);
			g_point_cloud[i].sh5.set(cur_point.sh[5].x, cur_point.sh[5].y, cur_point.sh[5].z, 0.f);
			g_point_cloud[i].sh6.set(cur_point.sh[6].x, cur_point.sh[6].y, cur_point.sh[6].z, 0.f);
			g_point_cloud[i].sh7.set(cur_point.sh[7].x, cur_point.sh[7].y, cur_point.sh[7].z, 0.f);
			g_point_cloud[i].sh8.set(cur_point.sh[8].x, cur_point.sh[8].y, cur_point.sh[8].z, 0.f);
			g_point_cloud_indices[i] = i;
		}

		// Upload points
		{
			const UINT buffer_size = sizeof(PointCloudSampleInstance) * g_max_point_cloud_points;
			D3D12_SUBRESOURCE_DATA subresource_data = {};
			subresource_data.pData = g_point_cloud;
			subresource_data.RowPitch = sizeof(PointCloudSampleInstance) * g_max_point_cloud_points;
			subresource_data.SlicePitch = subresource_data.RowPitch;

			UpdateSubresources(
				m_command_list.Get(),
				m_point_cloud_default_heap.Get(),
				m_point_cloud_upload_heap.Get(),
				0, 0, 1,
				&subresource_data
			);
		}
		gaussian_splat->set_splat_dirty(false);
	}

	// Sort indices

static bool bSkipSort = false;

	if (!bSkipSort)
	{
		struct IndexedDepth {
			uint32_t index;
			float depth;
		};

		std::vector<IndexedDepth> depth_list;
		depth_list.reserve(point_cloud->size());

		for (uint32_t i = 0; i < point_cloud->size(); ++i) {
			const PointCloudData& cur_point = (*point_cloud)[i];
			Vec3 view_pos = view_matrix.transform_point(cur_point.position);
			const float view_z = view_pos.z;
			depth_list.push_back({i, view_z});
		}

		// Sort back-to-front (larger Z first)
		std::sort(depth_list.begin(), depth_list.end(),
			[](const IndexedDepth& a, const IndexedDepth& b) {
				return a.depth > b.depth;
			});

		for (size_t i = 0; i < depth_list.size(); ++i) {
			g_point_cloud_indices[i] = depth_list[i].index;
		}

		const uint32_t buffer_size = (uint32_t)(sizeof(uint32_t) * point_cloud->size());
		void* mapped_data = nullptr;
		CD3DX12_RANGE read_range(0, 0);
		m_command_list->CopyBufferRegion(
			m_point_cloud_index_default_heap.Get(), 0,
			m_point_cloud_index_upload_heap.Get(), 0,
			buffer_size);
	}

	g_global_uniform->splat_sharpen_scale_near_far.x = gaussian_splat->splat_falloff();
	g_global_uniform->splat_sharpen_scale_near_far.y = gaussian_splat->splat_scale();
	g_global_uniform->splat_sharpen_scale_near_far.z = gaussian_splat->near_clip();
	g_global_uniform->splat_sharpen_scale_near_far.w = gaussian_splat->far_clip();
	g_global_uniform->splat_contrast.x = gaussian_splat->contrast();

	auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
		m_point_cloud_default_heap.Get(),
		D3D12_RESOURCE_STATE_COPY_DEST,
		D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	m_command_list->ResourceBarrier(1, &barrier);

	RenderPipeline_Dx12* const pipe = (RenderPipeline_Dx12*)get_pipeline("gaussian_splat");
	m_command_list->SetPipelineState(pipe->m_pipeline_state.Get());

	m_command_list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	// Optional: set dummy vertex buffer if needed by IA stage
	D3D12_VERTEX_BUFFER_VIEW dummy_vbv = {};
	m_command_list->IASetVertexBuffers(0, 1, &dummy_vbv);

	m_command_list->DrawInstanced((uint32_t)point_cloud->size() * 6, 1, 0, 0);
}
