/// renderer_factory.h
///
/// 2026 blk

#pragma once

#include <string>

class Renderer;

enum class ERendererBackend {
	D3D12,
	Vulkan,
	Software,

	// Dawn's webgpu.h, native here and the browser's own implementation in the
	// wasm build -- see `renderer_webgpu.h`.
	WebGpu,

	// Draws nothing. Only reachable on platforms with no native backend
	// compiled in (the Emscripten viewer spike) -- see `renderer_null.h`.
	Null
};

Renderer* create_renderer(const ERendererBackend backend);

// Maps a backend name ("dx12"/"vk"/"sw") to the matching backend then constructs it
Renderer* create_renderer(const std::string& name);
