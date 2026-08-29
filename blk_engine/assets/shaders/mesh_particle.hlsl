/// mesh_particle.hlsl
///
/// 2025 blk

#include "common_global.hlsli"

// Constant buffer can be cast to SceneData and BoneData.
struct BaseData {
	row_major matrix pad0[8];
};

/// SceneData
struct SceneData {
	row_major matrix mvp_matrix;
	row_major matrix world_matrix;
	float4 color;
	float4 spec;
	float4 time_since_spawn;
	float texture_list[16];
	float4 pad0[17];
};


ConstantBuffer<BaseData> scene_constants[] : register(b0);

struct SceneIndex {
	uint index;
};
ConstantBuffer<SceneIndex> scene_index : register(b0, space1);

SamplerState SampleType : register(s0);

/// VertexInput
struct VertexInput {
	float4 position		: POSITION;
	float2 uv			: TEXCOORD0;
	float4 color		: COLOR;
	float4 normal		: NORMAL;
	float4 tangent		: TANGENT;
};

/// PixelInput
struct PixelInput {
	float4 position		: SV_POSITION;
	float2 uv			: TEXCOORD0;
	float3 to_cam		: TEXCOORD1;
	float4 spec			: TEXCOORD2;
	float4 color		: COLOR;
	float4 normal		: NORMAL;
};

///	vertex_shader
PixelInput vertex_shader(VertexInput input) {
	const BaseData base_global = scene_constants[0];
	const GlobalConstantData global_constants = (GlobalConstantData)base_global;
	const BaseData base_instance = scene_constants[scene_index.index];
	const SceneData scene_instance = (SceneData)base_instance;

	PixelInput output = (PixelInput)(0);
	output.position = input.position;
	output.position = mul(input.position, scene_instance.mvp_matrix);

	float3 world_pos = mul(input.position, scene_instance.world_matrix).xyz;
	output.to_cam = global_constants.camera.xyz - world_pos;
	output.color = scene_instance.color;
	output.spec = scene_instance.spec;
	output.normal.xyz = mul(input.normal.xyz, (float3x3)scene_instance.world_matrix);
	output.uv = input.uv;
	return output;
}

///	pixel_shader
float4 pixel_shader(PixelInput input) : SV_TARGET {
	const BaseData base_global = scene_constants[0];
	const GlobalConstantData global_constants = (GlobalConstantData)base_global;

	const BaseData base_instance = scene_constants[scene_index.index];
	SceneData scene_constant = (SceneData)base_instance;
	const float2 uv_tile = float2(1.0f, 1.0f * 15.0f);
	const float2 uv_start = input.uv - float2(0.0f, 1.0f) + scene_constant.time_since_spawn.x * 2.0f;
	const float2 uv = saturate(uv_start * uv_tile);

	const uint tex_0 = (uint)(global_constants.srv_heap_base.x + scene_constant.texture_list[0]);
	const Texture2D<float4> color_tex = ResourceDescriptorHeap[tex_0];
	const float4 albedo = color_tex.Sample(SampleType, uv) * float4(1.0f, 0.9, 0.4f, 1.f);
	return albedo;
}
