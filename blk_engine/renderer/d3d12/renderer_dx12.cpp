/// renderer_dx12.cpp
///
/// 2025 blk

#include <algorithm>
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

// Vendored Dear ImGui (docking branch).
#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx12.h"

// imgui_impl_win32.h intentionally comments this declaration out (to avoid
// forcing a <windows.h> dependency on every includer) and expects the .cpp
// that actually calls it to forward-declare it -- see that header's comment.
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

using namespace std;
namespace fs = std::filesystem;

// Scene Config
const u32 g_max_scene_constants = 512;
const u32 g_max_scene_bone_arrays = 512;
const u32 g_max_scene_srvs = 512;

const u64 g_max_point_cloud_points = 15000000;

const u32 g_bone_array_descriptor_start = g_max_scene_constants;
const u32 g_srv_descriptor_start = g_max_scene_constants + g_max_scene_bone_arrays;

// Video Config
const bool g_high_performance_adapter = true;

const u32 g_shadow_tex_dimensions = (g_high_performance_adapter) ? (4096) : (1024);

// Shader Config
// Bindless (ResourceDescriptorHeap[]) requires 6_6.
// gated by g_max_shader_model check in initialize_internal, right after the probe.
const LPCWSTR g_vertex_shader_profile = L"vs_6_6";
const LPCWSTR g_pixel_shader_profile = L"ps_6_6";
const LPCWSTR g_compute_shader_profile = L"cs_6_6";
D3D_SHADER_MODEL g_max_shader_model = (D3D_SHADER_MODEL)0;	// probed at device creation

// Cached shader blobs are keyed on source timestamp alone, so the configurations
// must not share a file - Debug and Release now emit different code from one source.
#if defined(_DEBUG)
#define BLK_SHADER_CACHE_TAG ".debug"
#else
#define BLK_SHADER_CACHE_TAG ".release"
#endif

/// append_shader_codegen_args
/// Debug keeps symbols and skips optimization so shaders stay steppable in PIX.
/// Release compiles optimized - every configuration used to compile -Zi -Od.
static void append_shader_codegen_args(std::vector<LPCWSTR>& arguments) {
#if defined(_DEBUG)
	arguments.push_back(L"-Zi");
	arguments.push_back(L"-Od");
	arguments.push_back(L"-Qembed_debug");
#else
	arguments.push_back(L"-O3");
#endif
}

/// newest_shared_include_write_time
///
/// The compiled-blob cache below only stats the shader being compiled, not
/// what it #includes -- a shared .hlsli can change without the .hlsl files
/// that include it looking stale. Blunt fix for now (no real dependency
/// tracking): if ANY .hlsli in the shaders directory is newer than a cached
/// blob, treat the blob as stale and recompile everything.
static std::filesystem::file_time_type newest_shared_include_write_time(const std::filesystem::path& shader_dir) {
	// Parenthesized to dodge the min/max macros pulled in by <Windows.h>.
	std::filesystem::file_time_type newest = (std::filesystem::file_time_type::min)();
	for (const auto& entry : std::filesystem::directory_iterator(shader_dir)) {
		if (entry.path().extension() == ".hlsli") {
			newest = (std::max)(newest, entry.last_write_time());
		}
	}
	return newest;
}

CD3DX12_HEAP_PROPERTIES g_D3D12_HEAP_TYPE_UPLOAD = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
CD3DX12_HEAP_PROPERTIES g_D3D12_HEAP_TYPE_DEFAULT = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);

PointCloudSampleInstance* g_point_cloud = nullptr;
BoneInstanceData* g_bone_array_buffers = nullptr;
SceneInstanceData* g_scene_buffers = nullptr;
u32* g_point_cloud_indices = nullptr;
GlobalUniformData* g_global_uniform = nullptr;

std::vector<Mat4> light_matrices;
Vec4 cascade_distances;

XMMATRIX& XMMATRIXFromMat4(Mat4& matrix) { return (*(XMMATRIX*)&matrix); }
Mat4& Mat4FromXMMATRIX(FXMMATRIX& matrix) { return (*(Mat4*)&matrix); }

/// ImGuiDescriptorHeapAllocator::create
void ImGuiDescriptorHeapAllocator::create(ID3D12Device* device, ID3D12DescriptorHeap* descriptor_heap) {
	const D3D12_DESCRIPTOR_HEAP_DESC desc = descriptor_heap->GetDesc();
	heap = descriptor_heap;
	descriptor_size = device->GetDescriptorHandleIncrementSize(desc.Type);
	heap_start_cpu = descriptor_heap->GetCPUDescriptorHandleForHeapStart();
	heap_start_gpu = descriptor_heap->GetGPUDescriptorHandleForHeapStart();
	free_indices.reserve((size_t)desc.NumDescriptors);
	for (int i = (int)desc.NumDescriptors - 1; i >= 0; i--) {
		free_indices.push_back(i);
	}
}

/// ImGuiDescriptorHeapAllocator::alloc
void ImGuiDescriptorHeapAllocator::alloc(D3D12_CPU_DESCRIPTOR_HANDLE* out_cpu, D3D12_GPU_DESCRIPTOR_HANDLE* out_gpu) {
	blk::error_check(!free_indices.empty(), "ImGuiDescriptorHeapAllocator - out of descriptors");
	const int index = free_indices.back();
	free_indices.pop_back();
	out_cpu->ptr = heap_start_cpu.ptr + ((size_t)index * descriptor_size);
	out_gpu->ptr = heap_start_gpu.ptr + ((UINT64)index * descriptor_size);
}

/// ImGuiDescriptorHeapAllocator::free
void ImGuiDescriptorHeapAllocator::free(D3D12_CPU_DESCRIPTOR_HANDLE cpu, D3D12_GPU_DESCRIPTOR_HANDLE gpu) {
	const int index = (int)((cpu.ptr - heap_start_cpu.ptr) / descriptor_size);
	free_indices.push_back(index);
}

/// Renderer_Dx12::handle_platform_message_internal
///
/// The editor owns a raw Win32 window whose WndProc routes everything through
/// here (kbEditor::handle_message), so this is the whole of the editor's ImGui
/// input path. Unconditional is safe: ImGui_ImplWin32_WndProcHandler no-ops
/// when there's no ImGui context, which covers the window's creation-time
/// messages, before initialize_internal has run.
bool Renderer_Dx12::handle_platform_message_internal(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
	return ImGui_ImplWin32_WndProcHandler(hwnd, msg, wparam, lparam) != 0;
}

/// Renderer_Dx12::~Renderer_Dx12
Renderer_Dx12::~Renderer_Dx12() {
	shut_down();	// function is virtual but called in ~Renderer which is UB
}

