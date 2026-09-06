/// common_scene.hlsli
///
/// 2026 blk

#pragma once

#include "common_global.hlsli"

/// BaseData
///
/// The raw shape of a per-instance constant buffer slot: one homogeneous
/// matrix array covering the whole 512-byte CBV bound from the C++
/// SceneInstanceData (renderer_dx12.h). Material shaders bind this and then
/// cast to SceneData (or, where a shader has one, its own BoneData).
///
/// It must stay a homogeneous array rather than SceneData itself -- the cast
/// below is only well-defined out of a single flat field.
///
/// Notes: 8 matrices * 64 bytes = 512 bytes, exactly the bound CBV size.
///        Do not enlarge this to reach past the slot; it is a window, not a
///        request for more memory.
struct BaseData {
	row_major matrix pad0[8];
};

/// SceneData
///
/// Overlays the C++ SceneInstanceData (renderer_dx12.h) via
/// scene_constants[scene_index.index] in the material passes.
///
/// Reached through `(SceneData)base_instance`. That cast is NOT a byte-level
/// reinterpret: HLSL flattens both structs to an ordered scalar stream and
/// assigns component by component, so cbuffer packing rules never enter into
/// it. Two consequences, both load-bearing:
///
///   - `float texture_list[16]` consumes exactly 16 consecutive floats and
///     lines up with the tightly-packed C++ `f32 texture_list[16]`, rather
///     than burning a 16-byte register per element the way a real cbuffer
///     array would.
///   - Members match by POSITION, not by name or offset. Inserting or
///     reordering a field silently shifts every field below it and cannot
///     produce a compile error. entity_id sits at offset 304, immediately
///     after texture_list, and the C++ side static_asserts that.
///
/// Notes: Always keep SceneData in sync with SceneInstanceData.
struct SceneData {
	row_major matrix mvp_matrix;
	row_major matrix world_matrix;
	row_major matrix inv_world_matrix;
	float4 color;
	float4 spec;
	float4 time_since_spawn;
	float texture_list[16];
	// .x is the owning entity's id, written straight out to the EntityId
	// gbuffer target for viewport picking.
	float4 entity_id;
};
