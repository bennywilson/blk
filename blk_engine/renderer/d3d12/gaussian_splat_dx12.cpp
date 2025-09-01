/// gaussian_splat_dx12.cpp
///
/// 2025 blk 1.0

#include <functional>
#include <execution>
#include "blk_core.h"
#include "entity_header.h"
#include "renderer_dx12.h"
#include "d3dx12.h"
#include "d3d12_defs.h"
#include "render_component.h"

std::thread g_sort_thread;
std::atomic<bool> g_sort_running = false;

std::vector<u32> g_sorted_indices;
std::mutex g_sort_mutex;
std::atomic<u64> g_sort_indices_version = 0;

void splat_sort_thread(const Mat4& view_matrix, const std::vector<PointCloudSample>& point_cloud) {
	std::vector<u32> sorted_indices(point_cloud.size());

	while (g_sort_running) {
		// Sort indices
		{
			std::iota(sorted_indices.begin(), sorted_indices.end(), 0);
			for (u32 i = 0; i < (u32)point_cloud.size(); ++i) {
				sorted_indices[i] = i;
			}

			static std::vector<f32> view_depths;
			view_depths.resize(sorted_indices.size());

			// Parallel transform pass: compute view-space depth for each index
			std::transform(std::execution::par,
						   sorted_indices.begin(), sorted_indices.end(),
						   view_depths.begin(),
						   [&](u32 idx) {
									   return view_matrix.transform_point(point_cloud[idx].position).z;
						   });

			// Stable sort indices by descending depth (far → near)
			std::stable_sort(sorted_indices.begin(), sorted_indices.end(),
							 [&](u32 a, u32 b) {
										 return view_depths[a] > view_depths[b];
							 });
		}

		// Sort finish, let the render thread know&
		{
			std::lock_guard<std::mutex> lock(g_sort_mutex);
			g_sorted_indices = sorted_indices;
			g_sort_indices_version.fetch_add(1, std::memory_order_release);
		}

		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
}

/// Renderer_Dx12::initialize_gaussian_splatting
void Renderer_Dx12::initialize_gaussian_splatting(const GaussianSplatComponent* const gs) {
	blk::error_check(m_command_allocator->Reset());
	blk::error_check(m_command_list->Reset(m_command_allocator.Get(), nullptr));

	m_gaussian_splat = (GaussianSplatComponent*)gs;
	auto point_cloud = m_gaussian_splat->point_cloud();
	for (i32 i = 0; i < point_cloud->size(); i++) {

		const PointCloudSample& cur_point = (*point_cloud)[i];

		// GPU Point cloud
		{
			g_point_cloud[i].position.set(cur_point.position.x, cur_point.position.y, cur_point.position.z, 0.f);
			g_point_cloud[i].rotation = cur_point.rotation;

			// Normalize raw opacity via sigmoid to ensure [0,1] alpha range.
			// Prevents blending artifacts from out-of-bounds or noisy inputs.
			// Ref: https://github.com/nvpro-samples/vk_gaussian_splatting/blob/f40720ab318d86ddcf29ce61ebcbf5dc0ded9bd6/src/splat_set_vk.cpp#L304
			const f32 normalized_opacity = kbClamp(1.0f / (1.0f + std::exp(-cur_point.opacity)), 0.f, 1.f);

			// Convert scale from log-space to linear for rendering.
			// ML often operates in log-space for stability, precision, and to enforce strictly positive outputs.
			// Ref: https://github.com/nvpro-samples/vk_gaussian_splatting/blob/f40720ab318d86ddcf29ce61ebcbf5dc0ded9bd6/src/splat_set_vk.cpp#L770
			const Vec3 linear_scale(exp(cur_point.scale.x), exp(cur_point.scale.y), exp(cur_point.scale.z));
			g_point_cloud[i].scale3d_opacity.set(linear_scale.x, linear_scale.y, linear_scale.z, normalized_opacity);

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
	}

	const size_t num_elements = point_cloud->size();
	const size_t padded_elements = size_t(1) << static_cast<size_t>(ceil(log2(num_elements)));
	if (m_gaussian_splat->gpu_sort()) {
		for (i32 i = (i32)point_cloud->size(); i < padded_elements; i++) {
			g_point_cloud_indices[i] = i;
		}
	}

	// Upload gpu points for rendering
	{
		auto to_copy_dest = CD3DX12_RESOURCE_BARRIER::Transition(
			m_point_cloud_default_heap.Get(),
			D3D12_RESOURCE_STATE_COMMON,
			D3D12_RESOURCE_STATE_COPY_DEST
		);
		m_command_list->ResourceBarrier(1, &to_copy_dest);

		const u32 buffer_size = sizeof(PointCloudSampleInstance) * g_max_point_cloud_points;
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

		auto to_shader_read = CD3DX12_RESOURCE_BARRIER::Transition(
			m_point_cloud_default_heap.Get(),
			D3D12_RESOURCE_STATE_COPY_DEST,
			D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE
		);
		m_command_list->ResourceBarrier(1, &to_shader_read);
	}

	// Upload gpu points for sorting
	{
		auto to_copy_dest = CD3DX12_RESOURCE_BARRIER::Transition(
			m_point_cloud_index_default_heap.Get(),
			D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
			D3D12_RESOURCE_STATE_COPY_DEST
		);
		m_command_list->ResourceBarrier(1, &to_copy_dest);

		const u32 buffer_size = (u32)(sizeof(u32) * (m_gaussian_splat->gpu_sort() ? padded_elements : num_elements));
		void* mapped_data = nullptr;
		CD3DX12_RANGE read_range(0, 0);
		m_command_list->CopyBufferRegion(
			m_point_cloud_index_default_heap.Get(), 0,
			m_point_cloud_index_upload_heap.Get(), 0,
			buffer_size);

		auto to_shader_read = CD3DX12_RESOURCE_BARRIER::Transition(
			m_point_cloud_index_default_heap.Get(),
			D3D12_RESOURCE_STATE_COPY_DEST,
			D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE
		);
		m_command_list->ResourceBarrier(1, &to_shader_read);
	}

	blk::error_check(m_command_list->Close());
	ID3D12CommandList* const command_lists[] = { m_command_list.Get() };
	m_queue->ExecuteCommandLists(_countof(command_lists), command_lists);
	wait_on_fence();

	if (!gs->gpu_sort()) {
		g_sort_thread = std::thread([&]() {
			g_sort_running = true;
			splat_sort_thread(m_view_matrix, *m_gaussian_splat->point_cloud());
		});
	}
}

/// Renderer_Dx12::shutdown_gaussian_splatting
void Renderer_Dx12::shutdown_gaussian_splatting() {
	g_sort_running = false;
	if (g_sort_thread.joinable()) {
		g_sort_thread.join();
	}
	m_gaussian_splat = nullptr;
}


/// Renderer_Dx12::render_point_clouds
void Renderer_Dx12::render_point_clouds() {
	if (!m_gaussian_splat) {
		return;
	}

	const std::vector<PointCloudSample>* point_cloud = m_gaussian_splat->point_cloud();
	g_global_uniform->view_projection = m_view_projection_matrix;
	g_global_uniform->inv_view_proj = (*(Mat4*)&m_inv_view_projection_matrix);
	g_global_uniform->camera_pos = Vec4(m_view_position, 1.f);
	g_global_uniform->view = m_view_matrix;

	// Sort gs
	static bool prev_gpu_sort = !m_gaussian_splat->gpu_sort();
	const size_t num_elements = point_cloud->size();
	const size_t padded_elements = size_t(1) << static_cast<size_t>(ceil(log2(num_elements)));

	static u64 last_sort_version = 0;
	const u64 sort_version = g_sort_indices_version.load(std::memory_order_acquire);
	if (sort_version != last_sort_version) {
		std::lock_guard<std::mutex> lock(g_sort_mutex);
		memcpy(g_point_cloud_indices, g_sorted_indices.data(), sizeof(u32) * g_sorted_indices.size());

		last_sort_version = sort_version;

		const u32 buffer_size = static_cast<u32>(sizeof(u32) * point_cloud->size());
		CD3DX12_RANGE read_range(0, 0);
		m_command_list->CopyBufferRegion(
			m_point_cloud_index_default_heap.Get(), 0,
			m_point_cloud_index_upload_heap.Get(), 0,
			buffer_size);
	}

	// Sort
	if (m_gaussian_splat->gpu_sort()) {
		prev_gpu_sort = true;

		RenderPipeline_Dx12* const gs_sort_pso = (RenderPipeline_Dx12*)get_pipeline("gs_sort");

		const auto descriptor_size = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
		CD3DX12_GPU_DESCRIPTOR_HANDLE cbvSrvHandle(m_cbv_srv_descriptor_heap->GetGPUDescriptorHandleForHeapStart(), 0, descriptor_size);

		uint num_elements = static_cast<uint>(point_cloud->size());
		uint padded_elements = 1 << static_cast<uint>(ceil(log2(num_elements)));
		const uint num_groups_x = (padded_elements + 255) / 256;

		struct SortConstants {
			uint j;
			uint k;
		};

		// Set up pipeline
		m_command_list->SetComputeRootSignature(m_gs_sort_signature.Get());
		m_command_list->SetPipelineState(gs_sort_pso->m_pipeline_state.Get());

		// Bind resources
		m_command_list->SetComputeRootConstantBufferView(0, m_scene_cbv_upload_heap->GetGPUVirtualAddress()); // b0
		m_command_list->SetComputeRootShaderResourceView(1, m_point_cloud_default_heap->GetGPUVirtualAddress()); // t0
		m_command_list->SetComputeRootUnorderedAccessView(2, m_point_cloud_index_default_heap->GetGPUVirtualAddress()); // u0

		for (uint k = 2; k <= padded_elements; k <<= 1) {
			for (uint j = k >> 1; j >= 1; j >>= 1) {
				// Push j and k as root constants (slot 3)
				SortConstants sc = { j, k };
				m_command_list->SetComputeRoot32BitConstants(3, 2, &sc, 0); // slot 3, 2 DWORDs, offset 0

				// Dispatch
				m_command_list->Dispatch(num_groups_x, 1, 1);

				// Insert UAV barrier between passes
				auto barrier = CD3DX12_RESOURCE_BARRIER::UAV(m_point_cloud_index_default_heap.Get());
				m_command_list->ResourceBarrier(1, &barrier);
			}
		}
	}


	CD3DX12_CPU_DESCRIPTOR_HANDLE dsv_handle(m_depth_stencil_heap->GetCPUDescriptorHandleForHeapStart(), 0, m_depth_target_descriptor_size);
	CD3DX12_CPU_DESCRIPTOR_HANDLE rtv_handle(m_rtv_heap->GetCPUDescriptorHandleForHeapStart(), m_frame_index, m_rtv_descriptor_size);
	m_command_list->OMSetRenderTargets(1, &rtv_handle, false, &dsv_handle);

	m_command_list->SetGraphicsRootSignature(m_root_signature.Get());
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

	g_global_uniform->splat_params.x = m_gaussian_splat->splat_falloff();
	g_global_uniform->splat_params.y = m_gaussian_splat->splat_scale();
	g_global_uniform->splat_params.z = m_gaussian_splat->contrast();
	g_global_uniform->splat_params.w = (f32)point_cloud->size();


	RenderPipeline_Dx12* const pipe = (RenderPipeline_Dx12*)get_pipeline("gs_draw");
	m_command_list->SetPipelineState(pipe->m_pipeline_state.Get());

	m_command_list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	// Optional: set dummy vertex buffer if needed by IA stage
	D3D12_VERTEX_BUFFER_VIEW dummy_vbv = {};
	m_command_list->IASetVertexBuffers(0, 1, &dummy_vbv);

	if (m_gaussian_splat->gpu_sort()) {
		m_command_list->DrawInstanced((u32)padded_elements * 6, 1, 0, 0);
	} else {
		m_command_list->DrawInstanced((u32)point_cloud->size() * 6, 1, 0, 0);
	}
}
