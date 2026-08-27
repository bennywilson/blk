/// renderer_factory.cpp
///
/// 2025 blk 1.0

#include "blk_core.h"
#include "entity_header.h"
#include "renderer_factory.h"
#include <dxgi1_6.h>
#include "d3d12/renderer_dx12.h"
#include "sw/renderer_sw.h"
#include "vk/renderer_vk.h"

/// try_parse_renderer_backend
bool try_parse_renderer_backend(const std::string& name, ERendererBackend& backend) {
	if (name == "dx12") {
		backend = ERendererBackend::D3D12;
	} else if (name == "vk") {
		backend = ERendererBackend::Vulkan;
	} else if (name == "sw") {
		backend = ERendererBackend::Software;
	} else {
		return false;
	}

	return true;
}

/// create_renderer
Renderer* create_renderer(const ERendererBackend backend) {
	switch (backend) {
		case ERendererBackend::D3D12:
			return new Renderer_Dx12();

		case ERendererBackend::Vulkan:
			return new Renderer_Vk();

		case ERendererBackend::Software:
			return new Renderer_Sw();
	}

	blk::error("create_renderer - unhandled backend %d", (int)backend);
	return nullptr;
}