/// Renderer_Dx12::initialize_internal
void Renderer_Dx12::initialize_internal(HWND hwnd, const uint32_t frame_width, const uint32_t frame_height) {
	UINT dxgiFactoryFlags = 0;

	m_view_port = CD3DX12_VIEWPORT(0.f, 0.f, (float)frame_width, (float)frame_height);
	m_scissor_rect = CD3DX12_RECT(0, 0, frame_width, frame_height);
	m_hwnd = hwnd;

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

	// Probe the device's highest supported shader model. Bindless
	// (ResourceDescriptorHeap[]) needs 6_6, so Phase 2 must know this before it can
	// raise the profiles above - surfacing it here beats discovering it later as an
	// opaque pipeline-creation failure.
	{
		static const D3D_SHADER_MODEL candidates[] = {
			D3D_SHADER_MODEL_6_9, D3D_SHADER_MODEL_6_8, D3D_SHADER_MODEL_6_7,
			D3D_SHADER_MODEL_6_6, D3D_SHADER_MODEL_6_5, D3D_SHADER_MODEL_6_4,
			D3D_SHADER_MODEL_6_3, D3D_SHADER_MODEL_6_2, D3D_SHADER_MODEL_6_1,
			D3D_SHADER_MODEL_6_0,
		};

		// CheckFeatureSupport rejects a model the runtime is too old to know about,
		// so walk down until one is accepted.
		for (const D3D_SHADER_MODEL sm : candidates) {
			D3D12_FEATURE_DATA_SHADER_MODEL feature = { sm };
			if (SUCCEEDED(m_device->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &feature, sizeof(feature)))) {
				g_max_shader_model = feature.HighestShaderModel;
				break;
			}
		}

		if (g_max_shader_model == 0) {
			blk::warn("Renderer_Dx12 - could not determine supported shader model");
		} else {
			blk::log("Renderer_Dx12 - device supports shader model %d_%d",
				(g_max_shader_model >> 4) & 0xf, g_max_shader_model & 0xf);
		}

		blk::error_check(g_max_shader_model >= D3D_SHADER_MODEL_6_6,
			"Renderer_Dx12 - device does not support shader model 6.6, required for bindless");

		// Diagnostic only -- SM6.6 Dynamic Resources (ResourceDescriptorHeap[])
		// work across resource binding tiers per the DirectX spec, but this
		// hasn't been confirmed against every target machine/driver, so log it
		// rather than assume.
		D3D12_FEATURE_DATA_D3D12_OPTIONS options = {};
		if (SUCCEEDED(m_device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS, &options, sizeof(options)))) {
			blk::log("Renderer_Dx12 - device resource binding tier %d", (int)options.ResourceBindingTier);
		}
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

	// Dedicated ImGui SRV heap, separate from m_cbv_srv_descriptor_heap --
	// see ImGuiDescriptorHeapAllocator's doc comment in renderer_dx12.h.
	D3D12_DESCRIPTOR_HEAP_DESC imgui_srv_heap_desc = {};
	imgui_srv_heap_desc.NumDescriptors = 64;
	imgui_srv_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	imgui_srv_heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	blk::error_check(m_device->CreateDescriptorHeap(&imgui_srv_heap_desc, IID_PPV_ARGS(&m_imgui_srv_heap)));
	m_imgui_srv_heap->SetName(L"Renderer_Dx12::m_imgui_srv_heap");
	m_imgui_srv_heap_allocator.create(m_device.Get(), m_imgui_srv_heap.Get());

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
		// Fixed for the process lifetime -- material shaders add this to their
		// (still table-relative) texture_list id for a bindless ResourceDescriptorHeap[] index.
		g_global_uniform->srv_heap_base = Vec4((f32)g_srv_descriptor_start, 0.f, 0.f, 0.f);

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
			rt.Get()->SetName((L"Renderer_Dx12::Color_" + std::to_wstring(frame_idx)).c_str());

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
			rt.Get()->SetName((L"Renderer_Dx12::Normal_" + std::to_wstring(frame_idx)).c_str());

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
			rt.Get()->SetName((L"Renderer_Dx12::Specular_" + std::to_wstring(frame_idx)).c_str());

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
			rt.Get()->SetName((L"Renderer_Dx12::SceneDepth_" + std::to_wstring(frame_idx)).c_str());

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
			rt.Get()->SetName((L"Renderer_Dx12::Lighting_" + std::to_wstring(frame_idx)).c_str());

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

			rt.Get()->SetName((L"Renderer_Dx12::ShadowDepth_" + std::to_wstring(frame_idx)).c_str());

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

		// Scene Color -- full-screen lit output that lights/point-clouds/
		// translucency render into; the post-process pass reads this instead
		// of those passes writing the swapchain backbuffer directly. Must
		// stay after ShadowDepth in creation order (see the ERenderTarget
		// comment) so shader-hardcoded gbuffer_textures indices stay valid.
		{
			const auto format = DXGI_FORMAT_R8G8B8A8_UNORM;
			const D3D12_RESOURCE_DESC desc = CD3DX12_RESOURCE_DESC::Tex2D(format,
				(u64)m_frame_width,
				(u32)m_frame_height,
				1, 1, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET);
			const D3D12_CLEAR_VALUE clear_value = { format, {0.f, 0.f, 0.f, 0.f} };

			auto& rt = m_render_targets[ERenderTarget::SceneColor][frame_idx];
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
			rt.Get()->SetName((L"Renderer_Dx12::SceneColor_" + std::to_wstring(frame_idx)).c_str());

			m_device->CreateRenderTargetView(rt.Get(), nullptr, rtv_handle);
			rtv_handle.Offset(1, m_rtv_descriptor_size);

			m_device->CreateShaderResourceView(rt.Get(), nullptr, scene_cbv_srv_handle);
			scene_cbv_srv_handle.Offset(CBV_SRV_DESCRIPTOR_SIZE);

			auto rt_barrier = CD3DX12_RESOURCE_BARRIER::Transition(rt.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
			m_command_list->ResourceBarrier(1, &rt_barrier);
		}

		// Entity Id -- per-pixel owning entity for viewport click-to-select,
		// written as the gbuffer's 5th target. Deliberately the LAST block in
		// this loop: that puts its SRV at index 7, past every index the light
		// and shadow shaders hardcode, while its RTV still lands at slot 6 to
		// match its enum value (see the ERenderTarget comment).
		//
		// R32_FLOAT rather than R32_UINT so it needs no separate PSO/format
		// branch from SceneDepth's identical format, and because entity ids are
		// integers well inside float32's exactly-representable range. Cleared
		// to -1: entity id 0 is a real entity, so zero can't be the "no entity
		// here" sentinel, and the clear value must match the
		// ClearRenderTargetView in render_gbuffer_internal or the driver's fast
		// clear path is lost.
		{
			const auto format = DXGI_FORMAT_R32_FLOAT;
			const D3D12_CLEAR_VALUE clear_value = { format, {-1.f, 0.f, 0.f, 0.f} };
			const D3D12_RESOURCE_DESC desc = CD3DX12_RESOURCE_DESC::Tex2D(format,
				(u64)m_frame_width,
				(u32)m_frame_height,
				1, 1, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET);

			auto& rt = m_render_targets[ERenderTarget::EntityId][frame_idx];
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
			rt.Get()->SetName((L"Renderer_Dx12::EntityId_" + std::to_wstring(frame_idx)).c_str());

			m_device->CreateRenderTargetView(rt.Get(), nullptr, rtv_handle);
			rtv_handle.Offset(1, m_rtv_descriptor_size);

			m_device->CreateShaderResourceView(rt.Get(), nullptr, scene_cbv_srv_handle);
			scene_cbv_srv_handle.Offset(CBV_SRV_DESCRIPTOR_SIZE);

			auto rt_barrier = CD3DX12_RESOURCE_BARRIER::Transition(rt.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
			m_command_list->ResourceBarrier(1, &rt_barrier);
		}
	}

	// Entity-id pick readback. A CopyTextureRegion destination row must be
	// D3D12_TEXTURE_DATA_PITCH_ALIGNMENT-aligned, so the smallest legal buffer
	// for the one pixel this reads is 256 bytes. Not double-buffered: present()
	// resolves it after wait_on_fence(), so only one frame's copy is ever in
	// flight.
	{
		const auto readback_heap_props = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_READBACK);
		const D3D12_RESOURCE_DESC desc = CD3DX12_RESOURCE_DESC::Buffer(D3D12_TEXTURE_DATA_PITCH_ALIGNMENT);
		blk::error_check(
			m_device->CreateCommittedResource(
				&readback_heap_props,
				D3D12_HEAP_FLAG_NONE,
				&desc,
				D3D12_RESOURCE_STATE_COPY_DEST,
				nullptr,
				IID_PPV_ARGS(m_entity_id_readback_buffer.ReleaseAndGetAddressOf())
			)
		);
		m_entity_id_readback_buffer->SetName(L"Renderer_Dx12::EntityIdPickReadback");
	}

	// General root signature
	{
		// The root signature determines what kind of data the shader should expect.
		// SRVs are bindless (see EFrameResource/ERenderTarget-relative-index comment
		// removed from the light/shadow passes below): shaders fetch textures via
		// ResourceDescriptorHeap[absolute_index] instead of a bound descriptor table,
		// so there's no SRV range/root parameter here anymore.
		CD3DX12_DESCRIPTOR_RANGE1 ranges[3] = {};
		ranges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, g_max_scene_constants, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC);
		ranges[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER, 1, 0);
		ranges[2].Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, g_max_scene_bone_arrays, 0, 2, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC);

		// Root parameters are entries in the root signature
		CD3DX12_ROOT_PARAMETER1 root_parameters[5] = {};
		root_parameters[0].InitAsDescriptorTable(1, &ranges[0], D3D12_SHADER_VISIBILITY_ALL);		// scene_constants
		root_parameters[1].InitAsDescriptorTable(1, &ranges[1], D3D12_SHADER_VISIBILITY_PIXEL);		// sampler
		root_parameters[2].InitAsConstants(1, 0, 1, D3D12_SHADER_VISIBILITY_ALL);					// scene_indices
		root_parameters[3].InitAsDescriptorTable(1, &ranges[2], D3D12_SHADER_VISIBILITY_VERTEX);	// bones
		root_parameters[4].InitAsConstants(1, 0, 3, D3D12_SHADER_VISIBILITY_ALL);					// bone_index

		const D3D12_ROOT_SIGNATURE_FLAGS signature_flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
			D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
			D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
			D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS |
			D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED;

		CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC root_signature_desc = {};
		root_signature_desc.Init_1_1(_countof(root_parameters), root_parameters, 0, nullptr, signature_flags);

		ComPtr<ID3DBlob> signature;
		ComPtr<ID3DBlob> error;
		if (!blk::warn_check(D3DX12SerializeVersionedRootSignature(&root_signature_desc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &error))) {
			blk::error("%s", error->GetBufferPointer());
		}
		blk::error_check(m_device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&m_root_signature)));
	}

	{
		const u64 splat_buffer_size = sizeof(PointCloudSampleInstance) * g_max_point_cloud_points;
		const u64 index_buffer_size = sizeof(uint32_t) * g_max_point_cloud_points;

		// Point Cloud Data Heaps (Splats)
		{
			D3D12_RESOURCE_DESC desc = CD3DX12_RESOURCE_DESC::Buffer(splat_buffer_size, D3D12_RESOURCE_FLAG_NONE);

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

			desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

			blk::error_check(m_device->CreateCommittedResource(
				&g_D3D12_HEAP_TYPE_DEFAULT,
				D3D12_HEAP_FLAG_NONE,
				&desc,
				D3D12_RESOURCE_STATE_COMMON,
				nullptr,
				IID_PPV_ARGS(&m_point_cloud_default_heap)
			));
			m_point_cloud_default_heap->SetName(L"Renderer_Dx12::m_point_cloud_default_heap");
		}

		// Point Cloud Index Heaps (Indices)
		{
			D3D12_RESOURCE_DESC desc = CD3DX12_RESOURCE_DESC::Buffer(index_buffer_size, D3D12_RESOURCE_FLAG_NONE);

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

			desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

			blk::error_check(m_device->CreateCommittedResource(
				&g_D3D12_HEAP_TYPE_DEFAULT,
				D3D12_HEAP_FLAG_NONE,
				&desc,
				D3D12_RESOURCE_STATE_COMMON,
				nullptr,
				IID_PPV_ARGS(&m_point_cloud_index_default_heap)
			));
			m_point_cloud_index_default_heap->SetName(L"Renderer_Dx12::m_point_cloud_index_default_heap");
		}

		// Create the Descriptor Heap
		{
			D3D12_DESCRIPTOR_HEAP_DESC heap_desc = {};
			heap_desc.NumDescriptors = 2; // We need exactly 2 slots (t0 for splats, t1 for indices)
			heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
			heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE; // Critical for drawing!
			heap_desc.NodeMask = 0;

			blk::error_check(m_device->CreateDescriptorHeap(&heap_desc, IID_PPV_ARGS(&m_point_cloud_descriptor_heap)));
			m_point_cloud_descriptor_heap->SetName(L"Renderer_Dx12::m_point_cloud_descriptor_heap");
		}

		// Descriptor Routing (SRVs)
		{
			CD3DX12_CPU_DESCRIPTOR_HANDLE srv_handle(m_point_cloud_descriptor_heap->GetCPUDescriptorHandleForHeapStart());
			UINT descriptor_size = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

			// SRV for t0: Splat Buffer
			D3D12_SHADER_RESOURCE_VIEW_DESC splat_srv_desc = {};
			splat_srv_desc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
			splat_srv_desc.Format = DXGI_FORMAT_UNKNOWN;
			splat_srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
			splat_srv_desc.Buffer.FirstElement = 0;
			splat_srv_desc.Buffer.NumElements = g_max_point_cloud_points;
			splat_srv_desc.Buffer.StructureByteStride = sizeof(PointCloudSampleInstance);

			m_device->CreateShaderResourceView(m_point_cloud_default_heap.Get(), &splat_srv_desc, srv_handle);

			srv_handle.Offset(1, descriptor_size);

			// SRV for t1: Index Buffer
			D3D12_SHADER_RESOURCE_VIEW_DESC index_srv_desc = {};
			index_srv_desc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
			index_srv_desc.Format = DXGI_FORMAT_UNKNOWN;
			index_srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
			index_srv_desc.Buffer.FirstElement = 0;
			index_srv_desc.Buffer.NumElements = g_max_point_cloud_points;
			index_srv_desc.Buffer.StructureByteStride = sizeof(uint32_t);

			m_device->CreateShaderResourceView(m_point_cloud_index_default_heap.Get(), &index_srv_desc, srv_handle);
		}

		// Root Signature
		{
			CD3DX12_DESCRIPTOR_RANGE1 point_cloud_srv_range;
			point_cloud_srv_range.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 2, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_NONE);

			CD3DX12_ROOT_PARAMETER1 root_parameters[2] = {};
			root_parameters[0].InitAsConstantBufferView(0, 0, D3D12_ROOT_DESCRIPTOR_FLAG_NONE, D3D12_SHADER_VISIBILITY_ALL);
			root_parameters[1].InitAsDescriptorTable(1, &point_cloud_srv_range, D3D12_SHADER_VISIBILITY_VERTEX);

			const D3D12_ROOT_SIGNATURE_FLAGS signature_flags =
				D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
				D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
				D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
				D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS;

			CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC root_signature_desc;
			root_signature_desc.Init_1_1(_countof(root_parameters), root_parameters, 0, nullptr, signature_flags);

			ComPtr<ID3DBlob> signature;
			ComPtr<ID3DBlob> error;
			if (!blk::warn_check(D3DX12SerializeVersionedRootSignature(&root_signature_desc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &error))) {
				blk::error("%s", (char*)error->GetBufferPointer());
			}

			blk::error_check(m_device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&m_point_cloud_signature)));
			m_point_cloud_signature->SetName(L"Renderer_Dx12::m_point_cloud_signature");
		}
	}

	// GS Compute sort
	{
		CD3DX12_ROOT_PARAMETER1 root_parameters[4];
		root_parameters[0].InitAsConstantBufferView(0, 0, D3D12_ROOT_DESCRIPTOR_FLAG_NONE, D3D12_SHADER_VISIBILITY_ALL);		// b0: global constants
		root_parameters[1].InitAsShaderResourceView(0, 0, D3D12_ROOT_DESCRIPTOR_FLAG_NONE, D3D12_SHADER_VISIBILITY_ALL);		// t0: splats
		root_parameters[2].InitAsUnorderedAccessView(0, 0, D3D12_ROOT_DESCRIPTOR_FLAG_NONE, D3D12_SHADER_VISIBILITY_ALL);		// u0: output sorted indices
		root_parameters[3].InitAsConstants(2, 1, D3D12_SHADER_VISIBILITY_ALL);													// b1: 2 dwords

		CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC root_sig_desc;
		root_sig_desc.Init_1_1(_countof(root_parameters), root_parameters, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_NONE);

		ComPtr<ID3DBlob> signature;
		ComPtr<ID3DBlob> error;
		D3DX12SerializeVersionedRootSignature(&root_sig_desc, D3D_ROOT_SIGNATURE_VERSION_1_1, &signature, &error);
		m_device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&m_gs_sort_signature));
		m_gs_sort_signature->SetName(L"m_gs_sort_signature");

		// Descriptor heap for UAV
		D3D12_DESCRIPTOR_HEAP_DESC heap_desc = {};
		heap_desc.NumDescriptors = 1;
		heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
		m_device->CreateDescriptorHeap(&heap_desc, IID_PPV_ARGS(&m_gs_sort_desc_heap));

		// UAV
		D3D12_UNORDERED_ACCESS_VIEW_DESC uav_desc = {};
		uav_desc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
		uav_desc.Buffer.FirstElement = 0;
		uav_desc.Buffer.NumElements = g_max_point_cloud_points;
		uav_desc.Buffer.StructureByteStride = sizeof(PointCloudSample);
		uav_desc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;

		CD3DX12_RESOURCE_DESC buffer_desc = CD3DX12_RESOURCE_DESC::Buffer(
			g_max_point_cloud_points * sizeof(PointCloudSample),
			D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS
		);

		m_device->CreateCommittedResource(
			&g_D3D12_HEAP_TYPE_DEFAULT,
			D3D12_HEAP_FLAG_NONE,
			&buffer_desc,
			D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
			nullptr,
			IID_PPV_ARGS(&m_gs_sort_buffer)
		);

		m_device->CreateUnorderedAccessView(m_gs_sort_buffer.Get(), nullptr, &uav_desc, m_gs_sort_desc_heap->GetCPUDescriptorHandleForHeapStart());
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

	// Phase 3, Milestone 2: Dear ImGui init. Docking only; viewports/platform
	// windows are separate, larger scope. Font atlas upload is handled
	// automatically by the backend's own dynamic-texture support
	// (ImGuiBackendFlags_RendererHasTextures) -- no manual upload here.
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_DockingEnable;

	ImGui_ImplWin32_Init(m_hwnd);

	ImGui_ImplDX12_InitInfo imgui_init_info = {};
	imgui_init_info.Device = m_device.Get();
	imgui_init_info.CommandQueue = m_queue.Get();
	imgui_init_info.NumFramesInFlight = Renderer::max_frames();
	imgui_init_info.RTVFormat = swap_chain_desc.Format;
	imgui_init_info.SrvDescriptorHeap = m_imgui_srv_heap.Get();
	imgui_init_info.UserData = this;
	imgui_init_info.SrvDescriptorAllocFn = [](ImGui_ImplDX12_InitInfo* info, D3D12_CPU_DESCRIPTOR_HANDLE* out_cpu, D3D12_GPU_DESCRIPTOR_HANDLE* out_gpu) {
		((Renderer_Dx12*)info->UserData)->m_imgui_srv_heap_allocator.alloc(out_cpu, out_gpu);
	};
	imgui_init_info.SrvDescriptorFreeFn = [](ImGui_ImplDX12_InitInfo* info, D3D12_CPU_DESCRIPTOR_HANDLE cpu, D3D12_GPU_DESCRIPTOR_HANDLE gpu) {
		((Renderer_Dx12*)info->UserData)->m_imgui_srv_heap_allocator.free(cpu, gpu);
	};
	ImGui_ImplDX12_Init(&imgui_init_info);

	blk::log("Renderer_Dx12 initialized");
}

