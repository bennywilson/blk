/// skinned_model.hlsl
///
/// 2025 blk

#include "common_scene.hlsli"

/// BoneData
///
/// Bound separately at (b0, space2) -- not a cast off BaseData.
struct BoneData {
	 row_major matrix bones[128];
};

ConstantBuffer<BaseData> scene_constants[] : register(b0);
ConstantBuffer<SceneIndex> scene_index : register(b0, space1);

ConstantBuffer<BoneData> scene_bone_arrays[] : register(b0, space2);
ConstantBuffer<SceneIndex> bone_index : register(b0, space3);

SamplerState SampleType : register(s0);

/// VertexIn
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

	int4 blend_indices = input.blend_indices * 255;
	float4 blend_weights = (float4)input.blend_weights;

	BoneData bone_data = scene_bone_arrays[bone_index.index];
	matrix bone_mat = bone_data.bones[blend_indices.x] * blend_weights.x +
		bone_data.bones[blend_indices.y] * blend_weights.y +
		bone_data.bones[blend_indices.z] * blend_weights.z +
		bone_data.bones[blend_indices.w] * blend_weights.w;

	const float4 local_pos = mul(input.position, bone_mat);
	const float3 world_pos = mul(scene_constant.world_matrix, input.position).xyz;
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

/// PixelOut
struct PixelOut {
	float4 color		: SV_TARGET0;
	float4 normal		: SV_TARGET1;
	float4 specular		: SV_TARGET2;
	float depth			: SV_TARGET3;
	float entity_id		: SV_TARGET4;
};

PixelOut pixel_shader(VertexOut input) {
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
	o.entity_id = scene_constant.entity_id.x;
	return o;
}

//
float shadow_depth_ps(VertexOut input) : SV_TARGET {
	return input.clip_pos.z / input.clip_pos.w;
}
