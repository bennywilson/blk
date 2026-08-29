/// directional_light.hlsl
///
/// 2025 blk 1.0

#include "common_light.hlsli"

ConstantBuffer<LightData> scene_constants[] : register(b0);

struct SceneIndex {
	uint index;
};
ConstantBuffer<SceneIndex> scene_index : register(b0, space1);

SamplerState SampleType : register(s0);

/// VertexInput
struct VertexInput {
	float3 position		: POSITION;
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

float3 apply_directional_light(
	const float3 light_dir,
	const float3 light_color,
	const float4 albedo,
	const float3 normal,
	const float3 spec,
	const float depth) {
	const float n_dot_l = smoothstep(0.5, 0.6, saturate(dot(normal, light_dir))) * 0.3 + 0.7f;
	const float3 diffuse = n_dot_l.xxx * albedo.xyz * light_color;

	return diffuse;
}

/// pixel_shader
float4 pixel_shader(PixelInput input) : SV_TARGET {
	const LightData light_constant = scene_constants[scene_index.index];
	const uint gbuffer_base = (uint)light_constant.gbuffer_srv_base.x;
	const Texture2D<float4> g_buffer_0 = ResourceDescriptorHeap[gbuffer_base + 0]; // Color
	const Texture2D<float4> g_buffer_1 = ResourceDescriptorHeap[gbuffer_base + 1]; // Normal
	const Texture2D<float4> g_buffer_2 = ResourceDescriptorHeap[gbuffer_base + 2]; // Specular
	const Texture2D<float4> g_buffer_3 = ResourceDescriptorHeap[gbuffer_base + 3]; // SceneDepth
	const Texture2D<float4> g_buffer_4 = ResourceDescriptorHeap[gbuffer_base + 4]; // Lighting

	const float4 albedo = g_buffer_0.Sample(SampleType, input.uv);
	float3 normal = g_buffer_1.Sample(SampleType, input.uv).xyz * 2.f - 1.f;
	const float3 spec = g_buffer_2.Sample(SampleType, input.uv).xyz;
	const float scene_depth = g_buffer_3.Sample(SampleType, input.uv).r;

	float4 pixel_world_pos = float4(input.clip_position.xy, scene_depth, 1);
	pixel_world_pos = mul(pixel_world_pos, light_constant.player_inv_view_proj);
	pixel_world_pos /= pixel_world_pos.w;
	
	float3 out_color = 0;

	// Directional Light
	{
		const float3 light_dir = normalize(light_constant.direction.xyz);
		const float3 light_color = light_constant.color.xyz;

		/*if (dot(normal, normal) < 0.5) {
			// Skip lighting pixels w/o valid normals
			out_color = albedo.xyz;
		} else {*/
			normal = normalize(normal);
			out_color = apply_directional_light(
				light_dir,
				light_color,
				albedo,
				normal,
				spec,
				scene_depth
			);
			out_color *= g_buffer_4.Sample(SampleType, input.uv).r;
		//}	
	}

	// Ambient
	const float3 ambient = float3(0.2f, 0.2f, 0.2f) * albedo.xyz;
	out_color += ambient;

	return float4(out_color, 1.f);
}