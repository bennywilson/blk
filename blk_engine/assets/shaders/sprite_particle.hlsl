/// sprite_particle.hlsl
///
/// 2025 blk 1.0

#include "common_global.hlsli"

// Constant buffer can be cast to SceneData and BoneData.
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
};

ConstantBuffer<BaseData> scene_constants[] : register(b0);

struct SceneIndex {
	uint index;
};
ConstantBuffer<SceneIndex> scene_index : register(b0, space1);

SamplerState SampleType : register(s0);

struct VertexInput {
	float4 position		: POSITION;
	float2 uv			: TEXCOORD0;
	float4 color		: COLOR;
	float rotation		: NORMAL;
	float scale			: TANGENT;
};

/// PixelInput
struct PixelInput {
	float4 position		: SV_POSITION;
	float2 uv			: TEXCOORD0;
	float3 to_cam		: TEXCOORD1;
	float4 color		: COLOR;
};

///	vertex_shader
PixelInput vertex_shader(VertexInput local_vert) {
	const BaseData base_constant = scene_constants[0];
	const GlobalConstantData global_constants = (GlobalConstantData)base_constant;

	const BaseData base_instance = scene_constants[scene_index.index];
	SceneData scene_constant = (SceneData)base_instance;

	float4 from_center = normalize(float4(local_vert.uv.xy - float2(0.5f, 0.5f), 0.0, 1.0)) * local_vert.scale;
	const float4 world_pos = local_vert.position;//mul(local_vert.position, scene_constant.world_matrix).xyz;
	float3x3 billboard_mat3;
	{
		const float3 up = float3(0.0f, 1.f, 0.0f);
		const float3 zAxis = normalize(global_constants.camera.xyz - world_pos.xyz);
		const float3 xAxis = normalize(cross(up, zAxis));
		const float3 yAxis = normalize(cross(zAxis, xAxis));
		billboard_mat3 = float3x3(xAxis, yAxis, zAxis);

		float rotation = local_vert.rotation;
		const float cosRot = cos(rotation);
		const float sinRot = sin(rotation);
		const float3 xRot = float3(cosRot, sinRot, 0.0f);
		const float3 yRot = float3(-sinRot, cosRot, 0.0f);
		const float3 zRot = float3(0.0f, 0.0f, 1.0f);
		float3x3 rotationMatrix = float3x3(xRot, yRot, zRot);
		from_center.xyz = mul(from_center.xyz,transpose(rotationMatrix));
	}

	PixelInput output = (PixelInput)(0);

	const float3 offset = mul(from_center.xyz, billboard_mat3);
	output.position = mul(world_pos + float4(offset, 0.0f), global_constants.view_projection);

	output.to_cam = global_constants.camera.xyz - world_pos.xyz;
	output.color = local_vert.color;
	output.uv = local_vert.uv;
	return output;
}

///	pixelShader
float4 pixel_shader(PixelInput input) : SV_TARGET {
	const BaseData base_global = scene_constants[0];
	const GlobalConstantData global_constants = (GlobalConstantData)base_global;

	const BaseData base_light = scene_constants[scene_index.index];
	const SceneData scene_constant = (SceneData)base_light;

	const uint tex_0 = (uint)(global_constants.srv_heap_base.x + scene_constant.texture_list[0]);
	const Texture2D<float4> color_tex = ResourceDescriptorHeap[tex_0];
	const float4 albedo = color_tex.Sample(SampleType, input.uv) * input.color;
	return albedo;
}