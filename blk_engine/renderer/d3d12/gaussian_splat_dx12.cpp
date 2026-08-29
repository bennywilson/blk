/// gaussian_splat_dx12.cpp
///
/// 2025-2026 blk 1.0

#include <DirectXPackedVector.h>
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

/**
 * Highly optimized, inlined transform that ONLY computes the view-space Z component.
 * This completely bypasses X and Y calculations to maximize raw throughput.
 */
inline float compute_point_depth_z(const Vec3& point, const Mat4& view_matrix) {
	// Directly accessing the matrix columns matching your library's layout
	return (point.x * view_matrix[0][2]) +
		(point.y * view_matrix[1][2]) +
		(point.z * view_matrix[2][2]) +
		view_matrix[3][2];
}

void splat_sort_thread(const Mat4& view_matrix, const std::vector<PointCloudSample>& point_cloud) {
	const size_t num_points = point_cloud.size();
	if (num_points == 0)
	{
		return;
	}

	// Allocate tracking vectors OUTSIDE the while loop.
	// This ensures we allocate heap memory exactly once, completely eliminating allocation churn.
	std::vector<u32> staging_indices(num_points);
	std::vector<f32> view_depths(num_points);
	std::vector<int> point_bins(num_points);

	// 8192 bins provides sub-millimeter precision. Fits perfectly inside L1/L2 cache.
	const int NUM_BINS = 8192;
	std::vector<u32> offsets(NUM_BINS);

	while (g_sort_running) {
		const Mat4 current_view = view_matrix;

		// Fill view_depths each point's depth
		std::transform(std::execution::par_unseq,
					   point_cloud.begin(), point_cloud.end(),
					   view_depths.begin(),
					   [&](const PointCloudSample& p) {
						   return compute_point_depth_z(p.position, current_view);
					   });

		// Find the max/min depths in the list
		const auto [min_it, max_it] = std::minmax_element(std::execution::par_unseq, view_depths.begin(), view_depths.end());
		const f32 min_depth = *min_it;
		const f32 depth_range = max(*max_it - min_depth, 0.0001f);

		// Create histogram
		std::vector<u32> histogram(NUM_BINS);
		std::fill(histogram.begin(), histogram.end(), 0);

		for (u32 i = 0; i < num_points; ++i) {
			const f32 normalized = (view_depths[i] - min_depth) / depth_range;
			i32 bin = static_cast<i32>(normalized * (NUM_BINS - 1));
			bin = bin < 0 ? 0 : (bin >= NUM_BINS ? NUM_BINS - 1 : bin);

			point_bins[i] = bin;
			histogram[bin]++;
		}

		// Prefix Sum (Far → Near Rendering Order)
		u32 current_offset = 0;
		for (int i = NUM_BINS - 1; i >= 0; --i) {
			offsets[i] = current_offset;
			current_offset += histogram[i];
		}

		// Scatter Indices (Linear O(N))
		for (u32 i = 0; i < num_points; ++i) {
			int bin = point_bins[i];
			u32 dest_index = offsets[bin]++;
			staging_indices[dest_index] = i;
		}

		// Low-Contention Double-Buffered Swap
		{
			std::lock_guard<std::mutex> lock(g_sort_mutex);
			// Swaps internal pointers instantly (O(1)). Holds the lock for nanoseconds.
			std::swap(g_sorted_indices, staging_indices);
			g_sort_indices_version.fetch_add(1, std::memory_order_release);
		}

		// Re-ensure staging container matches size invariants after receiving the swapped buffer
		if (staging_indices.size() != num_points) {
			staging_indices.resize(num_points);
		}

		// Prevent thread from continuously starving other background tasks
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
}

/// Renderer_Dx12::initialize_gaussian_splatting
void Renderer_Dx12::initialize_gaussian_splatting(const GaussianSplatComponent* const gs) {
	blk::error_check(m_command_allocator->Reset());
	blk::error_check(m_command_list->Reset(m_command_allocator.Get(), nullptr));

	m_gaussian_splat = (GaussianSplatComponent*)gs;
	auto point_cloud = m_gaussian_splat->point_cloud();

	const size_t num_points = point_cloud->size(); 
	blk::error_check(num_points <= g_max_point_cloud_points, "Point cloud size %d exceeds %d", num_points, g_max_point_cloud_points);

	for (i32 i = 0; i < point_cloud->size() && i < g_max_point_cloud_points; i++) {

		const PointCloudSample& cur_point = (*point_cloud)[i];
		
		// GPU Point cloud
		{
			g_point_cloud[i].position.set(cur_point.position.x, cur_point.position.y, cur_point.position.z, 0.f);
			g_point_cloud[i].rotation = cur_point.rotation;

			// Normalize raw opacity via sigmoid to ensure [0,1] alpha range.
			const f32 normalized_opacity = kbClamp(1.0f / (1.0f + std::exp(-cur_point.opacity)), 0.f, 1.f);

			// Convert scale from log-space to linear for rendering.
			const Vec3 linear_scale(exp(cur_point.scale.x), exp(cur_point.scale.y), exp(cur_point.scale.z));
			g_point_cloud[i].scale3d_opacity.set(linear_scale.x, linear_scale.y, linear_scale.z, normalized_opacity);

			g_point_cloud[i].sh0.set(cur_point.f_dc.x, cur_point.f_dc.y, cur_point.f_dc.z, 0.f);

			// Map SH coefficients: f_rest is packed as [All Red (0-14), All Green (15-29), All Blue (30-44)].
			// We extract R, G, and B components using a stride of 15 to assemble per-coefficient RGB vectors.
			// Converted to f16 (Half) here to keep this array's upload footprint at 48 bytes instead of 96 --
			// but gaussian_splat_draw.hlsl's SplatPoint.f_rest currently reads this at a 4-byte (not 2-byte)
			// stride, so the packing doesn't survive the round trip intact. See the FIXME on SplatPoint in
			// that shader and on sh_rest in PointCloudSampleInstance (renderer_dx12.h).
			for (int n = 0; n < 8; ++n) {
				int dest_idx = n * 3;
				g_point_cloud[i].sh_rest[dest_idx + 0] = DirectX::PackedVector::XMConvertFloatToHalf(cur_point.f_rest[n]);      // R
				g_point_cloud[i].sh_rest[dest_idx + 1] = DirectX::PackedVector::XMConvertFloatToHalf(cur_point.f_rest[n + 15]); // G
				g_point_cloud[i].sh_rest[dest_idx + 2] = DirectX::PackedVector::XMConvertFloatToHalf(cur_point.f_rest[n + 30]); // B
			}

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

		const u64 buffer_size = sizeof(PointCloudSampleInstance) * g_max_point_cloud_points;
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
void Renderer_Dx12::render_point_clouds(const RenderCamera& camera) {
	if (!m_gaussian_splat) {
		return;
	}

	const std::vector<PointCloudSample>* point_cloud = m_gaussian_splat->point_cloud();

	// log2(0) is -infinity, and casting that to size_t below is undefined
	// behavior -- guard against an empty cloud (e.g. its source .ply failed
	// to load) instead of computing a garbage padded_elements count.
	if (point_cloud->empty()) {
		return;
	}

	g_global_uniform->view_projection = camera.view_projection_matrix;
	g_global_uniform->inv_view_proj = (*(Mat4*)&camera.inv_view_projection_matrix);
	g_global_uniform->camera_pos = Vec4(camera.view_position, 1.f);
	g_global_uniform->view = camera.view_matrix;

	// Sort gs
	static bool prev_gpu_sort = !m_gaussian_splat->gpu_sort();
	const size_t num_elements = point_cloud->size();
	const size_t padded_elements = size_t(1) << static_cast<size_t>(ceil(log2(num_elements)));

	static u64 last_sort_version = 0;
	const u64 sort_version = g_sort_indices_version.load(std::memory_order_acquire);

	if (sort_version != last_sort_version) {
		// Persistent local buffer to retain capacity across frames
		static std::vector<u32> render_staging_indices;

		// Lock ONLY to swap pointers. 
		{
			std::lock_guard<std::mutex> lock(g_sort_mutex);
			std::swap(g_sorted_indices, render_staging_indices);
			last_sort_version = sort_version;
		}

		// CRITICAL: memcpy happens COMPLETELY OUTSIDE the lock!
		// The sorting thread can immediately begin its next pass unhindered.
		memcpy(g_point_cloud_indices, render_staging_indices.data(), sizeof(u32) * render_staging_indices.size());

		const u32 buffer_size = static_cast<u32>(sizeof(u32) * point_cloud->size());
		m_command_list->CopyBufferRegion(
			m_point_cloud_index_default_heap.Get(), 0,
			m_point_cloud_index_upload_heap.Get(), 0,
			buffer_size);
	}
	// Sort
	if (m_gaussian_splat->gpu_sort()) {
		prev_gpu_sort = true;

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
		m_command_list->SetPipelineState(get_pipeline_state("gs_sort"));

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

	// SceneColor is already RenderTarget here -- the render graph put it there
	// for render_lights_internal, and this pass runs right after in the same
	// graph node's writes, so it stays bound until translucency's implicit
	// reuse of this OMSetRenderTargets call finishes drawing.
	CD3DX12_CPU_DESCRIPTOR_HANDLE dsv_handle(m_depth_stencil_heap->GetCPUDescriptorHandleForHeapStart(), 0, m_depth_target_descriptor_size);
	const u32 gbuffer_start = Renderer::max_frames() + (ERenderTarget::Count - 1) * m_frame_index;
	CD3DX12_CPU_DESCRIPTOR_HANDLE rtv_handle(m_rtv_heap->GetCPUDescriptorHandleForHeapStart(), gbuffer_start + SceneColor, m_rtv_descriptor_size);
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

	g_global_uniform->splat_params_2.x = (f32)m_gaussian_splat->max_sh_degree();
	g_global_uniform->splat_params_2.y = 0.f;
	g_global_uniform->splat_params_2.z = 0.f;
	g_global_uniform->splat_params_2.w = 0.f;

	m_command_list->SetPipelineState(get_pipeline_state("gs_draw"));

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
