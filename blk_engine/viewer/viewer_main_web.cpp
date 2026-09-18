/// viewer_main_web.cpp
///
/// 2026 blk

/// Emscripten entry point for the web viewer.
///
/// THROWAWAY SPIKE SCAFFOLDING - see the Phase 5 web-viewer plan.
///
/// This is deliberately NOT a port of `blaise/src/main.cpp`. That file is a
/// Win32 host: window class, `WndProc`, message pump, editor construction. The
/// viewer needs none of it, so this brings up the smallest thing that can prove
/// the spike's claim - engine core initialized, renderer constructed through
/// the normal factory seam, frame loop ticking, canvas being written to.
///
/// No level loading, no entities, no input. Those come after the core is known
/// to run at all, which is the only question this file exists to answer.

#include <emscripten/emscripten.h>
#include <emscripten/html5.h>
#include "blk_core.h"
// renderer.h is not self-contained - it needs Vec3/Quat4/RenderPipeline/
// ViewContext, which arrive via entity_header.h. Every other includer of
// renderer.h does the same thing, so match it rather than fix it here.
#include "entity_header.h"
#include "renderer.h"
#include "renderer_factory.h"

namespace {
	// The canvas size the "swapchain" reports. Fixed for the spike; a real
	// viewer reads it from the canvas and handles resize.
	constexpr uint32_t k_frame_width = 1280;
	constexpr uint32_t k_frame_height = 720;

	Timer g_frame_timer;
	uint32_t g_frames_rendered = 0;

	/// tick
	///
	/// One frame, driven by the browser's rAF via `emscripten_set_main_loop`.
	void tick() {
		if (g_renderer == nullptr) {
			return;
		}

		g_renderer->render();
		g_frames_rendered++;

		// One line a second, as the headless proof that the loop is advancing
		// even if the canvas is never looked at.
		if (g_frame_timer.TimeElapsedSeconds() >= 1.0f) {
			blk::log("viewer - %u frames in the last second", g_frames_rendered);
			g_frames_rendered = 0;
			g_frame_timer.Reset();
		}
	}
}

/// main
int main() {
	blk::log("blk_engine web viewer - starting");

	blk::initialize_engine();

	g_renderer = create_renderer("null");
	if (g_renderer == nullptr) {
		blk::error("viewer - create_renderer() returned nullptr");
		return 1;
	}

	g_renderer->initialize(nullptr, k_frame_width, k_frame_height);

	blk::log("viewer - entering main loop");
	g_frame_timer.Reset();

	// 0 fps means "use requestAnimationFrame"; the 1 makes Emscripten throw to
	// unwind out of main() while keeping the runtime (and our globals) alive.
	emscripten_set_main_loop(tick, 0, 1);

	return 0;
}
