/// common_light.hlsli
///
/// 2026 blk

/// LightData
///
/// Overlays the C++ LightInstanceData (renderer_dx12.h) via
/// scene_constants[scene_index.index] in the light/shadow passes.
///
/// Notes: Always keep LightData in sync with LightInstanceData.
struct LightData {
	float4 position;
	float4 direction;
	float4 color;
	row_major matrix light_matrices[4];
	float4 cascade_distances;
	row_major matrix player_inv_view_proj;
	float4 player_camera_pos;
	// .x = absolute SRV slot (in the shared CBV/SRV/UAV-type heap) where this
	// frame's gbuffer SRVs begin -- point_light/directional_light read
	// Color/Normal/Specular/
	// SceneDepth at +0..+3 (directional_light also +4 for Lighting);
	// directional_shadow reads SceneDepth/ShadowDepth at +3/+5. See the
	// bindless SRV conversion in Renderer_Dx12.
	float4 gbuffer_srv_base;
	float4 pad[6];
};
