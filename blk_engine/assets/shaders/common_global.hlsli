/// common_global.hlsli
///
/// 2026 blk

#pragma once

/// SceneIndex
///
/// The per-draw index into the bindless scene_constants[] array, bound at
/// (b0, space1) by every pass that reads a per-instance or per-light slot.
struct SceneIndex {
	uint index;
};

/// GlobalConstantData
///
/// Overlays the C++ GlobalUniformData (renderer_dx12.h) via scene_constants[0]
/// and keeps the total size at 512 bytes to match the actual bound CBV
///
/// Notes: Always keep GlobalConstantData in sync with GlobalUniformData.
struct GlobalConstantData {
	row_major matrix view;
	row_major matrix view_projection;
	row_major matrix inv_view_proj;
	float4 camera;
	float4 pad0;
	float4 pad1;
	// .x = g_srv_descriptor_start: the absolute SRV slot (in the shared
	// CBV/SRV/UAV-type heap) where material textures begin. Material shaders
	// add this to their (still table-relative) texture_list id to get the
	// absolute ResourceDescriptorHeap[] index.
	float4 srv_heap_base;
	float4 pad[16];
};
