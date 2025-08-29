// Global camera/view/projection data
cbuffer GlobalConstants : register(b0)
{
    float4x4 view_projection;
    float4x4 inv_view_proj;
    float4 camera;
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

struct VSInput
{
    uint vertexID : SV_VertexID; // Used to index into splat buffer
};

struct VSOutput
{
    float4 position : SV_Position;
    float4 color    : COLOR;
};

VSOutput vertex_shader(VSInput input)
{
   SplatPoint splat = g_splats[input.vertexID];

    // Expand splat in view space (simplified)
	float4 world_pos = float4(splat.position.xyz * 1000.f, 1.0);
    float4 clip_pos = mul(view_projection, world_pos);
    clip_pos /= clip_pos.w;

    VSOutput output;
    output.position = clip_pos;
    output.color = splat.sh0;
    return output;
}

float4 pixel_shader(VSOutput input) : SV_Target
{
    return input.color;
}