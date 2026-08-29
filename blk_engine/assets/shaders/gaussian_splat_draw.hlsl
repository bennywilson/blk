/// gaussian_splat_draw.hlsl
cbuffer GlobalConstants : register(b0) {
    row_major float4x4 view_matrix;
    row_major float4x4 view_projection;
    row_major float4x4 inv_view_proj;
    float4 camera_pos;
    float4 splat_params;
    float4 splat_params_2;		// x - draw mode, y - sh level
    float4 pad[17];
};

// This shader compiles at SM6.0 where half is just an alias for float.
// So half f_rest[24] is actually 96 bytes, not 48.
//
// To get 16-bit halfs in SM6.2+ you need the compiler flag -enable-16bit-types
// and hardware/driver support (Native16BitShaderOpsSupported)
//
// FIXME: the CPU side (gaussian_splat_dx12.cpp) still packs sh_rest as 24
// tightly-packed 2-byte halfs (48 bytes, not 96), so every f_rest[k] read
// here actually lands on CPU bytes [64+4k, 64+4k+4) -- two adjacent packed
// half values' raw bits reinterpreted as one garbage float, for k<12, and
// pure padding bytes for k>=12. Degree-1/2 SH evaluation (evaluate_sh below)
// is fed garbage whenever max_sh_degree() >= 1, currently masked by the
// correctly-read sh0 base color dominating the visual result.
struct SplatPoint {
	float4 position;			// 16
	float4 scale3d_opacity;		// 32
	float4 rotation;			// 48
	float4 sh0;					// 64
	half f_rest[24];			// 160
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

float3x3 quat_to_matrix(float4 q) {
    // Row i is local basis axis i rotated into world space -- matches
    // Quat4::to_mat4() in math/quaternion.cpp (row-vector convention,
    // v' = v*M).
    q = normalize(q);
    return float3x3(
        float3(1 - 2 * (q.y * q.y + q.z * q.z),     2 * (q.x * q.y - q.w * q.z),     2 * (q.x * q.z + q.w * q.y)),
        float3(    2 * (q.x * q.y + q.w * q.z), 1 - 2 * (q.x * q.x + q.z * q.z),     2 * (q.y * q.z - q.w * q.x)),
        float3(    2 * (q.x * q.z - q.w * q.y),     2 * (q.y * q.z + q.w * q.x), 1 - 2 * (q.x * q.x + q.y * q.y))
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

float3 evaluate_sh(float3 n, const SplatPoint splat) {
    // Note: n should be the normalized view direction
    float x = n.x;
    float y = n.y;
    float z = n.z;

    float shBasis[16];

    // Degree 0
    shBasis[0] = 0.282095f;

    // Degree 1
    shBasis[1] = 0.488603f * y;
    shBasis[2] = 0.488603f * z;
    shBasis[3] = 0.488603f * x;

    // Degree 2
    shBasis[4] = 1.092548f * x * y;
    shBasis[5] = 1.092548f * y * z;
    shBasis[6] = 0.315392f * (3.0f * z * z - 1.0f);
    shBasis[7] = 1.092548f * x * z;
    shBasis[8] = 0.546274f * (x * x - y * y);

    // Accumulate lighting (Base color)
    float3 result = shBasis[0] * splat.sh0.rgb;

    // Degree 1
    if (splat_params_2.x >= 1)
    {
        // f_rest mapping: [0,1,2] -> sh1 | [3,4,5] -> sh2 | [6,7,8] -> sh3
        result += shBasis[1] * float3(splat.f_rest[0], splat.f_rest[1], splat.f_rest[2]);
        result += shBasis[2] * float3(splat.f_rest[3], splat.f_rest[4], splat.f_rest[5]);
        result += shBasis[3] * float3(splat.f_rest[6], splat.f_rest[7], splat.f_rest[8]);
    }

    // Degree 2
    if (splat_params_2.x >= 2)
    {
        // f_rest mapping continues sequentially
        result += shBasis[4] * float3(splat.f_rest[9],  splat.f_rest[10], splat.f_rest[11]);
        result += shBasis[5] * float3(splat.f_rest[12], splat.f_rest[13], splat.f_rest[14]);
        result += shBasis[6] * float3(splat.f_rest[15], splat.f_rest[16], splat.f_rest[17]);
        result += shBasis[7] * float3(splat.f_rest[18], splat.f_rest[19], splat.f_rest[20]);
        result += shBasis[8] * float3(splat.f_rest[21], splat.f_rest[22], splat.f_rest[23]);
    }

    return saturate(result + 0.5f);
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

    const float4 fill = float4(input.color.rgb, 1.f);//float4(0.401f, 0.050f, 0.855f, 1.f);
    const float4 outline = float4(1, 0.866, 0.059, 1);
    //float4(0.376, 0.715, 1.000, 1); // light blue

    if (splat_params_2.w > 0) {
        if (falloff > 0.01 && falloff < 0.02) {
            return outline;
        } else if (falloff <= 0.01f) {
            return 0;
        } else {
            return fill;
        }
        return 0;
    }
    return float4(out_color.rgb, output_alpha);
}
