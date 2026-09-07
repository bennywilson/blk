/// common_light.hlsli
///
/// 2026 blk

// Macro guard rather than `#pragma once` -- see the note in common_global.hlsli.
#ifndef BLK_COMMON_LIGHT_HLSLI
#define BLK_COMMON_LIGHT_HLSLI

#include "common_global.hlsli"

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

/// toon_specular
///
/// Blinn-Phong lobe pushed through a smoothstep, so the highlight reads as a
/// hard-edged patch rather than a smooth falloff -- matching the banded diffuse
/// ramp both light passes already use (smoothstep(0.5, 0.6, n_dot_l)).
///
/// spec_sample is the raw ERenderTarget::Specular texel: rgb reflectance,
/// a gloss. See encode_specular()/gloss_to_spec_power() in common_global.hlsli
/// for the encoding the gbuffer passes write.
///
/// view_dir must point from the shaded pixel toward the camera. The light
/// passes reconstruct it from the depth buffer and player_camera_pos rather
/// than from the material shaders' to_cam interpolator -- to_cam is per-vertex
/// on geometry this pass no longer has, and deferred shading only has the
/// gbuffer to work from.
float3 toon_specular(
	const float3 normal,
	const float3 light_dir,
	const float3 view_dir,
	const float3 light_color,
	const float4 spec_sample) {

	const float3 half_vec = normalize(light_dir + view_dir);
	const float n_dot_h = saturate(dot(normal, half_vec));
	const float lobe = pow(n_dot_h, gloss_to_spec_power(spec_sample.a));

	return smoothstep(0.35f, 0.45f, lobe) * spec_sample.rgb * light_color;
}

#endif // BLK_COMMON_LIGHT_HLSLI
