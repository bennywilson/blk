// Global camera/view/projection data
cbuffer GlobalConstants : register(b0)
{
    float4x4 view_matrix;
    float4x4 view_projection;
    float4x4 inv_view_proj;
    float4 camera_pos;
};

// Per-point data
struct SplatPoint
{
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
    float4 pad[19];
};

StructuredBuffer<SplatPoint> g_splats : register(t0);

StructuredBuffer<uint> g_sorted_indices : register(t1);

struct VSInput
{
    uint vertexID : SV_VertexID; // Used to index into splat buffer
};

struct VSOutput
{
    float4 position : SV_Position;
    float4 clip_pos : TEXCOORD0;
    float4 color    : COLOR;
};

float3 EvaluateSH(float3 n, SplatPoint sp)
{
    // Precompute SH basis functions for direction n
    float shBasis[9];
    shBasis[0] = 0.282095f;                          // L00
    shBasis[1] = 0.488603f * n.y;                    // L1-1
    shBasis[2] = 0.488603f * n.z;                    // L10
    shBasis[3] = 0.488603f * n.x;                    // L11
    shBasis[4] = 1.092548f * n.x * n.y;              // L2-2
    shBasis[5] = 1.092548f * n.y * n.z;              // L2-1
    shBasis[6] = 0.315392f * (3.0f * n.z * n.z - 1); // L20
    shBasis[7] = 1.092548f * n.x * n.z;              // L21
    shBasis[8] = 0.546274f * (n.x * n.x - n.y * n.y);// L22

    // Accumulate SH lighting
    float3 result = float3(0, 0, 0);
    result += shBasis[0] * sp.sh0.rgb;
  /*  result += shBasis[1] * sp.sh1.rgb;
    result += shBasis[2] * sp.sh2.rgb;
    result += shBasis[3] * sp.sh3.rgb;
    result += shBasis[4] * sp.sh4.rgb;
    result += shBasis[5] * sp.sh5.rgb;
    result += shBasis[6] * sp.sh6.rgb;
    result += shBasis[7] * sp.sh7.rgb;
    result += shBasis[8] * sp.sh8.rgb;*/

    return result;
}

float3 GetCornerOffset(uint cornerID) {
    // Triangle list layout: 0,1,2, 2,3,0
    static const float3 offsets[6] = {
        float3(-1, -1, 0),
        float3( 1, -1, 0),
        float3( 1,  1, 0),
        float3( 1,  1, 0),
        float3(-1,  1, 0),
        float3(-1, -1, 0),
    };
    return offsets[cornerID];
}

VSOutput vertex_shader(VSInput input) {
    const uint quad_id = g_sorted_indices[input.vertexID/6];
    const uint corner_id = input.vertexID % 6;

    SplatPoint splat = g_splats[quad_id];

    const float overall_scale = 100.f;
    // Expand splat in view space (simplified)
    float3 corners = GetCornerOffset(corner_id);
    float3 local_pos = 
       view_matrix[0].xyz * corners.x + 
       view_matrix[1].xyz * corners.y +
       view_matrix[2].xyz * corners.z;

    float3 splat_scale = float3(splat.scale3d_opacity.x, splat.scale3d_opacity.y, splat.scale3d_opacity.z);
	float4 world_pos = float4(local_pos.xyz * overall_scale * max(splat_scale.x, max(splat_scale.y, splat_scale.z)) + splat.position.xyz * overall_scale, 1.0);
    float4 clip_pos = mul(view_projection, world_pos);
    clip_pos /= clip_pos.w;

    float3 view_dir = normalize(camera_pos.xyz - world_pos.xyz);
  //  view_dir.z *= -1.0f;

    VSOutput output;
    output.position = clip_pos;
    output.clip_pos = clip_pos;
    output.color.xyz = EvaluateSH(view_dir, splat);
    output.color.w = saturate(splat.scale3d_opacity.w);
    return output;
}

float4 pixel_shader(VSOutput input) : SV_Target {
    return float4(input.color.xyz * input.color.a, input.color.a);
}