/// Renderer_Dx12.cpp
///
/// 2025 blk 1.0

// Constant buffer can be cast to SceneData and BoneData.
struct BaseData {
	row_major matrix pad0[64];
};

/// GlobalConstantData
struct GlobalConstantData {
	row_major matrix view_projection;
	row_major matrix inv_view_proj;
	float4 camera;
	float4 pad[247];
};

/// SceneData
struct SceneData {
	row_major matrix mvp_matrix;
	row_major matrix world_matrix;
	float4 color;
	float4 spec;
	float4 time_since_spawn;
	float4 pad0[245];
};

ConstantBuffer<BaseData> scene_constants[] : register(b0);

struct SceneIndex {
	uint index;
};
ConstantBuffer<SceneIndex> scene_index : register(b0, space1);

SamplerState SampleType : register(s0);
Texture2D color_tex : register(t0);

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

	const BaseData base_light = scene_constants[scene_index.index];
	const SceneData scene_instance = (SceneData)base_light;	


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
	const float4 albedo = color_tex.Sample( SampleType, input.uv ) * input.color;
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

