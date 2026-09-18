/// renderer_null.h
///
/// 2026 blk

#pragma once

#include "renderer.h"

/// Renderer_Null
///
/// THROWAWAY SPIKE SCAFFOLDING - see the Phase 5 web-viewer plan.
///
/// A backend that draws nothing. It exists to prove the engine core runs on a
/// platform with no GPU backend at all, which is the question the Emscripten
/// feasibility spike has to answer before any WebGPU code is worth writing.
///
/// It leans on the seam the render graph already provides: `get_pass_execute()`
/// is left at its base-class `nullptr`, so `run_render_graph()` skips every
/// declared pass and assembles an empty graph. No render targets are allocated,
/// no shaders are compiled, no barriers are emitted. `present()` is the only
/// place anything happens - it clears the target to a slowly cycling colour, so
/// that "the frame loop is actually ticking" is visible rather than inferred.
///
/// Delete this once `Renderer_WebGpu` clears its own swapchain.
class Renderer_Null : public Renderer {
public:
	Renderer_Null() = default;
	~Renderer_Null() override = default;

	u32 load_texture(const std::string& path, LoadTextureParams& params) override;

private:
	void initialize_internal(HWND hwnd, const uint32_t frame_width, const uint32_t frame_height) override;
	void shut_down_internal() override;

	void present() override;

	RenderPipeline* create_gpu_pipeline(const std::string& friendly_name, const std::string& path) override;
	RenderPipeline* create_compute_pipeline(const std::string& friendly_name, const std::string& path) override;
	RenderBuffer* create_render_buffer_internal() override;

	u32 m_frame_index = 0;
};
