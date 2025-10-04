/// gaussian_splat_draw.shader
cbuffer GlobalConstants : register(b0) {
    row_major float4x4 view_matrix;
    row_major float4x4 view_projection;
    row_major float4x4 inv_view_proj;
    float4 camera_pos;
    float4 splat_params;
    float4 pad[18];
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
};

float3 evaluate_sh(float3 n, const SplatPoint splat) {
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

float2 get_vertex_corner(uint cornerID) {
    // Triangle list layout: 0,1,2, 2,3,0
    static const float2 offsets[6] = {
        float2(-1, -1),
        float2( 1, -1),
        float2( 1,  1),
        float2( 1,  1),
        float2(-1,  1),
        float2(-1, -1),
    };
    return offsets[cornerID];
}

VSOutput vertex_shader(VSInput input) {
    const float overall_scale = 100.f;

    const uint sorted_index = input.vertexID / 6;

    const uint splat_id = g_sorted_indices[input.vertexID / 6];
    const SplatPoint splat = g_splats[splat_id];

    // todo: Skip padded entries.
    // They should be at the back and not rendered.  Needs investigation
    if (splat_id >= splat_params.w) {
        VSOutput output = (VSOutput)0;
        output.position = float4(0, 0, 0, 0);
        output.color = 0.0f;
        return output;
    }

    // Splat transform
    const float3 splat_pos = splat.position.xyz * overall_scale;
    const float3x3 splat_axes = quat_to_matrix(splat.rotation);
    const float3 splat_scale = splat.scale3d_opacity.xyz;

    // Indexes for the major, minor, and intermediate axes
    const int long_axis_idx = (splat_scale.x > splat_scale.y) ? ((splat_scale.x  > splat_scale.z) ? 0 : 2) : ((splat_scale.y > splat_scale.z) ? 1 : 2);
    const int short_axis_idx = (splat_scale.x  < splat_scale.y) ? ((splat_scale.x  < splat_scale.z) ? 0 : 2) : ((splat_scale.y < splat_scale.z) ? 1 : 2);
    const int mid_axis_idx = 3 - long_axis_idx - short_axis_idx;

    const float3 long_axis = normalize(splat_axes[long_axis_idx]);

    // Apply scale
    const float long_scale = max(splat_scale.x, max(splat_scale.y, splat_scale.z));
    const float short_scale = min(splat_scale.x, min(splat_scale.y, splat_scale.z));
    const float mid_scale = splat_scale.x + splat_scale.y + splat_scale.z - short_scale - long_scale;

    // Build billboard basis
    const float3 cam_forward = normalize(camera_pos.xyz - splat_pos.xyz);
    const float3 cam_right = normalize(cross(cam_forward, long_axis));

    // Score the alignment of the mid and short axes with the view vector.  Lerping between them provides the billboard width
    const float mid_alignment = abs(dot(cam_forward, splat_axes[mid_axis_idx]));
    const float short_alignment = abs(dot(cam_forward, splat_axes[short_axis_idx]));
    const float t = saturate(short_alignment / (mid_alignment + short_alignment));
    const float billboard_width = lerp(short_scale, mid_scale, t);

    // Create vertex and transform
    const float2 vertex_corner = get_vertex_corner(input.vertexID % 6);
    const float billboard_offset_x = vertex_corner.x * billboard_width;
    const float billboard_offset_y = vertex_corner.y * long_scale;

    const float3 vertex_offset = cam_right * billboard_offset_x + long_axis * billboard_offset_y;
    const float3 world_pos = splat_pos + vertex_offset * splat_params.y * overall_scale;
    const float4 clip_pos = mul(float4(world_pos, 1.0), view_projection);

    VSOutput output;
    output.position = clip_pos;
    output.clip_pos = mul(float4(world_pos, 1), view_matrix);
    output.color.xyz = evaluate_sh(-cam_forward, splat);
    output.color.w = saturate(splat.scale3d_opacity.w) * 0.24f;
    output.uv_and_scale = float4(billboard_offset_x, billboard_offset_y, billboard_width, long_scale);

    return output;
}

float4 pixel_shader(VSOutput input) : SV_Target {
    const float sharpness = splat_params.x;
    const float2 uv = input.uv_and_scale.xy;
    const float2 scale = input.uv_and_scale.zw;
    const float falloff = exp(-sharpness * dot(uv * uv / (scale * scale), float2(1,1)));
    const float output_alpha = saturate(input.color.a * falloff);
    const float3 out_color = (((input.color.rgb * output_alpha) - 0.5) * splat_params.z) + 0.5f;
    return float4(out_color.rgb, output_alpha);
}