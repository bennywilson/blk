/// renderer_webgpu.h
///
/// 2026 blk

#pragma once

#include <webgpu/webgpu.h>
#include "renderer.h"

/// Renderer_WebGpu
///
/// The backend the web viewer will run on, hand-written against Dawn's
/// `webgpu.h` (Phase 5). Dawn implements that API over D3D12 here and the
/// browser implements it in the wasm build, so this is one backend for both -
/// and it is debugged natively first, where RenderDoc works.
///
/// First milestone, and all this does today: bring up the device against the
/// editor's window and clear it. `get_pass_execute()` is left at the base
/// class's nullptr, so `run_render_graph()` skips every declared pass, the same
/// way `Renderer_Null` does. The pass bodies come next, one at a time.
///
/// Selected with `-renderer=webgpu`; D3D12 stays the default.
class Renderer_WebGpu : public Renderer {
public:
	~Renderer_WebGpu() override;

	u32 load_texture(const std::string& path, LoadTextureParams& params) override;

private:
	void initialize_internal(HWND hwnd, const uint32_t frame_width, const uint32_t frame_height) override;
	void shut_down_internal() override;

	void present() override;

	RenderPipeline* create_gpu_pipeline(const std::string& friendly_name, const std::string& path) override;
	RenderPipeline* create_compute_pipeline(const std::string& friendly_name, const std::string& path) override;
	RenderBuffer* create_render_buffer_internal() override;

	// Each blocks on wgpuInstanceProcessEvents until its callback fires. Fine at
	// startup, which is the only place they run.
	bool request_adapter();
	bool request_device();

	WGPUInstance m_instance = nullptr;
	WGPUAdapter m_adapter = nullptr;
	WGPUDevice m_device = nullptr;
	WGPUQueue m_queue = nullptr;
	WGPUSurface m_surface = nullptr;
	WGPUTextureFormat m_surface_format = WGPUTextureFormat_Undefined;

	u32 m_frame_index = 0;
};
