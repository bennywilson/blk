/// renderer_factory.cpp
///
/// 2026 blk 1.0

#include "blk_core.h"
#include "entity_header.h"
#include "renderer_factory.h"
#include <dxgi1_6.h>
#include "d3d12/renderer_dx12.h"
#include "sw/renderer_sw.h"
#include "vk/renderer_vk.h"

/// create_renderer
Renderer*  create_renderer(const ERendererBackend backend) {
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

/// create_renderer
Renderer* create_renderer(const std::string& name) {
	if (name == "dx12") {
		return create_renderer(ERendererBackend::D3D12);
	}
	if (name == "vk") {
		return create_renderer(ERendererBackend::Vulkan);
	}
	if (name == "sw") {
		return create_renderer(ERendererBackend::Software);
	}

	if (!name.empty()) {
		blk::warn("Unrecognized renderer backend '%s', defaulting to dx12", name.c_str());
	}

	return create_renderer(ERendererBackend::D3D12);
}
