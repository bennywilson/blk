/// renderer_factory.h
///
/// 2025 blk 1.0

#pragma once

#include <string>

class Renderer;

enum class ERendererBackend {
	D3D12,
	Vulkan,
	Software
};

// Returns false and leaves backend untouched if name doesn't match a known backend
bool try_parse_renderer_backend(const std::string& name, ERendererBackend& backend);

Renderer* create_renderer(const ERendererBackend backend);
