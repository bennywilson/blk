/// gaussian_splat_radix.hlsl
///
/// 2026 blk
///
/// GPU least-significant-digit radix sort of the splat index buffer, keyed by
/// view-space depth so gaussian_splat_draw.hlsl composites back-to-front.
/// Reduce-Scan-Scan-Scatter radix: 32-bit keys, four 8-bit digit passes, each
/// O(n). Ported from black_splat's gaussian_splat_radix.wgsl (same repo family,
/// hand-authored WGSL there); this is the HLSL source of truth, compiled to
/// WGSL for the web the same way every other shader here is.
///
/// One-Sweep / decoupled-look-back radix needs cross-workgroup forward-progress
/// guarantees WebGPU does not provide, and would risk deadlock on the web
/// backend. Every phase here is its own dispatch instead (its own pass
/// boundary, so D3D12 needs a UAV barrier between phases and WebGPU needs
/// nothing extra - a new compute pass already synchronizes), and every scan
/// runs in group-shared memory with no wave/subgroup intrinsics, which are
/// also not guaranteed on the web.
///
/// Phases per sort (the host drives the dispatch order - see
/// Renderer_Dx12::sort_splats_gpu / Renderer_WebGpu::sort_splats):
///   cs_compute_keys  - once: depth -> sortable u32 key, payload = splat index.
///   per digit (shift 0/8/16/24):
///     cs_histogram   - per-tile bucket counts, written bucket-major into g_hist.
///     cs_scan_reduce - exclusive-scan each 256-wide block of g_hist in place,
///                      emit each block's total to g_block_sums.
///     cs_scan_spine  - one workgroup: exclusive-scan g_block_sums (any length).
///     cs_scan_add    - add each block's scanned base back into g_hist. g_hist
///                      now holds, at [bucket*num_tiles + tile], the global
///                      output base of that tile's run of `bucket` elements.
///     cs_scatter     - per tile: stable local sort (8x 1-bit split) then
///                      scatter to global positions; ping-pongs keys/vals
///                      between the *_a / *_b buffers.

static const uint WG = 256;
static const uint RADIX = 256;
static const uint RADIX_MASK = 255;

cbuffer SortGlobals : register(b0, space0) {
	float4 zc;          // third row of (view * model): depth = dot(zc, float4(pos, 1))
	uint num_elements;  // real splat count (no padding)
	uint num_tiles;     // ceil(num_elements / WG)
	uint pad0;
	uint pad1;
};

// Matches gaussian_splat_draw.hlsl's SplatPoint - same buffer, read-only here.
// Only .position is read; the rest exists purely so the stride agrees with
// what that shader (and the CPU upload) already established.
struct SplatPoint {
	float4 position;
	float4 scale3d_opacity;
	float4 rotation;
	float4 sh0;
	half f_rest[24];
};

StructuredBuffer<SplatPoint> g_splats : register(t0, space0);
StructuredBuffer<uint> g_keys_in : register(t1, space0);
RWStructuredBuffer<uint> g_keys_out : register(u0, space0);
StructuredBuffer<uint> g_vals_in : register(t2, space0);
RWStructuredBuffer<uint> g_vals_out : register(u1, space0);
RWStructuredBuffer<uint> g_hist : register(u2, space0);
RWStructuredBuffer<uint> g_block_sums : register(u3, space0);

// Per-pass uniform, supplied via dynamic offset: the digit's bit shift (0/8/16/24).
cbuffer PassInfo : register(b0, space1) {
	uint pass_shift;
	uint pass_pad0;
	uint pass_pad1;
	uint pass_pad2;
};

// Maps an IEEE-754 float to a u32 whose unsigned order matches the float's
// order: flip the sign bit for positives, flip all bits for negatives. An
// ascending u32 sort then orders the floats ascending.
uint float_key(float f) {
	uint u = asuint(f);
	uint mask = ((u & 0x80000000u) != 0u) ? 0xFFFFFFFFu : 0x80000000u;
	return u ^ mask;
}

// Shared scratch used by several entry points.
groupshared uint s_scan[256];   // scan working buffer
groupshared uint s_keys[256];
groupshared uint s_vals[256];
groupshared uint s_count[256];  // per-digit tile counts (histogram)

