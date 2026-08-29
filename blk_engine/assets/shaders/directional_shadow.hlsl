/// directional_shadow.hlsl
///
/// 2025 blk

#include "common_light.hlsli"

ConstantBuffer<LightData> scene_constants[] : register(b0);

struct SceneIndex {
	uint index;
};
ConstantBuffer<SceneIndex> scene_index : register(b0, space1);

SamplerState SampleType : register(s0);

/// VertexInput
struct VertexInput {
	float4 position		: POSITION;
	float2 uv			: TEXCOORD0;
};

/// PixelInput
struct PixelInput {
	float4 position		: SV_POSITION;
	float4 clip_position : POSITION;
	float2 uv			: TEXCOORD0;
};

///	vertex_shader
PixelInput vertex_shader(VertexInput input) {
	PixelInput output = (PixelInput)(0);
	output.position = float4(input.position.xyz, 1.0f);
	output.clip_position = output.position;
	output.uv = input.uv;

	return output;
}

/// pixel_shader
float4 pixel_shader(PixelInput input) : SV_TARGET {
	const LightData light_constants = scene_constants[scene_index.index];
	const uint gbuffer_base = (uint)light_constants.gbuffer_srv_base.x;
	const Texture2D<float4> gbuffer_tex_3 = ResourceDescriptorHeap[gbuffer_base + 3]; // SceneDepth
	const Texture2D<float4> gbuffer_tex_5 = ResourceDescriptorHeap[gbuffer_base + 5]; // ShadowDepth

	// Shadow
   float4 world_pos = float4(input.clip_position.xy, gbuffer_tex_3.Sample(SampleType, input.uv).r, 1);
   world_pos = mul(world_pos, light_constants.player_inv_view_proj);
   world_pos /= world_pos.w;

   int index = 0;
   float2 offset = float2(0.0f, 0.0f);

   float cam_dist = length(world_pos.xyz - light_constants.player_camera_pos.xyz);
   if (cam_dist > light_constants.cascade_distances.z) {
      index = 3.0f;
      offset.x += 0.5f;
      offset.y += 0.5f;
   } else if (cam_dist > light_constants.cascade_distances.y) {
      index = 2.0f;
      offset.y += 0.5f;
   } else if (cam_dist > light_constants.cascade_distances.x) {
      index = 1.0f;
      offset.x += 0.5f;
   }

   float4 shadow_tex = mul(world_pos, light_constants.light_matrices[index]);
   shadow_tex /= shadow_tex.w;
   shadow_tex.xy *= 0.5f;
   shadow_tex.xy += offset;

	float4 out_color = float4(1.f, 1.f, 1.f, 1.f);
	const float depth = gbuffer_tex_5.Sample(SampleType, shadow_tex.xy).r;
	if (depth < shadow_tex.z - 0.0001f) {
         out_color = 0.f;
	}
	out_color = out_color * 0.5f + 0.5f;
	return out_color;
}
