/// gaussian_splat.shader
///
/// 2025 blk 1.0

// Constant buffer can be cast to SceneData and BoneData.
struct BaseData {
	row_major matrix pad0[8];
};

/// GlobalConstantData
struct GlobalConstantData {
	row_major matrix view_projection;
	row_major matrix inv_view_proj;
	float4 camera;
	float4 pad[23];
};

/// SceneData
struct SceneData {
	float4 position;
	float4 scale3d_opacity;
	float4 rotation;
	float4 sh0;
	float4 sh1;
	float4 sh2;
	float4 sh3;
	float4 sh4;
	float4 sh5;
	float4 sh6;
	float4 sh7;
	float4 sh8;
	float4 sh9;
};


ConstantBuffer<BaseData> scene_constants[] : register(b0);

struct SceneIndex {
	uint index;
};
ConstantBuffer<SceneIndex> scene_index : register(b0, space1);

SamplerState SampleType : register(s0);
Texture2D color_tex[] : register(t0);

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

	return output;
}

///	pixel_shader
float4 pixel_shader(PixelInput input) : SV_TARGET {
	const BaseData base_instance = scene_constants[scene_index.index];
	SceneData scene_constant = (SceneData)base_instance;
	return float4(1.f, 1.f, 1.f, 1.f);
}