// Hillis-Steele exclusive scan of s_scan[0..WG). On return s_scan holds the
// exclusive prefix sums and the function returns the total sum.
uint excl_scan_wg(uint li) {
	uint mine = s_scan[li];
	GroupMemoryBarrierWithGroupSync();
	uint offset = 1u;
	[loop]
	while (offset < WG) {
		uint add = 0u;
		if (li >= offset) {
			add = s_scan[li - offset];
		}
		GroupMemoryBarrierWithGroupSync();
		if (li >= offset) {
			s_scan[li] = s_scan[li] + add;
		}
		GroupMemoryBarrierWithGroupSync();
		offset = offset << 1u;
	}
	uint total = s_scan[WG - 1u];
	GroupMemoryBarrierWithGroupSync();
	uint inclusive = s_scan[li];
	GroupMemoryBarrierWithGroupSync();
	s_scan[li] = inclusive - mine;  // each lane touches only its own slot
	GroupMemoryBarrierWithGroupSync();
	return total;
}

// Hillis-Steele inclusive max-scan of s_scan[0..WG), in place.
void incl_max_scan_wg(uint li) {
	GroupMemoryBarrierWithGroupSync();
	uint offset = 1u;
	[loop]
	while (offset < WG) {
		uint v = 0u;
		if (li >= offset) {
			v = s_scan[li - offset];
		}
		GroupMemoryBarrierWithGroupSync();
		if (li >= offset) {
			s_scan[li] = max(s_scan[li], v);
		}
		GroupMemoryBarrierWithGroupSync();
		offset = offset << 1u;
	}
}

// Stable local sort of s_keys/s_vals[0..WG) by the 8-bit digit at `shift`, as
// 8 sequential 1-bit stable splits. O(WG log WG); no atomics.
void local_sort(uint li, uint shift) {
	uint bit = 0u;
	[loop]
	while (bit < 8u) {
		uint b = shift + bit;
		uint my_key = s_keys[li];
		uint my_val = s_vals[li];
		uint is_one = (my_key >> b) & 1u;

		s_scan[li] = 1u - is_one;  // predicate: this lane is a zero
		uint total_zeros = excl_scan_wg(li);
		uint zeros_before = s_scan[li];  // exclusive prefix of zeros
		uint dest = zeros_before;        // zeros keep their relative order
		if (is_one == 1u) {
			dest = total_zeros + (li - zeros_before);  // ones follow, stably
		}
		GroupMemoryBarrierWithGroupSync();
		s_keys[dest] = my_key;
		s_vals[dest] = my_val;
		GroupMemoryBarrierWithGroupSync();
		bit = bit + 1u;
	}
}

// For the sorted tile, returns the run-start of position `li`: the index
// where li's digit run begins (so li - run_start is li's rank within its
// run). Must be called by every lane (it contains group barriers).
uint run_start_of(uint li, uint active_count, uint shift) {
	uint boundary = 0u;
	if (li < active_count) {
		uint d = (s_keys[li] >> shift) & RADIX_MASK;
		uint prev = 0xFFFFFFFFu;  // sentinel: lane 0 is always a boundary
		if (li > 0u) {
			prev = (s_keys[li - 1u] >> shift) & RADIX_MASK;
		}
		if (li == 0u || d != prev) {
			boundary = li;
		}
	}
	s_scan[li] = boundary;
	incl_max_scan_wg(li);
	return s_scan[li];
}

// -----------------------------------------------------------------------------
// Phase 0: compute keys + payloads (run once per sort, before the digit passes).
// -----------------------------------------------------------------------------
[numthreads(256, 1, 1)]
void cs_compute_keys(uint3 dtid : SV_DispatchThreadID) {
	uint i = dtid.x;
	if (i >= num_elements) {
		return;
	}
	float3 p = g_splats[i].position.xyz;
	float depth = zc.x * p.x + zc.y * p.y + zc.z * p.z + zc.w;
	// Ascending by depth, so index 0 ends up the farthest splat: the draw
	// (alpha blending in index order) then composites back-to-front.
	g_keys_out[i] = float_key(depth);
	g_vals_out[i] = i;
}

