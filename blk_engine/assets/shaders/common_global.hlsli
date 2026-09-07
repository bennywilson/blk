/// common_global.hlsli
///
/// 2026 blk

// Intentional macro guard. Do NOT use `#pragma once`.
//
// DXC's default include handler lacks a real filesystem and falls back to string
// comparisons for `#pragma once`. Because DXC internally mixes slashes (using `\` 
// for -I search paths and `/` for relative includes), it treats the same header 
// as two different files and compiles it twice, causing collisions. 
//
// A macro guard bypasses path strings entirely.
//
// Note: You can't fix this by canonicalizing paths in `renderer_dx12.cpp` because 
// the mismatch is generated internally by DXC. Standalone `dxc.exe` won't catch 
// this either. Test all header changes directly in `blaise`.
#ifndef BLK_COMMON_GLOBAL_HLSLI
#define BLK_COMMON_GLOBAL_HLSLI

/// SceneIndex
///
/// The per-draw index into the bindless scene_constants[] array, bound at
/// (b0, space1) by every pass that reads a per-instance or per-light slot.
struct SceneIndex {
	uint index;
};

/// Specular gbuffer encoding
///
/// ERenderTarget::Specular is R8G8B8A8_UNORM, so every channel is [0,1]:
///   rgb = specular reflectance colour, from the material's "spec" param
///   a   = gloss, turned into a Blinn-Phong exponent by gloss_to_spec_power()
///
/// Defined here rather than in each shader because the gbuffer passes write
/// this and the light passes read it -- two places that must agree exactly.
/// The material shaders used to write a flat `o.specular = 1` that no light
/// shader ever sampled, so nothing enforced an encoding at all.
float4 encode_specular(const float3 spec_color, const float gloss) {
	return float4(saturate(spec_color), saturate(gloss));
}

/// gloss_to_spec_power
///
/// A useful Blinn-Phong exponent spans roughly 1..1024, which cannot sit in a
/// UNORM channel raw. Exponential remap so even gloss steps give even steps in
/// perceived highlight tightness: 0 -> 1 (very broad), 1 -> 1024 (very tight).
float gloss_to_spec_power(const float gloss) {
	return exp2(saturate(gloss) * 10.0f);
}

/// GlobalConstantData
///
/// Overlays the C++ GlobalUniformData (renderer_dx12.h) via scene_constants[0]
/// and keeps the total size at 512 bytes to match the actual bound CBV
///
/// Notes: Always keep GlobalConstantData in sync with GlobalUniformData.
struct GlobalConstantData {
	row_major matrix view;
	row_major matrix view_projection;
	row_major matrix inv_view_proj;
	float4 camera;
	float4 pad0;
	float4 pad1;
	// .x = g_srv_descriptor_start: the absolute SRV slot (in the shared
	// CBV/SRV/UAV-type heap) where material textures begin. Material shaders
	// add this to their (still table-relative) texture_list id to get the
	// absolute ResourceDescriptorHeap[] index.
	float4 srv_heap_base;
	float4 pad[16];
};

#endif // BLK_COMMON_GLOBAL_HLSLI
