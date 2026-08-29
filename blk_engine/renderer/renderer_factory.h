/// renderer_factory.h
///
/// 2026 blk

#pragma once

#include <string>

class Renderer;

enum class ERendererBackend {
	D3D12,
	Vulkan,
	Software
};

Renderer* create_renderer(const ERendererBackend backend);

// Maps a backend name ("dx12"/"vk"/"sw") to the matching backend then constructs it
Renderer* create_renderer(const std::string& name);
