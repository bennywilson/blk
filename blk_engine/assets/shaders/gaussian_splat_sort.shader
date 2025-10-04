/// gaussian_splat_sort.shader

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

StructuredBuffer<SplatPoint> g_splats  : register(t0);  // unsorted splats
RWStructuredBuffer<uint> g_sorted_indices : register(u0);  // output sorted indices
uint j;
uint k;

[numthreads(256, 1, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    uint idx = DTid.x;

    // TODO

    int num_elements = (int)splat_params.w;

    uint ixj = idx ^ j;

    if (ixj > idx) {
        uint a_idx = g_sorted_indices[idx];
        uint b_idx = g_sorted_indices[ixj];

        float3 cam_pos = camera_pos.xyz;
        float3 cam_dir = view_matrix[2].xyz;
        float3 a_pos = g_splats[a_idx].position.xyz;
        float3 b_pos = g_splats[b_idx].position.xyz;

        bool valid_a = a_idx < num_elements;
        bool valid_b = b_idx < num_elements;

        float a_key = valid_a ? length(g_splats[a_idx].position.xyz - cam_pos) : 9999999999999.;
        float b_key = valid_b ? length(g_splats[b_idx].position.xyz - cam_pos) : 9999999999999.;

        bool ascending = ((idx & k) == 0);
        if ((ascending && a_key < b_key) || (!ascending && a_key > b_key)) {
            g_sorted_indices[idx] = b_idx;
            g_sorted_indices[ixj] = a_idx;
        }
    }
}