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

/// Binding macros - one shader source, two binding models
///
/// D3D12 (the default) is bindless: textures come from ResourceDescriptorHeap[],
/// and frame/draw constants are entries in an unbounded ConstantBuffer<T> table
/// picked by index. WebGPU has neither, so the web build - compiled with
/// `-D BLK_WEB` by `tools/shaders/hlsl_to_wgsl.py` - binds each of them directly.
/// That works because every access has one fixed role (a material's texture,
/// gbuffer target N, the frame's constants, the current draw's), so the index
/// the shader computes on D3D12 becomes the WebGPU backend's choice of what to
/// bind. The web build's bind groups, ordered by how often they change:
///
///     group 0  per frame  frame constants (the sampler is already in space0)
///     group 1  per pass   textures
///     group 2  per draw   draw constants and bones, as dynamic offsets
///
/// On D3D12 each macro expands to the plain bindless expression, so the DXIL is
/// the same as writing it out by hand.
///
///     BLK_TEXTURE(slot, heap_index)     a texture; `slot` is its web binding
///     BLK_SCENE_TABLE(Type, name)       declares a scene constants table
///     BLK_BONE_TABLE(Type, name)        declares a bone matrix table
///     BLK_FRAME_CONSTANTS(name)         the frame's entry in a table
///     BLK_DRAW_CONSTANTS(name, index)   the current draw's entry in a table
#if defined(BLK_WEB)
Texture2D<float4> blk_texture_0 : register(t0, space1);
Texture2D<float4> blk_texture_1 : register(t1, space1);
Texture2D<float4> blk_texture_2 : register(t2, space1);
Texture2D<float4> blk_texture_3 : register(t3, space1);
Texture2D<float4> blk_texture_4 : register(t4, space1);

	#define BLK_TEXTURE(slot, heap_index) blk_texture_##slot
	#define BLK_SCENE_TABLE(Type, name) \
		ConstantBuffer<Type> name##_frame : register(b0, space0); \
		ConstantBuffer<Type> name##_draw : register(b0, space2)
	#define BLK_BONE_TABLE(Type, name) ConstantBuffer<Type> name##_draw : register(b1, space2)
	#define BLK_FRAME_CONSTANTS(name) name##_frame
	#define BLK_DRAW_CONSTANTS(name, index) name##_draw
#else
	#define BLK_TEXTURE(slot, heap_index) ResourceDescriptorHeap[heap_index]
	#define BLK_SCENE_TABLE(Type, name) ConstantBuffer<Type> name[] : register(b0)
	#define BLK_BONE_TABLE(Type, name) ConstantBuffer<Type> name[] : register(b0, space2)
	#define BLK_FRAME_CONSTANTS(name) name[0]
	#define BLK_DRAW_CONSTANTS(name, index) name[index]
#endif

/// Specular gbuffer encoding
///
/// ERenderTarget::Specular is R8G8B8A8_UNORM, so every channel is [0,1]:
///   rgb = specular reflectance colour, from the material's "spec" param
///   a   = gloss, turned into a Blinn-Phong exponent by gloss_to_spec_power()
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
