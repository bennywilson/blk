/// viewer_main_web.cpp
///
/// 2026 blk

/// Emscripten entry point for the web viewer.
///
/// THROWAWAY SPIKE SCAFFOLDING - see the Phase 5 web-viewer plan.
///
/// This is deliberately NOT a port of `blaise/src/main.cpp`. That file is a
/// Win32 host: window class, `WndProc`, message pump, editor construction. The
/// viewer needs none of it: engine core, the renderer through the normal factory
/// seam, one level read with `File`, and a frame loop.
///
/// Reading the level is the whole load path. `File::ReadGameEntity()` enables
/// each component as it reads it, which loads its resources through
/// `g_ResourceManager` and registers it with `g_renderer` - so even with the null
/// backend, everything a real backend would be handed gets built.
///
/// Assets come from the virtual filesystem `tools/wasm/stage_assets.py` packs,
/// laid out as the repo is (`/blk/blaise`, `/blk/blk_engine`) with every path
/// lowercased to match the keys `ResourceManager::resource()` builds.
///
///     node viewer.js [level] [backend]   defaults: the_sheep_and_fox_show, webgpu
///     viewer.html?level=gs_test&backend=null
///
/// The backend is "webgpu" (the browser's own WebGPU, through the same
/// Renderer_WebGpu the native build runs on Dawn) or "null", which draws
/// nothing and is the way to tell an engine problem from a rendering one.
/// node has no WebGPU, so a node run wants "null".

#include <emscripten/emscripten.h>
#include <filesystem>
#include <map>
#include <unistd.h>
#include "blk_core.h"
// renderer.h is not self-contained - it needs Vec3/Quat4/RenderPipeline/
// ViewContext, which arrive via entity_header.h. Every other includer of
// renderer.h does the same thing, so match it rather than fix it here.
#include "entity_header.h"
#include "file.h"
#include "renderer.h"
#include "renderer_factory.h"

namespace {
	// The canvas size the "swapchain" reports. Fixed for the spike; a real
	// viewer reads it from the canvas and handles resize.
	constexpr uint32_t k_frame_width = 1280;
	constexpr uint32_t k_frame_height = 720;

	// Where the native build starts: `initialize_engine()` does `chdir("../")`,
	// which lands in `/blk/blaise`, the directory every asset path is relative to.
	constexpr const char* k_start_directory = "/blk/blaise/src";

	Timer g_frame_timer;
	Timer g_tick_timer;
	uint32_t g_frames_rendered = 0;

	std::vector<GameEntity*> g_entities;

	/// find_level
	///
	/// Levels sit under `assets/levels/`, some a folder down, and the native
	/// loader searches for them by name. Mirrors that with a recursive search.
	std::string find_level(const std::string& name) {
		const std::string file_name = name + ".blklevel";

		std::error_code ec;
		for (const auto& entry : std::filesystem::recursive_directory_iterator("assets/levels", ec)) {
			if (entry.is_regular_file() && entry.path().filename() == file_name) {
				return entry.path().string();
			}
		}

		return "";
	}

	/// load_level
	bool load_level(const std::string& name) {
		const float start_time = g_GlobalTimer.TimeElapsedSeconds();

		const std::string path = find_level(name);
		if (path.empty()) {
			blk::warn("viewer - no level named %s under assets/levels", name.c_str());
			return false;
		}

		File file;
		if (!file.Open(path, File::FT_Read)) {
			blk::warn("viewer - could not open %s", path.c_str());
			return false;
		}

		std::map<std::string, int> component_counts;
		const EditorLevelSettingsComponent* level_settings = nullptr;

		while (GameEntity* const entity = file.ReadGameEntity()) {
			for (int i = 0; i < entity->num_components(); i++) {
				component_counts[entity->component(i)->GetComponentClassName()]++;
			}

			if (level_settings == nullptr) {
				level_settings = (const EditorLevelSettingsComponent*)entity->GetComponentByType(EditorLevelSettingsComponent::GetType());
			}

			g_entities.push_back(entity);
		}
		file.Close();

		blk::log("viewer - loaded %s: %zu entities in %.3f s", path.c_str(), g_entities.size(), g_GlobalTimer.TimeElapsedSeconds() - start_time);
		for (const auto& [class_name, count] : component_counts) {
			blk::log("viewer -   %4d  %s", count, class_name.c_str());
		}

		if (level_settings != nullptr) {
			g_renderer->set_camera_transform(level_settings->m_CameraPosition, level_settings->m_CameraRotation);
			blk::log("viewer - camera from level settings (%.1f, %.1f, %.1f)",
				level_settings->m_CameraPosition.x, level_settings->m_CameraPosition.y, level_settings->m_CameraPosition.z);
		}

		return true;
	}

	/// tick
	///
	/// One frame, driven by the browser's rAF via `emscripten_set_main_loop`.
	void tick() {
		if (g_renderer == nullptr) {
			return;
		}

		// Update then render_sync, the order Editor::Update and the game loop both
		// use. Both halves are needed, and neither is gameplay: update gives
		// anything animated a pose, and render_sync is what hands the renderer the
		// result - for a ParticleComponent that includes the vertex buffer it
		// spends the next update writing into.
		const float delta_time = (std::min)(g_tick_timer.TimeElapsedSeconds(), 0.1f);
		g_tick_timer.Reset();
		for (GameEntity* const entity : g_entities) {
			entity->update(delta_time);
		}
		for (GameEntity* const entity : g_entities) {
			entity->render_sync();
		}
		g_ResourceManager.render_sync();

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
int main(int argc, char** argv) {
	const std::string level_name = (argc > 1) ? argv[1] : "the_sheep_and_fox_show";
	const std::string backend_name = (argc > 2) ? argv[2] : "webgpu";

	if (chdir(k_start_directory) != 0) {
		printf("viewer - %s is missing; were the assets staged? (tools/wasm/stage_assets.py)\n", k_start_directory);
		return 1;
	}

	blk::initialize_engine();
	blk::log("blk_engine web viewer - starting, level %s, backend %s", level_name.c_str(), backend_name.c_str());

	g_renderer = create_renderer(backend_name);
	if (g_renderer == nullptr) {
		blk::error("viewer - create_renderer() returned nullptr");
		return 1;
	}

	g_renderer->initialize(nullptr, k_frame_width, k_frame_height);

	// blk::error() throws the formatted message; catch it so a bad asset
	// reports what failed instead of taking the runtime down silently.
	try {
		load_level(level_name);
	} catch (char* const message) {
		blk::log("viewer - level load threw: %s", message);
	}

	blk::log("viewer - entering main loop");
	g_frame_timer.Reset();
	g_tick_timer.Reset();

	// 0 fps means "use requestAnimationFrame"; the 1 makes Emscripten throw to
	// unwind out of main() while keeping the runtime (and our globals) alive.
	emscripten_set_main_loop(tick, 0, 1);

	return 0;
}
