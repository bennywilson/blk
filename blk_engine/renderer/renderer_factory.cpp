/// renderer_factory.cpp
///
/// 2026 blk

#include "blk_core.h"
#include "entity_header.h"
#include "renderer_factory.h"
#if defined(_WIN32)
	#include <dxgi1_6.h>
	#include "d3d12/renderer_dx12.h"
	#include "sw/renderer_sw.h"
	#include "vk/renderer_vk.h"
	#include "webgpu/renderer_webgpu.h"
#elif defined(__EMSCRIPTEN__)
	#include "null/renderer_null.h"
	#include "webgpu/renderer_webgpu.h"
#else
	#include "null/renderer_null.h"
#endif

/// create_renderer
Renderer* create_renderer(const ERendererBackend backend) {
	switch (backend) {
#if defined(_WIN32)
		case ERendererBackend::D3D12:
			return new Renderer_Dx12();

		case ERendererBackend::Vulkan:
			return new Renderer_Vk();

		case ERendererBackend::Software:
			return new Renderer_Sw();

		case ERendererBackend::WebGpu:
			return new Renderer_WebGpu();

		// Not built on Windows -- falls through to the error below, which is
		// the honest answer to "-renderer=null" on a platform that has real ones.
		case ERendererBackend::Null:
			break;
#else
	#if defined(__EMSCRIPTEN__)
		// The browser has exactly one real backend, and it is the same
		// Renderer_WebGpu the native build uses - Dawn there, the browser's own
		// WebGPU here.
		case ERendererBackend::WebGpu:
			return new Renderer_WebGpu();
	#else
		case ERendererBackend::WebGpu:
			return new Renderer_Null();
	#endif

		// The rest have no implementation off Windows; the null backend at least
		// runs, rather than hiding that behind a failure.
		case ERendererBackend::D3D12:
		case ERendererBackend::Vulkan:
		case ERendererBackend::Software:
		case ERendererBackend::Null:
			return new Renderer_Null();
#endif
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
	if (name == "webgpu") {
		return create_renderer(ERendererBackend::WebGpu);
	}
	if (name == "null") {
		return create_renderer(ERendererBackend::Null);
	}

	if (!name.empty()) {
		blk::warn("Unrecognized renderer backend '%s', defaulting to dx12", name.c_str());
	}

	return create_renderer(ERendererBackend::D3D12);
}
