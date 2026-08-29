/// destructible.hlsl
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
	float4 color;
	float4 spec;
	float4 time_since_spawn;
	float4 pad0[17];
};

/// BoneData
struct BoneData {
	 row_major matrix bones[64];
};

ConstantBuffer<BaseData> scene_constants[2048] : register(b0);

struct SceneIndex {
	uint index;
};
ConstantBuffer<SceneIndex> scene_index : register(b0, space1);

SamplerState SampleType : register(s0);

struct VertexIn {
	float4 position			: POSITION;
	float2 uv				: TEXCOORD0;
	float4 normal			: NORMAL;
	float4 blend_indices	: COLOR;
	float4 blend_weights	: TANGENT;
};

/// VertexOut
struct VertexOut {
	float4 position			: SV_POSITION;
	float4 clip_pos			: POSITION;
	float2 uv				: TEXCOORD0;
	float3 to_cam			: TEXCOORD1;
	float4 spec				: TEXCOORD2;
	float4 color			: COLOR;
	float4 normal			: NORMAL;
};

///	vertex_shader
VertexOut vertex_shader(VertexIn input) {
	const BaseData base_global = scene_constants[0];
	const GlobalConstantData global_constants = (GlobalConstantData)base_global;

	const BaseData base_scene = scene_constants[scene_index.index];
	const SceneData scene_constant = (SceneData)base_scene;	
	
	int4 IndexVector = input.blend_indices * 255;
	const BaseData base_bone_1 = scene_constants[scene_index.index];
	const BaseData base_bone_2 = scene_constants[scene_index.index];
	const BoneData bone_data_1 = (const BoneData)base_bone_1;
	const BoneData bone_data_2 = (const BoneData)base_bone_2;
	matrix bone_mat;
	if (IndexVector[0] <= 63) {
		bone_mat = bone_data_1.bones[IndexVector[0]];
	} else {
		bone_mat = bone_data_2.bones[IndexVector[0] - 64];
	}

	const float4 local_pos = mul(input.position, bone_mat);
	const float3 world_pos = mul(input.position, scene_constant.world_matrix).xyz;
	const float3 normal = mul(input.normal.xyz * 2.0f - 1.0f, (float3x3)bone_mat);

	VertexOut output = (VertexOut)(0);
	output.position = mul(local_pos, scene_constant.mvp_matrix);
	output.clip_pos = output.position;
	output.to_cam = global_constants.camera.xyz - world_pos;
	output.color = scene_constant.color;
	output.spec = scene_constant.spec;
	output.normal.xyz = mul(normal.xyz, (float3x3)scene_constant.world_matrix);
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
PixelOut pixel_shader(VertexOut input) {
	const BaseData base_global = scene_constants[0];
	const GlobalConstantData global_constants = (GlobalConstantData)base_global;

	const uint tex_0 = (uint)global_constants.srv_heap_base.x;
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

// shadow_depth_ps
float shadow_depth_ps(VertexOut input) : SV_TARGET {
	return input.clip_pos.z / input.clip_pos.w;
}

