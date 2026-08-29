/// common_global.hlsli
///
/// 2026 blk 1.0

/// GlobalConstantData
///
/// Overlays the C++ GlobalUniformData (renderer_dx12.h) via scene_constants[0]
/// -- keep field-for-field in sync with that struct, and keep the total size
/// at 512 bytes to match the actual bound CBV (the same slot SceneInstanceData
/// uses everywhere else).
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
