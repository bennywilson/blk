/// directional_light.kbShader
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

/// LightData
struct LightData {
	float4 position;
	float4 direction;
	float4 color;
	row_major matrix light_matrices[4];
	float4 cascade_distances;
	float4 pad[236];
};

ConstantBuffer<BaseData> scene_constants[] : register(b0);

struct SceneIndex {
	uint index;
};
ConstantBuffer<SceneIndex> scene_index : register(b0, space1);

// [0]:color [1]:normal [2]:spec [3]:depth, [4]:light, [5]:shadow
SamplerState SampleType : register(s0);
Texture2D g_buffer[6] : register(t0);

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
	const float n_dot_l = smoothstep(0.5, 0.6, saturate(dot(normal, light_dir))) * 0.8 + 0.2f;
	const float3 diffuse = n_dot_l.xxx * albedo.xyz * light_color;

	return diffuse;
}

/// pixel_shader
float4 pixel_shader(PixelInput input) : SV_TARGET {
	const BaseData base_global = scene_constants[0];
	const GlobalConstantData global_constants = (GlobalConstantData)base_global;
	const BaseData base_instance = scene_constants[scene_index.index];
	const LightData light_constant = (LightData) base_instance;

	const float4 albedo = g_buffer[0].Sample(SampleType, input.uv);
	const float3 normal = normalize(g_buffer[1].Sample(SampleType, input.uv).xyz * 2.f - 1.f);
	const float3 spec = g_buffer[2].Sample(SampleType, input.uv).xyz;
	const float scene_depth = g_buffer[3].Sample(SampleType, input.uv).r;

	float4 pixel_world_pos = float4(input.clip_position.xy, scene_depth, 1);
	pixel_world_pos = mul( pixel_world_pos, global_constants.inv_view_proj );
	pixel_world_pos /= pixel_world_pos.w;
	
	float3 out_color = 0;

	// Directional Light
	{
		const float3 light_dir = normalize(light_constant.direction.xyz);
		const float3 light_color = light_constant.color.xyz;

		out_color = apply_directional_light(
			light_dir,
			light_color,
			albedo,
			normal,
			spec,
			scene_depth
		);

		out_color *= g_buffer[4].Sample(SampleType, input.uv).r;
		
	}

	// Ambient
	const float3 ambient = float3(0.2f, 0.2f, 0.2f) * albedo.xyz;
	out_color += ambient;

	return float4(out_color, 1.f);
}