/// Renderer_Dx12::shut_down_internal
void Renderer_Dx12::shut_down_internal() {
	shutdown_gaussian_splatting();

	wait_on_fence();

	// Phase 3, Milestone 2: mirrors the init in initialize_internal.
	ImGui_ImplDX12_Shutdown();
	ImGui_ImplWin32_Shutdown();
	ImGui::DestroyContext();

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

	m_point_cloud_descriptor_heap.Reset();
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

	m_gs_sort_signature.Reset();
	m_gs_sort_desc_heap.Reset();
	m_gs_sort_buffer.Reset();
	m_gs_sort_upload_buffer.Reset();

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

/// Renderer_Dx12::add_render_component_internal
void Renderer_Dx12::add_render_component_internal(const RenderComponent* const render_comp) {
	blk::error_check(render_comp, "Renderer_Dx12::add_render_component_internal() - null RenderComponent");

	if (!m_gaussian_splat && render_comp->IsA(GaussianSplatComponent::GetType())) {
		initialize_gaussian_splatting((GaussianSplatComponent*)render_comp);
	}
}

/// Renderer_Dx12::remove_render_component_internal
void Renderer_Dx12::remove_render_component_internal(const RenderComponent* const render_comp) {
	blk::error_check(render_comp, "Renderer_Dx12::remove_render_component_internal() - null RenderComponent");

	if (render_comp->IsA(GaussianSplatComponent::GetType())) {
		shutdown_gaussian_splatting();
	}
}

/// Renderer_Dx12::create_render_buffer_internal
RenderBuffer* Renderer_Dx12::create_render_buffer_internal() {
	return new RenderBuffer_Dx12();
}

namespace {
	D3D12_RESOURCE_STATES to_d3d12_state(const EGraphResourceState state) {
		switch (state) {
			case EGraphResourceState::RenderTarget: return D3D12_RESOURCE_STATE_RENDER_TARGET;
			case EGraphResourceState::DepthWrite: return D3D12_RESOURCE_STATE_DEPTH_WRITE;
			case EGraphResourceState::CopySource: return D3D12_RESOURCE_STATE_COPY_SOURCE;
			case EGraphResourceState::CopyDest: return D3D12_RESOURCE_STATE_COPY_DEST;
			case EGraphResourceState::Common:
			default: return D3D12_RESOURCE_STATE_COMMON;
		}
	}

	// The debug layer/GPU-based validation (enabled in initialize_internal)
	// normally reports problems only via OutputDebugString, which is silent
	// without an attached debugger. Draining ID3D12InfoQueue on demand puts
	// those messages in the regular log instead, so a command-list failure
	// like a bad resource-state transition self-diagnoses without needing a
	// debugger session.
	void log_d3d12_debug_messages(ID3D12Device* const device) {
		ComPtr<ID3D12InfoQueue> info_queue;
		if (FAILED(device->QueryInterface(IID_PPV_ARGS(&info_queue)))) {
			return;
		}

		const UINT64 num_messages = info_queue->GetNumStoredMessages();
		for (UINT64 i = 0; i < num_messages; i++) {
			SIZE_T message_length = 0;
			info_queue->GetMessage(i, nullptr, &message_length);
			if (message_length == 0) {
				continue;
			}

			std::vector<u8> buffer(message_length);
			D3D12_MESSAGE* const message = (D3D12_MESSAGE*)buffer.data();
			info_queue->GetMessage(i, message, &message_length);
			blk::log("D3D12 debug layer [%d]: %s", (int)message->Severity, message->pDescription);
		}

		info_queue->ClearStoredMessages();
	}
}

/// Renderer_Dx12::emit_barriers
void Renderer_Dx12::emit_barriers(const std::vector<GraphTransition>& transitions) {
	std::vector<D3D12_RESOURCE_BARRIER> barriers;
	barriers.reserve(transitions.size());
	for (const GraphTransition& transition : transitions) {
		barriers.push_back(CD3DX12_RESOURCE_BARRIER::Transition(
			(ID3D12Resource*)transition.resource->native_handle,
			to_d3d12_state(transition.from),
			to_d3d12_state(transition.to)
		));
	}
	m_command_list->ResourceBarrier((u32)barriers.size(), barriers.data());
}

/// Renderer_Dx12::push_debug_marker
void Renderer_Dx12::push_debug_marker(const char* const name) {
	std::wstring wide_name;
	WStringFromString(wide_name, name);
	m_command_list->BeginEvent(0, wide_name.c_str(), (u32)((wide_name.size() + 1) * sizeof(wchar_t)));
}

/// Renderer_Dx12::pop_debug_marker
void Renderer_Dx12::pop_debug_marker() {
	m_command_list->EndEvent();
}

/// Renderer_Dx12::begin_frame_resources
///
/// Refreshes this frame's GraphResource for each ERenderTarget from the
/// double-buffered m_render_targets copy that matches m_frame_index, before
/// resolve_graph_resource() hands any of them out to the shared graph
/// driver (Renderer::run_render_graph()).
///
/// Also resets the command allocator/list for the frame -- this used to
/// happen inside render_gbuffer_internal() itself (implicitly relying on
/// "gbuffer" always being the first pass to touch the command list each
/// frame), which broke once passes started being bracketed with debug event
/// markers: Renderer::run_render_graph() calls begin_frame_resources()
/// before building the graph, so push_debug_marker()'s BeginEvent() for the
/// first pass was firing on the command list still closed from the previous
/// frame's present() -- an invalid call on a closed list, silently dropped
/// (RenderDoc showed no "gbuffer" marker at all, even though the pass itself
/// still ran). Resetting here, before any pass or its marker runs, fixes
/// that for "gbuffer" specifically and stops the reset being implicitly tied
/// to whichever pass happens to be first in the topology.
void Renderer_Dx12::begin_frame_resources() {
	blk::error_check(m_command_allocator->Reset());
	blk::error_check(m_command_list->Reset(m_command_allocator.Get(), nullptr));

	m_frame_graph_resources[ERenderTarget::Color] = GraphResource{ m_render_targets[ERenderTarget::Color][m_frame_index].Get() };
	m_frame_graph_resources[ERenderTarget::Normal] = GraphResource{ m_render_targets[ERenderTarget::Normal][m_frame_index].Get() };
	m_frame_graph_resources[ERenderTarget::Specular] = GraphResource{ m_render_targets[ERenderTarget::Specular][m_frame_index].Get() };
	// Despite the name, SceneDepth is an R32_FLOAT color target (linear
	// depth written via a normal pixel shader), not a real depth-stencil
	// resource -- the actual depth buffer for the gbuffer pass is the
	// separate dsv_handle it binds directly. It was created with
	// ALLOW_RENDER_TARGET, not ALLOW_DEPTH_STENCIL, so it's declared
	// RenderTarget in frame_pass_topology() like its three siblings.
	m_frame_graph_resources[ERenderTarget::SceneDepth] = GraphResource{ m_render_targets[ERenderTarget::SceneDepth][m_frame_index].Get() };
	m_frame_graph_resources[ERenderTarget::Lighting] = GraphResource{ m_render_targets[ERenderTarget::Lighting][m_frame_index].Get() };
	// Lights/point-clouds/translucency all render into SceneColor instead of
	// the swapchain backbuffer directly, so post-process has a full-screen
	// buffer to read from before the final composite.
	m_frame_graph_resources[ERenderTarget::SceneColor] = GraphResource{ m_render_targets[ERenderTarget::SceneColor][m_frame_index].Get() };
	// Written by the gbuffer pass alongside the four targets above; only ever
	// read back through copy_entity_id_pick_pixel(), never sampled.
	m_frame_graph_resources[ERenderTarget::EntityId] = GraphResource{ m_render_targets[ERenderTarget::EntityId][m_frame_index].Get() };
	m_frame_graph_resources[ERenderTarget::ShadowDepth] = GraphResource{ m_render_targets[ERenderTarget::ShadowDepth][m_frame_index].Get() };
}

/// Renderer_Dx12::resolve_graph_resource
///
/// Maps the shared topology's logical EFrameResource to this frame's
/// concrete GraphResource. A distinct enum from ERenderTarget on purpose --
/// ERenderTarget also encodes D3D12's own descriptor-heap ordering
/// constraints (see its doc comment above), which no other backend should
/// have to know about.
GraphResource* Renderer_Dx12::resolve_graph_resource(EFrameResource target) {
	switch (target) {
		case EFrameResource::Color: return &m_frame_graph_resources[ERenderTarget::Color];
		case EFrameResource::Normal: return &m_frame_graph_resources[ERenderTarget::Normal];
		case EFrameResource::Specular: return &m_frame_graph_resources[ERenderTarget::Specular];
		case EFrameResource::SceneDepth: return &m_frame_graph_resources[ERenderTarget::SceneDepth];
		case EFrameResource::Lighting: return &m_frame_graph_resources[ERenderTarget::Lighting];
		case EFrameResource::SceneColor: return &m_frame_graph_resources[ERenderTarget::SceneColor];
		case EFrameResource::EntityId: return &m_frame_graph_resources[ERenderTarget::EntityId];
		case EFrameResource::ShadowDepth: return &m_frame_graph_resources[ERenderTarget::ShadowDepth];
	}
	return nullptr;
}

/// Renderer_Dx12::get_pipeline_state
ID3D12PipelineState* Renderer_Dx12::get_pipeline_state(const std::string& name) {
	return ((RenderPipeline_Dx12*)get_pipeline(name))->m_pipeline_state.Get();
}

/// Renderer_Dx12::get_pass_execute
///
/// Maps a shared topology pass name (see Renderer::frame_pass_topology())
/// to this backend's execution callback. Returning nullptr opts the pass
/// out entirely for this frame -- run_render_graph() skips it with no
/// resources touched and no barriers requested; nothing here currently
/// returns nullptr since D3D12 implements every pass in the topology.
///
/// opaque_mask/translucent_mask are `static` (not per-call locals) so the
/// lambdas below, which are returned out of this function and invoked later
/// from run_render_graph(), can reference them directly with no capture at
/// all -- an ordinary local would already be gone by the time that happens.
RenderGraph::ExecuteFn Renderer_Dx12::get_pass_execute(const std::string& pass_name, const std::vector<ViewContext>& views, size_t view_index) {
	// Which ERenderPass buckets each pass draws -- declared here instead of
	// hardcoded as a "render_pass() != RP_X" check inside each function, so
	// wiring up a currently-dead bucket (e.g. RP_Distortion) in a later
	// phase means adding a mask entry, not touching render_*_internal.
	static const ERenderPassMask opaque_mask = { ERenderPass::RP_Lighting };
	static const ERenderPassMask translucent_mask = { ERenderPass::RP_Translucent };

	if (pass_name == "gbuffer") {
		return [this, &views, view_index]() { render_gbuffer_internal(views[view_index].camera, opaque_mask); };
	}
	// Two passes, not one: ShadowDepth must revert to Common between cascade
	// rendering and the composite that reads it back via SRV (see
	// render_shadow_cascades's comment for why bundling them into a single
	// pass broke the shadow projection).
	if (pass_name == "shadow_cascades") {
		return [this, &views]() { render_shadow_cascades(views[0].camera, opaque_mask); };
	}
	if (pass_name == "shadow_composite") {
		return [this, &views]() { render_shadow_composite(views[0].camera); };
	}
	if (pass_name == "lights") {
		return [this, &views, view_index]() { render_lights_internal(views[view_index].camera); };
	}
	if (pass_name == "point_clouds") {
		return [this, &views]() { render_point_clouds(views[0].camera); };
	}
	if (pass_name == "translucency") {
		return [this, &views, view_index]() { render_transluency_internal(views[view_index].camera, translucent_mask); };
	}
	// Placeholder composite (straight copy) from SceneColor to the back
	// buffer today -- the seam for tonemap/bloom/color-grade once those
	// exist.
	if (pass_name == "post_process") {
		return [this, &views]() { render_post_process(views[0].camera); };
	}
	if (pass_name == "ui_overlay") {
		return [this]() { render_ui_overlay(); };
	}

	return nullptr;
}

/// Renderer_Dx12::render_gbuffer_internal
void Renderer_Dx12::render_gbuffer_internal(const RenderCamera& camera, const ERenderPassMask& render_pass_mask) {
	// The command allocator/list reset for the frame now happens once, up in
	// begin_frame_resources() -- see its doc comment for why it moved out of
	// this specific pass.
	// SetDescriptorHeaps must precede SetGraphicsRootSignature: m_root_signature
	// carries D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED, and
	// binding that root signature before the CBV/SRV/UAV heap leaves the
	// shaders' ResourceDescriptorHeap[] indexing pointing at nothing -- every
	// bindless texture fetch (material albedo here) reads garbage, so the
	// gbuffer's Color target came out black while Normal/SceneDepth, which are
	// computed rather than sampled, still looked correct.
	ID3D12DescriptorHeap* ppHeaps[] = { m_cbv_srv_descriptor_heap.Get(), m_sampler_descriptor_heap.Get() };
	m_command_list->SetDescriptorHeaps(_countof(ppHeaps), ppHeaps);

	m_command_list->SetGraphicsRootSignature(m_root_signature.Get());
	m_command_list->RSSetViewports(1, &m_view_port);
	m_command_list->RSSetScissorRects(1, &m_scissor_rect);

	// Color/Normal/Specular/SceneDepth are transitioned into RenderTarget/
	// DepthWrite by the render graph (see Renderer::frame_pass_topology())
	// before this pass runs, and back to Common after it returns.

	// Todo: Subtract 1 since the shadow render target doesn't have an associated rtv
	const u32 gbuffer_start = Renderer::max_frames() + (ERenderTarget::Count - 1) * m_frame_index;
	// The first four are Color/Normal/Specular/SceneDepth (slots 0-3, matching
	// their enum values). EntityId is addressed by its own enum value rather
	// than a literal 4 -- its RTV slot is 6, since SceneColor and the
	// RTV-less ShadowDepth sit between.
	CD3DX12_CPU_DESCRIPTOR_HANDLE rtv_handle[] = {
		CD3DX12_CPU_DESCRIPTOR_HANDLE(m_rtv_heap->GetCPUDescriptorHandleForHeapStart(), gbuffer_start + 0, m_rtv_descriptor_size),
		CD3DX12_CPU_DESCRIPTOR_HANDLE(m_rtv_heap->GetCPUDescriptorHandleForHeapStart(), gbuffer_start + 1, m_rtv_descriptor_size),
		CD3DX12_CPU_DESCRIPTOR_HANDLE(m_rtv_heap->GetCPUDescriptorHandleForHeapStart(), gbuffer_start + 2, m_rtv_descriptor_size),
		CD3DX12_CPU_DESCRIPTOR_HANDLE(m_rtv_heap->GetCPUDescriptorHandleForHeapStart(), gbuffer_start + 3, m_rtv_descriptor_size),
		CD3DX12_CPU_DESCRIPTOR_HANDLE(m_rtv_heap->GetCPUDescriptorHandleForHeapStart(), gbuffer_start + ERenderTarget::EntityId, m_rtv_descriptor_size),
	};

	CD3DX12_CPU_DESCRIPTOR_HANDLE dsv_handle(m_depth_stencil_heap->GetCPUDescriptorHandleForHeapStart(), 0, m_depth_target_descriptor_size);
	m_command_list->OMSetRenderTargets(5, rtv_handle, false, &dsv_handle);

	const f32 clear_color[] = { 0.f, 0.f, 0.f, 0.f };
	const f32 normal_color[] = { 0.5f, 0.5f, 0.5f, 0.f };
	// -1 == "no entity at this pixel"; must match the resource's own
	// D3D12_CLEAR_VALUE (see the EntityId creation block).
	const f32 no_entity_color[] = { -1.f, 0.f, 0.f, 0.f };
	m_command_list->ClearRenderTargetView(rtv_handle[0], clear_color, 0, nullptr);
	m_command_list->ClearRenderTargetView(rtv_handle[1], normal_color, 0, nullptr);
	m_command_list->ClearRenderTargetView(rtv_handle[2], clear_color, 0, nullptr);
	m_command_list->ClearRenderTargetView(rtv_handle[3], clear_color, 0, nullptr);
	m_command_list->ClearRenderTargetView(rtv_handle[4], no_entity_color, 0, nullptr);

	m_command_list->ClearDepthStencilView(dsv_handle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

	m_command_list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	auto descriptor_size = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	CD3DX12_GPU_DESCRIPTOR_HANDLE cbvSrvHandle(m_cbv_srv_descriptor_heap->GetGPUDescriptorHandleForHeapStart(), 0, descriptor_size);
	m_command_list->SetGraphicsRootDescriptorTable(0, cbvSrvHandle);
	m_command_list->SetGraphicsRootDescriptorTable(1, m_sampler_descriptor_heap->GetGPUDescriptorHandleForHeapStart());

	CD3DX12_GPU_DESCRIPTOR_HANDLE bone_descriptor_handle(m_cbv_srv_descriptor_heap->GetGPUDescriptorHandleForHeapStart(), g_bone_array_descriptor_start, descriptor_size);
	m_command_list->SetGraphicsRootDescriptorTable(3, bone_descriptor_handle);

	g_global_uniform->view_projection = camera.view_projection_matrix;
	g_global_uniform->inv_view_proj = (*(Mat4*)&camera.inv_view_projection_matrix);
	g_global_uniform->camera_pos = Vec4(camera.view_position, 1.f);
	g_global_uniform->view = camera.view_matrix;

	// The first entry in g_scene_buffers is the global const
	m_frame_draws = 1;
	m_bone_draws = 0;
	for (auto& render_comp : this->render_components()) {
		RenderBuffer_Dx12* vertex_buffer = nullptr;
		RenderBuffer_Dx12* index_buffer = nullptr;
		const kbModel* model = nullptr;

		auto& scene_buffer = g_scene_buffers[m_frame_draws];

		if (!render_pass_in_mask(render_comp->render_pass(), render_pass_mask)) {
			continue;
		}

		if (render_comp->IsA(StaticModelComponent::GetType())) {
			const StaticModelComponent* const model_comp = static_cast<const StaticModelComponent*>(render_comp);
			model = model_comp->model();

			m_command_list->SetPipelineState(get_pipeline_state("static_model_base"));

			vertex_buffer = (RenderBuffer_Dx12*)model->m_vertex_buffer;
			index_buffer = (RenderBuffer_Dx12*)model->m_index_buffer;

			const auto vertex_buf_view = vertex_buffer->vertex_buffer_view();
			m_command_list->IASetVertexBuffers(0, 1, &vertex_buf_view);

			const auto index_buf_view = index_buffer->index_buffer_view();
			m_command_list->IASetIndexBuffer(&index_buf_view);
		} else if (render_comp->IsA(SkeletalModelComponent::GetType())) {
			const SkeletalModelComponent* const skel = static_cast<const SkeletalModelComponent*>(render_comp);
			model = skel->model();

			m_command_list->SetPipelineState(get_pipeline_state("skinned_base"));

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

			m_command_list->SetGraphicsRoot32BitConstant(4, (u32)m_bone_draws, 0);
			m_bone_draws++;
		} else if (render_comp->IsA(ParticleComponent::GetType())) {
			continue;
		} else if (render_comp->IsA(TerrainComponent::GetType())) {
			const TerrainComponent* const model_comp = static_cast<const TerrainComponent*>(render_comp);
			const kbModel& model = model_comp->model();

			m_command_list->SetPipelineState(get_pipeline_state("terrain"));

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

		scene_buffer.mvp = (world_mat * camera.view_projection_matrix);

		scene_buffer.color = color;
		scene_buffer.spec = spec;
		scene_buffer.time_since_spawn = time;

		// Taken from the owning entity rather than the render object's own
		// m_EntityId: that copy is only refreshed on some component enable
		// paths (model_component's, not terrain's), so it can be stale or zero
		// while GetOwner()->GetEntityId() is always current.
		scene_buffer.entity_id = Vec4((f32)render_comp->GetOwner()->GetEntityId(), 0.f, 0.f, 0.f);

		m_command_list->SetGraphicsRoot32BitConstant(2, (u32)m_frame_draws, 0);

		// Material textures are bindless: the shader adds GlobalConstantData's
		// srv_heap_base to each texture_list[] id itself, so no SRV table bind here.
		m_command_list->DrawIndexedInstanced(index_buffer->num_elements(), 1, 0, 0, 0);
		m_frame_draws = m_frame_draws + 1;
	}

	// Inside this pass, after every draw: the graph has EntityId in
	// RenderTarget for the duration and flips it back to Common on return, so
	// the COPY_SOURCE round trip has to be bracketed here by hand -- the same
	// arrangement render_post_process uses for the back buffer.
	copy_entity_id_pick_pixel();
}

/// Renderer_Dx12::request_entity_id_pick
void Renderer_Dx12::request_entity_id_pick(const u32 backbuffer_x, const u32 backbuffer_y) {
	// Later requests in the same frame win; a click is a click, and the editor
	// only ever issues one per frame anyway.
	m_pick_x = backbuffer_x;
	m_pick_y = backbuffer_y;
	m_pick_requested = true;
}

/// Renderer_Dx12::try_take_entity_id_pick
bool Renderer_Dx12::try_take_entity_id_pick(u32& out_entity_id) {
	if (!m_pick_result_ready) {
		return false;
	}

	out_entity_id = m_pick_result;
	m_pick_result_ready = false;
	m_pick_result = Renderer::invalid_entity_id();
	return true;
}

/// Renderer_Dx12::copy_entity_id_pick_pixel
void Renderer_Dx12::copy_entity_id_pick_pixel() {
	if (!m_pick_requested) {
		return;
	}

	m_pick_requested = false;

	// A click outside the backbuffer can't have hit anything. Answering it here
	// as a miss (rather than dropping the request) keeps the editor's poll from
	// waiting forever on a result that would never arrive.
	if (m_pick_x >= m_frame_width || m_pick_y >= m_frame_height) {
		m_pick_result = Renderer::invalid_entity_id();
		m_pick_result_ready = true;
		return;
	}

	ID3D12Resource* const entity_id_rt = m_render_targets[ERenderTarget::EntityId][m_frame_index].Get();

	auto to_copy_source = CD3DX12_RESOURCE_BARRIER::Transition(entity_id_rt, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
	m_command_list->ResourceBarrier(1, &to_copy_source);

	const CD3DX12_TEXTURE_COPY_LOCATION copy_src(entity_id_rt, 0);

	D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint = {};
	footprint.Offset = 0;
	footprint.Footprint.Format = DXGI_FORMAT_R32_FLOAT;
	footprint.Footprint.Width = 1;
	footprint.Footprint.Height = 1;
	footprint.Footprint.Depth = 1;
	footprint.Footprint.RowPitch = D3D12_TEXTURE_DATA_PITCH_ALIGNMENT;
	const CD3DX12_TEXTURE_COPY_LOCATION copy_dst(m_entity_id_readback_buffer.Get(), footprint);

	// Box is in source-texture pixels; back is 1 (not 0) or the copy is empty.
	D3D12_BOX src_box = {};
	src_box.left = m_pick_x;
	src_box.top = m_pick_y;
	src_box.front = 0;
	src_box.right = m_pick_x + 1;
	src_box.bottom = m_pick_y + 1;
	src_box.back = 1;

	m_command_list->CopyTextureRegion(&copy_dst, 0, 0, 0, &copy_src, &src_box);

	auto to_render_target = CD3DX12_RESOURCE_BARRIER::Transition(entity_id_rt, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
	m_command_list->ResourceBarrier(1, &to_render_target);

	m_pick_copy_recorded = true;
}

/// Renderer_Dx12::resolve_entity_id_pick
void Renderer_Dx12::resolve_entity_id_pick() {
	// Only once this frame actually recorded the copy -- see the comment on
	// m_pick_copy_recorded for why the request flag alone isn't enough.
	if (!m_pick_copy_recorded) {
		return;
	}

	m_pick_copy_recorded = false;
	m_pick_result = Renderer::invalid_entity_id();
	m_pick_result_ready = true;

	// Read range covers just the one float the copy wrote; the empty write
	// range tells D3D12 the CPU changed nothing.
	const D3D12_RANGE read_range = { 0, sizeof(f32) };
	const D3D12_RANGE write_range = { 0, 0 };

	void* mapped = nullptr;
	if (!blk::warn_check(m_entity_id_readback_buffer->Map(0, &read_range, &mapped)) || !mapped) {
		return;
	}

	const f32 pixel = *(const f32*)mapped;
	m_entity_id_readback_buffer->Unmap(0, &write_range);

	// Cleared to -1 where nothing drew. Anything >= 0 is an id written as an
	// exact integer, so the cast back is lossless.
	if (pixel >= 0.f) {
		m_pick_result = (u32)(pixel + 0.5f);
	}
}

/// Renderer_Dx12::render_lights_internal
void Renderer_Dx12::render_lights_internal(const RenderCamera& camera) {
	// SceneColor is transitioned into RenderTarget by the render graph (see
	// Renderer::frame_pass_topology()) before this pass runs, and back to
	// Common after.

	CD3DX12_CPU_DESCRIPTOR_HANDLE dsv_handle(m_depth_stencil_heap->GetCPUDescriptorHandleForHeapStart(), 0, m_depth_target_descriptor_size);
	const u32 gbuffer_start = Renderer::max_frames() + (ERenderTarget::Count - 1) * m_frame_index;
	CD3DX12_CPU_DESCRIPTOR_HANDLE rtv_handle(m_rtv_heap->GetCPUDescriptorHandleForHeapStart(), gbuffer_start + SceneColor, m_rtv_descriptor_size);

	m_command_list->OMSetRenderTargets(1, &rtv_handle, false, &dsv_handle);

	const float clear_color[] = { 0.0f, 0.0f, 0.f, 0.0f };
	m_command_list->ClearRenderTargetView(rtv_handle, clear_color, 0, nullptr);

	const auto& lights = this->light_components();
	for (auto& light : lights) {
		ID3D12PipelineState* pipe_state = nullptr;
		if (light->IsA(kbDirectionalLightComponent::GetType())) {
			pipe_state = get_pipeline_state("directional_light");
		} else {
			pipe_state = get_pipeline_state("point_light");
		}

		m_command_list->SetPipelineState(pipe_state);
		m_command_list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		m_command_list->IASetVertexBuffers(0, 1, &m_quad_vb_view);

		// color_tex[0..3] in directional_light.hlsl/point_light.hlsl read
		// Color/Normal/Specular/SceneDepth through ResourceDescriptorHeap[] at
		// gbuffer_srv_base + 0..3 (bindless -- no table bind needed). The base
		// must land on *this frame's* gbuffer SRV block -- those targets are
		// double-buffered by m_frame_index (see render_gbuffer_internal), and
		// the SRVs for each frame_index sit in their own ERenderTarget::Count
		// -sized block (see the "Initialize GBuffers" loop). Without this
		// offset, on frame_index 1 the light passes would sample frame_index
		// 0's stale gbuffer instead of the one just written this frame.
		const u32 gbuffer_srv_start = g_srv_descriptor_start + ERenderTarget::Count * m_frame_index;

		LightInstanceData* light_instance_data = (LightInstanceData*)&g_scene_buffers[m_frame_draws];
		light_instance_data->position = light->owner_position();
		light_instance_data->position.w = light->radius();
		light_instance_data->color = light->GetColor();
		light_instance_data->direction = light->owner_rotation().to_mat4()[2].ToVec3();
		light_instance_data->gbuffer_srv_base = Vec4((f32)gbuffer_srv_start, 0.f, 0.f, 0.f);

		int i = 0;
		for (; i < 4; i++) {
			if (i >= light_matrices.size()) {
				light_instance_data->light_matrices[i] = Mat4::identity;
			} else {
				light_instance_data->light_matrices[i] = light_matrices[i];
			}
		}

		light_instance_data->cascade_distances = cascade_distances;
		light_instance_data->player_inv_view_proj = (*(Mat4*)&camera.inv_view_projection_matrix);
		light_instance_data->player_camera_position = Vec4(camera.view_position, 1);

		m_command_list->SetGraphicsRoot32BitConstant(2, (u32)m_frame_draws, 0);

		m_command_list->DrawInstanced(6, 1, 0, 0);
		m_frame_draws++;
	}
}

/// Renderer_Dx12::render_transluency_internal
void Renderer_Dx12::render_transluency_internal(const RenderCamera& camera, const ERenderPassMask& render_pass_mask) {
	auto descriptor_size = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	// Heaps before root signature -- see render_gbuffer_internal's comment.
	ID3D12DescriptorHeap* ppHeaps[] = { m_cbv_srv_descriptor_heap.Get(), m_sampler_descriptor_heap.Get() };
	m_command_list->SetDescriptorHeaps(_countof(ppHeaps), ppHeaps);

	m_command_list->SetGraphicsRootSignature(m_root_signature.Get());
	m_command_list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	CD3DX12_GPU_DESCRIPTOR_HANDLE cbvSrvHandle(m_cbv_srv_descriptor_heap->GetGPUDescriptorHandleForHeapStart(), 0, descriptor_size);
	m_command_list->SetGraphicsRootDescriptorTable(0, cbvSrvHandle);
	m_command_list->SetGraphicsRootDescriptorTable(1, m_sampler_descriptor_heap->GetGPUDescriptorHandleForHeapStart());

	CD3DX12_GPU_DESCRIPTOR_HANDLE bone_descriptor_handle(m_cbv_srv_descriptor_heap->GetGPUDescriptorHandleForHeapStart(), g_bone_array_descriptor_start, descriptor_size);
	m_command_list->SetGraphicsRootDescriptorTable(3, bone_descriptor_handle);

	for (auto& render_comp : this->render_components()) {
		if (!render_pass_in_mask(render_comp->render_pass(), render_pass_mask)) {
			continue;
		}

		RenderBuffer_Dx12* vertex_buffer = nullptr;
		RenderBuffer_Dx12* index_buffer = nullptr;
		const kbModel* model = nullptr;

		auto& scene_buffer = g_scene_buffers[m_frame_draws];

		if (render_comp->IsA(StaticModelComponent::GetType())) {
			const StaticModelComponent* const skel = static_cast<const StaticModelComponent*>(render_comp);
			model = skel->model();

			m_command_list->SetPipelineState(get_pipeline_state("mesh_particle_add"));

			vertex_buffer = (RenderBuffer_Dx12*)model->m_vertex_buffer;
			index_buffer = (RenderBuffer_Dx12*)model->m_index_buffer;

			const auto vertex_buf_view = vertex_buffer->vertex_buffer_view();
			m_command_list->IASetVertexBuffers(0, 1, &vertex_buf_view);

			const auto index_buf_view = index_buffer->index_buffer_view();
			m_command_list->IASetIndexBuffer(&index_buf_view);
		} else if (render_comp->IsA(SkeletalModelComponent::GetType())) {
			const SkeletalModelComponent* const skel = static_cast<const SkeletalModelComponent*>(render_comp);
			model = skel->model();

			ID3D12PipelineState* const pipe_state = (skel->is_breakable()) ?
				get_pipeline_state("destructible_base") :
				get_pipeline_state("skinned_base");

			m_command_list->SetPipelineState(pipe_state);

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

			ID3D12PipelineState* pipe_state = nullptr;
			auto& materials = particle->materials();

			if (materials.size() > 0) {
				switch (materials[0].blend_override()) {
					case EBlendMode::Additive: {
						pipe_state = get_pipeline_state("sprite_particle_add");
						break;
					}
					case EBlendMode::Alpha:
					default: {
						pipe_state = get_pipeline_state("sprite_particle_blend");
						break;
					}
				}
			}
			m_command_list->SetPipelineState(pipe_state);

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

		scene_buffer.mvp = (world_mat * camera.view_projection_matrix);
		scene_buffer.world = world_mat;
		scene_buffer.color = color;
		scene_buffer.time_since_spawn = time;

		m_command_list->SetGraphicsRoot32BitConstant(2, (u32)m_frame_draws, 0);

		// Material textures are bindless: the shader adds GlobalConstantData's
		// srv_heap_base to each texture_list[] id itself, so no SRV table bind here.
		m_command_list->DrawIndexedInstanced(index_buffer->num_elements(), 1, 0, 0, 0);
		m_frame_draws++;
	}
}

/// Renderer_Dx12::render_post_process
///
/// Placeholder: a straight copy from SceneColor to the back buffer. Once a
/// real post-process effect (tonemap/bloom/color-grade/etc.) exists, replace
/// the copy with a full-screen shader pass reading SceneColor via SRV --
/// Renderer::frame_pass_topology() already declares SceneColor as this
/// pass's CopySource read, so only this function needs to change.
void Renderer_Dx12::render_post_process(const RenderCamera& camera) {
	auto to_copy_dest = CD3DX12_RESOURCE_BARRIER::Transition(m_swap_chain_rtv[m_frame_index].Get(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_COPY_DEST);
	m_command_list->ResourceBarrier(1, &to_copy_dest);

	m_command_list->CopyResource(m_swap_chain_rtv[m_frame_index].Get(), m_render_targets[ERenderTarget::SceneColor][m_frame_index].Get());
}

/// Renderer_Dx12::render_ui_overlay
///
/// render_post_process leaves the back buffer in CopyDest; this pass
/// self-brackets its own transitions (CopyDest -> RenderTarget -> CopyDest)
/// so it leaves the buffer exactly where render_post_process left it and
/// present()'s existing CopyDest -> Present transition needs no changes.
/// NewFrame/content/Render are collapsed into this one pass since nothing
/// else produces ImGui widgets yet -- splitting NewFrame out to run before
/// other frame logic is future work once real panels exist outside this pass.
/// Phase 3, Milestone 2: draws whatever m_ui_draw_callback was registered
/// with (kbEditor's real panels in the live editor); falls back to the demo
/// window when nothing is registered.
void Renderer_Dx12::render_ui_overlay() {
	ImGui_ImplDX12_NewFrame();
	ImGui_ImplWin32_NewFrame();

	// Phase 3, Milestone 3: the swapchain is created at g_screen_width x
	// g_screen_height (a hardcoded 1920x1080 in blaise's main.cpp), but
	// ImGui_ImplWin32_NewFrame() just set io.DisplaySize to the viewport
	// window's *client* size, which is smaller. The DX12 backend sizes its
	// D3D12 viewport as DisplaySize * FramebufferScale, so leaving the scale
	// at 1 makes ImGui draw into only the top-left corner of the backbuffer,
	// which the present then stretches down to the window -- shrinking the UI
	// visually while hit-testing stayed in unscaled logical space. That
	// mismatch grows with distance from the origin, so widgets appear
	// progressively further from where they can actually be clicked.
	// FramebufferScale is exactly the field for this (the vendored backend
	// honors it for both the viewport and clip rects): keep logical/hit-test
	// space as the client size the mouse feed already uses, and scale the
	// rendering up to fill the real backbuffer.
	ImGuiIO& io = ImGui::GetIO();
	if (io.DisplaySize.x > 0.0f && io.DisplaySize.y > 0.0f) {
		io.DisplayFramebufferScale = ImVec2((float)m_frame_width / io.DisplaySize.x,
			(float)m_frame_height / io.DisplaySize.y);
	}

	ImGui::NewFrame();

	if (m_ui_draw_callback) {
		m_ui_draw_callback();
	} else {
		ImGui::ShowDemoWindow();
	}

	ImGui::Render();

	auto to_render_target = CD3DX12_RESOURCE_BARRIER::Transition(m_swap_chain_rtv[m_frame_index].Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_RENDER_TARGET);
	m_command_list->ResourceBarrier(1, &to_render_target);

	CD3DX12_CPU_DESCRIPTOR_HANDLE rtv_handle(m_rtv_heap->GetCPUDescriptorHandleForHeapStart(), m_frame_index, m_rtv_descriptor_size);
	m_command_list->OMSetRenderTargets(1, &rtv_handle, false, nullptr);

	ID3D12DescriptorHeap* imgui_heaps[] = { m_imgui_srv_heap.Get() };
	m_command_list->SetDescriptorHeaps(_countof(imgui_heaps), imgui_heaps);

	ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), m_command_list.Get());

	auto to_copy_dest = CD3DX12_RESOURCE_BARRIER::Transition(m_swap_chain_rtv[m_frame_index].Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_DEST);
	m_command_list->ResourceBarrier(1, &to_copy_dest);
}

/// Renderer_Dx12::present
void Renderer_Dx12::present() {
	// The post-process pass leaves the back buffer in CopyDest (see
	// render_post_process); indicate it will now be used to present.
	auto res_barrier = CD3DX12_RESOURCE_BARRIER::Transition(m_swap_chain_rtv[m_frame_index].Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PRESENT);
	m_command_list->ResourceBarrier(1, &res_barrier);

	const HRESULT close_hr = m_command_list->Close();
	if (FAILED(close_hr)) {
		log_d3d12_debug_messages(m_device.Get());
	}
	blk::error_check(close_hr, "Renderer_Dx12::present() - m_command_list->Close() failed (hr=0x%08X, device_removed_reason=0x%08X) - see the D3D12 debug layer messages logged just above", close_hr, m_device->GetDeviceRemovedReason());

	// Execute command lists
	ID3D12CommandList* const command_lists[] = { m_command_list.Get() };
	m_queue->ExecuteCommandLists(_countof(command_lists), command_lists);

	wait_on_fence();

	// After the fence, so the CopyTextureRegion recorded in the gbuffer pass
	// has definitely landed. This renderer blocks on the GPU every frame, which
	// is what lets a pick resolve in the same frame it was requested instead of
	// needing a per-frame-in-flight readback ring.
	resolve_entity_id_pick();

	// Drain every frame, not just on a Close() failure -- anything the debug
	// layer raises while the frame is being recorded (bad state, unbound
	// descriptor, PSO/RTV format mismatch) would otherwise be cleared without
	// ever being seen.
	log_d3d12_debug_messages(m_device.Get());

	// Present
	blk::error_check(m_swap_chain->Present(1, 0));

	// Wait for previous frame (todo)
	//wait_on_fence();

	m_frame_index = m_swap_chain->GetCurrentBackBufferIndex();
}

/// Renderer_Dx12::create_gpu_pipeline
RenderPipeline* Renderer_Dx12::create_gpu_pipeline(const string& friendly_name, const string& relative_shader_path) {
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
	const bool is_point_cloud = blk::std_contains(friendly_name, "gs_draw");

	u32 blend_type = 0;
	if (friendly_name.find("_blend") != friendly_name.npos) {
		blend_type = 2;
	} else if (friendly_name.find("_add") != friendly_name.npos || is_light) {
		blend_type = 1;
	}

	Microsoft::WRL::ComPtr<ID3DBlob> errors;

	wstring pipeline_path;
	WStringFromString(pipeline_path, absolute_shader_path);

	// Compiled from an in-memory buffer with no associated path, so DXC's
	// default include handler has nothing to resolve a relative #include
	// against -- point it at the shader's own directory explicitly.
	const std::wstring shader_include_dir = std::filesystem::path(absolute_shader_path).parent_path().wstring();

	std::vector<char> vertex_shader;
	std::vector<char> pixel_shader;

	// Parenthesized to dodge the min/max macros pulled in by <Windows.h>.
	const auto shader_text_write_time = (std::max)(
		fs::last_write_time(absolute_shader_path),
		newest_shared_include_write_time(shader_include_dir)
	);

	// Compile vertex shader
	std::filesystem::path shader_output_file(absolute_shader_path.c_str());
	shader_output_file.replace_extension(BLK_SHADER_CACHE_TAG ".vso");

	if (fs::exists(shader_output_file) && fs::last_write_time(shader_output_file) > shader_text_write_time) {
		std::ifstream shader_bin(shader_output_file, std::ios::binary | std::ios::ate);
		if (!shader_bin.is_open()) {
			blk::warn("Renderer_Dx12::create_gpu_pipeline() - %s", shader_output_file.c_str());
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
			L"-T", g_vertex_shader_profile,
			L"-I", shader_include_dir.c_str(),
		};
		append_shader_codegen_args(arguments);

		ComPtr<IDxcResult> result;
		if (FAILED(m_dxc_compiler->Compile(&sourceBuffer, arguments.data(), (UINT)arguments.size(), m_dxc_include_handler.Get(), IID_PPV_ARGS(&result)))) {
			throw std::runtime_error("Shader compilation failed.");
		}

		ComPtr<IDxcBlobUtf8> errors;
		ComPtr<IDxcBlobUtf16> unused_blob;
		HRESULT hr = result->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&errors), &unused_blob);
		blk::error_check(!FAILED(hr) && (errors == nullptr || errors->GetStringLength() == 0), "Shader comp0ilation errors: %s\n", std::string(errors->GetStringPointer()).c_str());

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
		arguments.push_back(g_pixel_shader_profile);
		arguments.push_back(L"-I");
		arguments.push_back(shader_include_dir.c_str());
		append_shader_codegen_args(arguments);


		// Compile the shader
		ComPtr<IDxcResult> result;
		blk::error_check(
			m_dxc_compiler->Compile(
				&sourceBuffer,
				arguments.data(),
				(u32)arguments.size(),
				m_dxc_include_handler.Get(),
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

		// Only the pipelines render_gbuffer_internal actually draws with get
		// the 5th (EntityId) target. shadow_projection and gs_draw land in this
		// branch too -- neither is light-blended -- but they bind a single
		// render target in their own passes, so declaring an output they never
		// write would just add a debug-layer complaint for no gain.
		if (!is_shadow_proj && !is_point_cloud) {
			psoDesc.NumRenderTargets = 5;
			psoDesc.RTVFormats[4] = DXGI_FORMAT_R32_FLOAT;
		}
	} else {
		psoDesc.NumRenderTargets = 1;
		psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
	}

	RenderPipeline_Dx12* const pipe = new RenderPipeline_Dx12();
	blk::error_check(m_device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&pipe->m_pipeline_state)));

	return (RenderPipeline*)pipe;
}

/// Renderer_Dx12::create_compute_pipeline
RenderPipeline* Renderer_Dx12::create_compute_pipeline(const string& friendly_name, const string& relative_shader_path) {

	string absolute_shader_path = "./";
	u32 num_iterations = 0;
	while (fs::exists(absolute_shader_path + "/blk_engine/") == false && num_iterations < 10) {
		absolute_shader_path += "../";
		num_iterations++;
	}
	absolute_shader_path = absolute_shader_path + relative_shader_path;
	Microsoft::WRL::ComPtr<ID3DBlob> errors;

	wstring pipeline_path;
	WStringFromString(pipeline_path, absolute_shader_path);

	// Compiled from an in-memory buffer with no associated path, so DXC's
	// default include handler has nothing to resolve a relative #include
	// against -- point it at the shader's own directory explicitly.
	const std::wstring shader_include_dir = std::filesystem::path(absolute_shader_path).parent_path().wstring();

	std::vector<char> compute_shader;

	// Parenthesized to dodge the min/max macros pulled in by <Windows.h>.
	const auto shader_text_write_time = (std::max)(
		fs::last_write_time(absolute_shader_path),
		newest_shared_include_write_time(shader_include_dir)
	);

	// Compile compute6 shader
	std::filesystem::path shader_output_file(absolute_shader_path.c_str());
	shader_output_file.replace_extension(BLK_SHADER_CACHE_TAG ".cso");
	if (fs::exists(shader_output_file) && fs::last_write_time(shader_output_file) > shader_text_write_time) {
		std::ifstream shader_bin(shader_output_file, std::ios::binary | std::ios::ate);
		if (!shader_bin.is_open()) {
			blk::warn("Renderer_Dx12::create_pipeline() - %s", shader_output_file.c_str());
			return nullptr;
		}

		std::streamsize file_size = shader_bin.tellg();
		shader_bin.seekg(0, std::ios::beg);

		// Read the file into a buffer
		compute_shader.resize(file_size);
		shader_bin.read(compute_shader.data(), file_size);
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

		wstring entry_point = L"main";

		// Prepare shader compilation arguments
		std::vector<LPCWSTR> arguments;
		arguments.push_back(L"-E");
		arguments.push_back(entry_point.c_str());
		arguments.push_back(L"-T");
		arguments.push_back(g_compute_shader_profile);
		arguments.push_back(L"-I");
		arguments.push_back(shader_include_dir.c_str());
		append_shader_codegen_args(arguments);

		// Compile the shader
		ComPtr<IDxcResult> result;
		blk::error_check(
			m_dxc_compiler->Compile(
				&sourceBuffer,
				arguments.data(),
				(u32)arguments.size(),
				m_dxc_include_handler.Get(),
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
		compute_shader.resize(blob_byte_size);
		std::memcpy(compute_shader.data(), shader_blob->GetBufferPointer(), blob_byte_size);

		// Write binary to file
		std::ofstream ofs(shader_output_file, std::ios::binary);
		ofs.write(reinterpret_cast<const char*>(shader_blob->GetBufferPointer()), shader_blob->GetBufferSize());
		ofs.close();
	}


	D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc = {};
	psoDesc.pRootSignature = m_gs_sort_signature.Get();
	psoDesc.CS = CD3DX12_SHADER_BYTECODE(compute_shader.data(), compute_shader.size());
	psoDesc.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;


	RenderPipeline_Dx12* const pipe = new RenderPipeline_Dx12();
	blk::error_check(m_device->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(&pipe->m_pipeline_state)));
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

	load_pipeline(ERenderPipelineType::Gpu, "static_model_base", "/blk_engine/assets/shaders/static_model.hlsl");
	load_pipeline(ERenderPipelineType::Gpu, "static_model_shadow_depth", "/blk_engine/assets/shaders/static_model.hlsl");

	load_pipeline(ERenderPipelineType::Gpu, "skinned_base", "/blk_engine/assets/shaders/skinned_model.hlsl");
	load_pipeline(ERenderPipelineType::Gpu, "skinned_shadow_depth", "/blk_engine/assets/shaders/skinned_model.hlsl");

	load_pipeline(ERenderPipelineType::Gpu, "sprite_particle_blend", "/blk_engine/assets/shaders/sprite_particle.hlsl");
	load_pipeline(ERenderPipelineType::Gpu, "sprite_particle_add", "/blk_engine/assets/shaders/sprite_particle.hlsl");
	load_pipeline(ERenderPipelineType::Gpu, "mesh_particle_add", "/blk_engine/assets/shaders/mesh_particle.hlsl");

	load_pipeline(ERenderPipelineType::Gpu, "directional_light", "/blk_engine/assets/shaders/directional_light.hlsl");
	load_pipeline(ERenderPipelineType::Gpu, "point_light", "/blk_engine/assets/shaders/point_light.hlsl");
	load_pipeline(ERenderPipelineType::Gpu, "directional_shadow_projection", "/blk_engine/assets/shaders/directional_shadow.hlsl");

	load_pipeline(ERenderPipelineType::Gpu, "terrain", "/blk_engine/assets/shaders/terrain.hlsl");

	load_pipeline(ERenderPipelineType::Gpu, "gs_draw", "/blk_engine/assets/shaders/gaussian_splat_draw.hlsl");
	load_pipeline(ERenderPipelineType::Compute, "gs_sort", "/blk_engine/assets/shaders/gaussian_splat_sort.hlsl");
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

/// Renderer_Dx12::render_shadow_cascades
///
/// Split from the old single render_shadows() so the render graph can revert
/// ShadowDepth to Common between this pass and render_shadow_composite --
/// the composite reads ShadowDepth via SRV, which relies on the same
/// Common-state read promotion every other gbuffer-style read in this engine
/// uses. Bracketing both cascade rendering and the composite as one pass's
/// writes (as the first cut of this split did) left ShadowDepth stuck in
/// DepthWrite for the composite's read, corrupting the shadow projection.
void Renderer_Dx12::render_shadow_cascades(const RenderCamera& camera, const ERenderPassMask& render_pass_mask) {
	const kbDirectionalLightComponent* dir_light = nullptr;
	for (const auto light : light_components()) {
		if (light->casts_shadow() && light->IsA(kbDirectionalLightComponent::GetType())) {
			dir_light = (kbDirectionalLightComponent*)light;
			break;
		}
	}

	if (!dir_light) {
		return;
	}

	light_matrices.clear();

	// Update constant buffer
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

	// Heaps before root signature -- see render_gbuffer_internal's comment.
	{
		ID3D12DescriptorHeap* const heaps[] = { m_cbv_srv_descriptor_heap.Get(), m_sampler_descriptor_heap.Get() };
		m_command_list->SetDescriptorHeaps(_countof(heaps), heaps);
	}

	m_command_list->SetGraphicsRootSignature(m_root_signature.Get());

	m_command_list->RSSetScissorRects(1, &m_scissor_rect);

	// ShadowDepth is transitioned by the render graph (see
	// Renderer::frame_pass_topology()) before this pass runs, and back to
	// Common after -- render_shadow_composite then reads it via SRV, relying
	// on that Common state for the implicit
	// read promotion this engine's gbuffer-style reads all use.

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
	m_command_list->SetGraphicsRootDescriptorTable(3, bone_descriptor_handle);

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
		const Vec3 look_at_point = camera.view_position + cam_dir * (prev_cascade_dist + (cascade_dists[i] - prev_cascade_dist) * 0.5f);
		const float half_fov = g_fov * 0.5f;
		const float dist_to_corner = cascade_dists[i] / cos(half_fov);
		Vec3 corner_vert = camera.view_position + dist_to_corner * ul;
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

			if (!render_pass_in_mask(render_comp->render_pass(), render_pass_mask)) {
				continue;
			}

			if (render_comp->IsA(StaticModelComponent::GetType())) {
				const StaticModelComponent* const model_comp = static_cast<const StaticModelComponent*>(render_comp);
				model = model_comp->model();

				//	blk::log("--> %d", model->GetMaterials()[0].get_shader()->GetBlendOp());
				m_command_list->SetPipelineState(get_pipeline_state("static_model_shadow_depth"));

				vertex_buffer = (RenderBuffer_Dx12*)model->m_vertex_buffer;
				index_buffer = (RenderBuffer_Dx12*)model->m_index_buffer;

				const auto vertex_buf_view = vertex_buffer->vertex_buffer_view();
				m_command_list->IASetVertexBuffers(0, 1, &vertex_buf_view);

				const auto index_buf_view = index_buffer->index_buffer_view();
				m_command_list->IASetIndexBuffer(&index_buf_view);
			} else if (render_comp->IsA(SkeletalModelComponent::GetType())) {
				const SkeletalModelComponent* const skel = static_cast<const SkeletalModelComponent*>(render_comp);
				model = skel->model();

				m_command_list->SetPipelineState(get_pipeline_state("skinned_shadow_depth"));

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
				m_command_list->SetGraphicsRoot32BitConstant(4, (u32)m_bone_draws, 0);
				m_bone_draws++;
			} else if (render_comp->IsA(ParticleComponent::GetType())) {
				continue;
			} else {
				continue;
			}

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

			m_command_list->SetGraphicsRoot32BitConstant(2, (u32)m_frame_draws, 0);

			// No SRV table bind here -- the shadow_depth_ps entry point (both
			// static_model.hlsl and skinned_model.hlsl) never samples a
			// texture, so the color_tex table this used to bind was already
			// dead as far as the shader was concerned.
			m_command_list->DrawIndexedInstanced(index_buffer->num_elements(), 1, 0, 0, 0);
			m_frame_draws++;
		}
	}
}

/// Renderer_Dx12::render_shadow_composite
///
/// The other half of the old render_shadows() -- projects the cascades
/// render_shadow_cascades just wrote into the Lighting target. Registered as
/// its own graph pass so ShadowDepth reverts to Common (and becomes SRV-
/// readable) between the two; see render_shadow_cascades's comment.
void Renderer_Dx12::render_shadow_composite(const RenderCamera& camera) {
	const kbDirectionalLightComponent* dir_light = nullptr;
	for (const auto light : light_components()) {
		if (light->casts_shadow() && light->IsA(kbDirectionalLightComponent::GetType())) {
			dir_light = (kbDirectionalLightComponent*)light;
			break;
		}
	}

	if (!dir_light) {
		return;
	}

	// Project Shadows
	m_command_list->RSSetViewports(1, &m_view_port);
	m_command_list->RSSetScissorRects(1, &m_scissor_rect);

	// Set Lighting Buffer
	const u32 gbuffer_start = Renderer::max_frames() + (ERenderTarget::Count - 1) * m_frame_index;
	CD3DX12_CPU_DESCRIPTOR_HANDLE rtv_handle(m_rtv_heap->GetCPUDescriptorHandleForHeapStart(), gbuffer_start + Lighting, m_rtv_descriptor_size);
	m_command_list->OMSetRenderTargets(1, &rtv_handle, false, nullptr);

	const float clear_color[] = { 0.0f, 0.0f, 0.f, 0.0f };
	m_command_list->ClearRenderTargetView(rtv_handle, clear_color, 0, nullptr);

	{
		m_command_list->SetPipelineState(get_pipeline_state("directional_shadow_projection"));
		m_command_list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		m_command_list->IASetVertexBuffers(0, 1, &m_quad_vb_view);

		// Texture -- directional_shadow.hlsl's gbuffer_textures[3]/[5] read
		// SceneDepth/ShadowDepth through ResourceDescriptorHeap[] at
		// gbuffer_srv_base + 3/5 (bindless -- no table bind needed). Same as
		// render_lights_internal, the base must land on this frame's gbuffer
		// SRV block, not always frame_index 0's.
		const u32 gbuffer_srv_start = g_srv_descriptor_start + ERenderTarget::Count * m_frame_index;

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
		light_instance_data->player_inv_view_proj = camera.inv_view_projection_matrix;
		light_instance_data->player_camera_position = Vec4(camera.view_position, 1);
		light_instance_data->gbuffer_srv_base = Vec4((f32)gbuffer_srv_start, 0.f, 0.f, 0.f);
		m_command_list->SetGraphicsRoot32BitConstant(2, (u32)m_frame_draws, 0);

		m_command_list->DrawInstanced(6, 1, 0, 0);
		m_frame_draws++;
	}
}
