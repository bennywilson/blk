/// Renderer_Dx12.cpp
///
/// 2025 blk 1.0

struct BaseData {
	row_major matrix pad0[64];
};

/// GlobalConstantData
struct GlobalConstantData {
	row_major matrix view_projection;
	row_major matrix inv_view_proj;
	float4 camera;
	row_major matrix light_matrices[4];
	float4 pad[231];
};

/// LightData
struct LightData {
	float4 position;
	float4 direction;
	float4 color;
	row_major matrix light_matrices[4];
	float4 cascade_distances;
	float4 pad[236];
};


ConstantBuffer<BaseData> scene_constants[2048] : register(b0);

struct SceneIndex {
	uint index;
};
ConstantBuffer<SceneIndex> scene_index : register(b0, space1);

SamplerState SampleType : register(s0);
Texture2D color_tex[4] : register(t0);

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

float3 apply_point_light(
	const float3 light_pos,
	const float3 light_color,
	const float light_radius,
	const float3 pixel_pos,
	const float4 albedo,
	const float3 normal,
	const float3 spec,
	const float depth) {

	float3 vec_to_light = light_pos - pixel_pos.xyz;
	float dist_to_light = length(vec_to_light);
	const float3 light_dir = normalize(vec_to_light);

	const float atten = 1.0f - saturate(dist_to_light / light_radius);
	const float n_dot_l = saturate(dot(normal, light_dir));

	return n_dot_l.xxx * atten * light_color * albedo.xyz;
}

/// pixel_shader
float4 pixel_shader(PixelInput input) : SV_TARGET {
	const BaseData base_global = scene_constants[0];
	const GlobalConstantData global_constants = (GlobalConstantData)base_global;
	const BaseData base_light = scene_constants[scene_index.index];
	const LightData light_constant = (LightData) base_light;
	const float4 albedo =  color_tex[0].Sample(SampleType, input.uv);
	const float3 normal = normalize(color_tex[1].Sample(SampleType, input.uv).xyz * 2.f - 1.f);
	const float3 spec = color_tex[2].Sample(SampleType, input.uv).xyz;
	const float depth = color_tex[3].Sample(SampleType, input.uv).r;

	float4 pixel_world_pos = float4(input.clip_position.xy, depth, 1);
	pixel_world_pos = mul( pixel_world_pos, global_constants.inv_view_proj );
	pixel_world_pos /= pixel_world_pos.w;	float3 out_color = 0;

	const float3 light_pos = light_constant.position.xyz;
	const float light_radius = light_constant.position.w;
	const float3 light_color = light_constant.color.xyz;
	
	out_color += apply_point_light(
		light_pos,
		light_color,
		light_radius,
		pixel_world_pos.xyz,
		albedo,
		normal,
		spec,
		depth
	);

	return float4(out_color, 1.f);
}
