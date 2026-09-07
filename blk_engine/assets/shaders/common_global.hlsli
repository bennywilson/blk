/// common_global.hlsli
///
/// 2026 blk

// Deliberately a macro guard, not `#pragma once`. The engine compiles these
// through IDxcUtils::CreateDefaultIncludeHandler with an engine-built -I path,
// and the same file arrives spelled two different ways depending on who
// included it -- `...shaders\common_global.hlsli` from a shader's own #include
// versus `...shaders/common_global.hlsli` from inside another .hlsli. DXC keys
// `#pragma once` on that resolved path string, so the mismatched separators
// made it compile this header twice and fail with redefinition errors. A macro
// guard does not care how the path is spelled.
//
// Note this only reproduces in-engine: standalone dxc.exe normalises the
// separators, so compiling these shaders on the command line will NOT catch it.
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