// -----------------------------------------------------------------------------
// Phase 1: per-tile histogram (bucket-major output). Sorts the tile by digit,
// then reads off each digit's run length - O(WG log WG), atomic-free.
// -----------------------------------------------------------------------------
[numthreads(256, 1, 1)]
void cs_histogram(uint3 gtid : SV_GroupThreadID, uint3 gid : SV_GroupID) {
	uint li = gtid.x;
	uint tile = gid.x;
	uint tile_start = tile * WG;
	uint active_count = WG;
	if (tile_start + WG > num_elements) {
		active_count = num_elements - tile_start;
	}
	uint shift = pass_shift;

	if (li < active_count) {
		s_keys[li] = g_keys_in[tile_start + li];
	} else {
		s_keys[li] = 0xFFFFFFFFu;  // sorts to the tail, never counted
	}
	s_vals[li] = 0u;  // unused here; keeps local_sort generic
	GroupMemoryBarrierWithGroupSync();

	local_sort(li, shift);
	uint run_start = run_start_of(li, active_count, shift);

	// Each digit's count is written by the (single) lane at the end of its run.
	s_count[li] = 0u;
	GroupMemoryBarrierWithGroupSync();
	if (li < active_count) {
		uint d = (s_keys[li] >> shift) & RADIX_MASK;
		bool is_run_end = (li == active_count - 1u);
		if (!is_run_end) {
			is_run_end = d != ((s_keys[li + 1u] >> shift) & RADIX_MASK);
		}
		if (is_run_end) {
			s_count[d] = li - run_start + 1u;
		}
	}
	GroupMemoryBarrierWithGroupSync();
	// Bucket-major: hist[bucket * num_tiles + tile].
	g_hist[li * num_tiles + tile] = s_count[li];
}

// -----------------------------------------------------------------------------
// Phase 2a: exclusive-scan each 256-wide block of g_hist in place; emit block
// total. g_hist length is RADIX * num_tiles (a multiple of 256), so there are
// exactly num_tiles full blocks.
// -----------------------------------------------------------------------------
[numthreads(256, 1, 1)]
void cs_scan_reduce(uint3 gtid : SV_GroupThreadID, uint3 gid : SV_GroupID) {
	uint li = gtid.x;
	uint block = gid.x;
	uint idx = block * WG + li;
	s_scan[li] = g_hist[idx];
	uint total = excl_scan_wg(li);
	g_hist[idx] = s_scan[li];
	if (li == 0u) {
		g_block_sums[block] = total;
	}
}

// -----------------------------------------------------------------------------
// Phase 2b: exclusive-scan g_block_sums in a single workgroup (any length).
// -----------------------------------------------------------------------------
[numthreads(256, 1, 1)]
void cs_scan_spine(uint3 gtid : SV_GroupThreadID) {
	uint li = gtid.x;
	uint n = num_tiles;
	uint carry = 0u;
	uint base = 0u;
	[loop]
	while (base < n) {
		uint idx = base + li;
		uint v = 0u;
		if (idx < n) {
			v = g_block_sums[idx];
		}
		s_scan[li] = v;
		uint total = excl_scan_wg(li);
		if (idx < n) {
			g_block_sums[idx] = s_scan[li] + carry;
		}
		carry = carry + total;
		GroupMemoryBarrierWithGroupSync();
		base = base + WG;
	}
}

// -----------------------------------------------------------------------------
// Phase 2c: add each block's scanned base back into g_hist.
// -----------------------------------------------------------------------------
[numthreads(256, 1, 1)]
void cs_scan_add(uint3 gtid : SV_GroupThreadID, uint3 gid : SV_GroupID) {
	uint li = gtid.x;
	uint block = gid.x;
	uint idx = block * WG + li;
	g_hist[idx] = g_hist[idx] + g_block_sums[block];
}

// -----------------------------------------------------------------------------
// Phase 3: stable local sort + scatter to global positions.
// -----------------------------------------------------------------------------
[numthreads(256, 1, 1)]
void cs_scatter(uint3 dtid : SV_DispatchThreadID, uint3 gtid : SV_GroupThreadID, uint3 gid : SV_GroupID) {
	uint li = gtid.x;
	uint tile = gid.x;
	uint i = dtid.x;

	uint tile_start = tile * WG;
	uint active_count = WG;
	if (tile_start + WG > num_elements) {
		active_count = num_elements - tile_start;
	}

	// Load this tile. Inactive lanes (past the end) get a max key so they sort
	// to the tail; they are never scattered.
	uint key = 0xFFFFFFFFu;
	uint val = 0u;
	if (li < active_count) {
		key = g_keys_in[i];
		val = g_vals_in[i];
	}
	s_keys[li] = key;
	s_vals[li] = val;
	GroupMemoryBarrierWithGroupSync();

	uint shift = pass_shift;

	// Stably sort the tile by the current 8-bit digit, so equal digits are
	// contiguous; then li - run_start is this lane's stable rank within its run.
	local_sort(li, shift);
	uint run_start = run_start_of(li, active_count, shift);

	if (li < active_count) {
		uint my_digit = (s_keys[li] >> shift) & RADIX_MASK;
		// Global base for this tile's run of `my_digit` (from the scanned
		// histogram) plus the rank within that run.
		uint global_pos = g_hist[my_digit * num_tiles + tile] + (li - run_start);
		g_keys_out[global_pos] = s_keys[li];
		g_vals_out[global_pos] = s_vals[li];
	}
}
