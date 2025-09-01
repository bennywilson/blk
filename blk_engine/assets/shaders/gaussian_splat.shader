/// gaussian_splat.shader

cbuffer GlobalConstants : register(b0) {
    row_major float4x4 view_matrix;
    row_major float4x4 view_projection;
    row_major float4x4 inv_view_proj;
    float4 camera_pos;
    float4 splat_falloff_scale_near_far;
    float4 splat_contrast;
    float4 pad[17];
};

struct SplatPoint {
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
    float4 pad[20];
};

StructuredBuffer<SplatPoint> g_splats : register(t0);
StructuredBuffer<uint> g_sorted_indices : register(t1);

struct VSInput {
    uint vertexID : SV_VertexID;
};

struct VSOutput {
    float4 position         : SV_Position;
    float4 clip_pos         : TEXCOORD0;
    float4 color            : COLOR;
    float4 uv_and_scale     : TEXCOORD1;
    float projected_radius  : TEXCOORD2;
};

float3 EvaluateSH(float3 n, const SplatPoint splat) {
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
    result += shBasis[0] * splat.sh0.rgb;

    // todo
    return saturate(max(result + 0.5f, 0.f));
}

float3x3 quat_to_matrix(float4 q) {
    // Adapted from "Real-Time Rendering", 3rd Edition (2018), Chapter 4.3
    // Akenine-Moller et al.
    q = normalize(q);
    return float3x3(
        float3(1 - 2 * (q.y * q.y + q.z * q.z),     2 * (q.x * q.y + q.w * q.z),     2 * (q.x * q.z - q.w * q.y)),
        float3(    2 * (q.x * q.y - q.w * q.z), 1 - 2 * (q.x * q.x + q.z * q.z),     2 * (q.y * q.z + q.w * q.x)),
        float3(    2 * (q.x * q.z + q.w * q.y),     2 * (q.y * q.z - q.w * q.x), 1 - 2 * (q.x * q.x + q.y * q.y))
    );
}

float3 get_corner(uint cornerID) {
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
    const float overall_scale = 100.f;

    const uint quad_id = g_sorted_indices[input.vertexID / 6];
    const uint corner_id = input.vertexID % 6;

    SplatPoint splat = g_splats[quad_id];
    float3 splat_pos = splat.position.xyz * 100;

    float3x3 pca_basis = quat_to_matrix(splat.rotation);
    float3 scale = splat.scale3d_opacity.xyz;

    // Find dominant axis
    int max_axis = (scale.x > scale.y) ? ((scale.x > scale.z) ? 0 : 2) : ((scale.y > scale.z) ? 1 : 2);
    float3 dominant = normalize(pca_basis[max_axis]);

    // Build billboard basis
    float3 view_dir = normalize(camera_pos.xyz - splat_pos.xyz);
    float3 right = normalize(cross(view_dir, dominant));
    float3 up = normalize(cross(dominant, right));

    float3 view_space_radius = mul(scale, (float3x3)view_matrix).xyz;
    float projected_radius = length(view_space_radius);

    // Corner offset
    float2 corner = get_corner(corner_id).xy;

    // Apply scale
    float3 s = scale;
    float short_scale = min(s.x, min(s.y, s.z));
    float long_scale = max(s.x, max(s.y, s.z));
    float mid_scale = s.x + s.y + s.z - short_scale - long_scale;

    float3 offset = right * (corner.x * mid_scale) +
                    dominant * (corner.y * long_scale);

    float3 world_pos = splat_pos + offset * splat_falloff_scale_near_far.y * overall_scale;
    float4 clip_pos = mul(float4(world_pos, 1.0), view_projection);
    clip_pos /= clip_pos.w;

    VSOutput output;
    output.position = clip_pos;
    output.clip_pos = mul(float4(world_pos, 1), view_matrix);
    output.color.xyz = EvaluateSH(-view_dir, splat);
    output.color.w = saturate(splat.scale3d_opacity.w);
    output.projected_radius = projected_radius;

    output.uv_and_scale = float4(corner.x * short_scale, corner.y * long_scale, short_scale, long_scale);

    return output;
}

float4 pixel_shader(VSOutput input) : SV_Target {
    const float sharpness = splat_falloff_scale_near_far.x;
    const float near_clip = splat_falloff_scale_near_far.z;
    const float far_clip = splat_falloff_scale_near_far.w;

    // todo
    if (input.clip_pos.z < near_clip || input.clip_pos.z > far_clip){
        clip(-1);
    }

    // todo
    if (input.projected_radius.x > 0.5) {
        clip(-1);
    }

    const float2 uv = input.uv_and_scale.xy * 1.0;
    const float2 scale = input.uv_and_scale.zw;
    const float falloff = exp(-sharpness * dot(uv * uv / (scale * scale), float2(1,1)));
    const float output_alpha = saturate(input.color.a * falloff);
    const float3 out_color = (((input.color.rgb * output_alpha) - 0.5) * splat_contrast.x) + 0.5f;

    return float4(out_color.rgb, output_alpha);
}