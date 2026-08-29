/// static_model.hlsl
///
/// 2025-2026 blk 1.0

#include "common_global.hlsli"

// Constant buffer can be cast to SceneData and GlobalConstantData -- must be
// the raw homogeneous-array shape (not SceneData itself) for the casts below
// to be valid: HLSL's struct-cast only reliably reinterprets a single
// matrix-array field, not a heterogeneous multi-field struct like SceneData.
struct BaseData {
	row_major matrix pad0[8];
};

/// SceneData
struct SceneData {
	row_major matrix mvp_matrix;
	row_major matrix world_matrix;
	row_major matrix inv_world_matrix;
	float4 color;
	float4 spec;
	float4 time_since_spawn;
	float texture_list[16];
	float4 pad0;
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

/// VertexOutput
struct VertexOutput {
	float4 position		: SV_POSITION;
	float4 clip_pos		: POSITION;
	float2 uv			: TEXCOORD0;
	float3 to_cam		: TEXCOORD1;
	float4 spec			: TEXCOORD2;
	float4 color		: COLOR;
	float4 normal		: NORMAL;
};

///	vertex_shader
VertexOutput vertex_shader(VertexInput input) {
	const BaseData base_constant = scene_constants[0];
	const GlobalConstantData global_constants = (GlobalConstantData)base_constant;
	const BaseData base_instance = scene_constants[scene_index.index];
	const SceneData scene_instance = (SceneData)base_instance;

	VertexOutput output = (VertexOutput)(0);
	output.position = input.position;
	output.position = mul(input.position, scene_instance.mvp_matrix);
	output.clip_pos = output.position;

	float3 world_pos = mul(input.position, scene_instance.world_matrix).xyz;
	output.to_cam = global_constants.camera.xyz - world_pos;
	output.color = scene_instance.color;
	output.spec = scene_instance.spec;
	output.normal.xyz = float3(0.0f, 1.0, 0.0);// temp hack for gbuffer lighting floor mul(input.normal.xyz, (float3x3)scene_instance.world_matrix);
	output.uv = input.uv;

	return output;
}

///	pixelShader
struct PixelOut {
	float4 color		: SV_TARGET0;
	float4 normal		: SV_TARGET1;
	float4 specular		: SV_TARGET2;
	float depth			: SV_TARGET3;
};

///	pixelShader
PixelOut pixel_shader(VertexOutput input) {
	const BaseData base_global = scene_constants[0];
	const GlobalConstantData global_constants = (GlobalConstantData)base_global;
	const BaseData base_scene = scene_constants[scene_index.index];
	const SceneData scene_constant = (SceneData)base_scene;

	const uint tex_0 = (uint)(global_constants.srv_heap_base.x + scene_constant.texture_list[0]);
	const Texture2D<float4> color_tex = ResourceDescriptorHeap[tex_0];
	const float4 albedo = color_tex.Sample(SampleType, input.uv) * input.color;
	const float3 normal = normalize(input.normal.xyz);

	PixelOut o = (PixelOut)0;
	o.color = albedo;
	o.normal = float4(normal.xyz * 0.5f + 0.5f, 1.f);
	o.specular = 1;
	o.depth = input.clip_pos.z / input.clip_pos.w;

	return o;
}

float shadow_depth_ps(VertexOutput input) : SV_TARGET {
	return input.clip_pos.z / input.clip_pos.w;
}

