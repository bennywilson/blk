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
///     node viewer.js [level] [backend] [ui]   defaults: the_sheep_and_fox_show, webgpu
///     viewer.html?level=gs_test&backend=null
///     viewer.html?ui=imgui                    Dear ImGui overlay (webgpu only)
///
/// The backend is "webgpu" (the browser's own WebGPU, through the same
/// Renderer_WebGpu the native build runs on Dawn) or "null", which draws
/// nothing and is the way to tell an engine problem from a rendering one.
/// node has no WebGPU, so a node run wants "null".

#include <emscripten/emscripten.h>
#include <emscripten/html5.h>
#include <cfloat>
#include <cstring>
#include <filesystem>
#include <map>
#include <unistd.h>
#include "blk_core.h"
// renderer.h is not self-contained - it needs Vec3/Quat4/RenderPipeline/
// ViewContext, which arrive via entity_header.h. Every other includer of
// renderer.h does the same thing, so match it rather than fix it here.
#include "entity_header.h"
#include "file.h"
#include "imgui.h"
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

	/// Free-fly camera
	///
	/// The editor's camera runs through its widget event system, which a viewer
	/// has no use for, so this is its own small thing: WASD across the ground
	/// plane, Q/E down/up, shift to go faster, and mouse-look while the pointer
	/// is locked. Yaw and pitch are kept as angles rather than a quaternion so
	/// pitch can simply be clamped instead of needing to be re-derived.
	struct FlyCamera {
		Vec3 position = Vec3(0.f, 0.f, 0.f);
		float yaw = 0.f;   // around world up
		float pitch = 0.f; // clamped just short of straight up/down
		// Units a second. the_sheep_and_fox_show spreads its props over roughly
		// 1800 units, so this crosses it in about twenty seconds.
		float speed = 90.f;

		// Indexed by the DOM key codes we care about; see on_key().
		bool forward = false;
		bool back = false;
		bool left = false;
		bool right = false;
		bool up = false;
		bool down = false;
		bool fast = false;

		Quat4 rotation() const {
			// Pitch on the left: Quat4's operator* composes the other way round
			// from the usual, and yaw-then-pitch here rolls the horizon.
			return Quat4(Vec3(1.f, 0.f, 0.f), pitch) * Quat4(Vec3(0.f, 1.f, 0.f), yaw);
		}

		void update(const float delta_time) {
			const Mat4 basis = rotation().to_mat4();
			const Vec3 right_axis = basis[0].ToVec3();
			const Vec3 forward_axis = basis[2].ToVec3();

			Vec3 move(0.f, 0.f, 0.f);
			if (forward) {
				move += forward_axis;
			}
			if (back) {
				move -= forward_axis;
			}
			if (right) {
				move += right_axis;
			}
			if (left) {
				move -= right_axis;
			}
			if (up) {
				move += Vec3(0.f, 1.f, 0.f);
			}
			if (down) {
				move -= Vec3(0.f, 1.f, 0.f);
			}

			if (move.length() > 0.f) {
				move.normalize_safe();
				position += move * (speed * (fast ? 4.f : 1.f) * delta_time);
			}
		}
	};

	FlyCamera g_camera;

	/// on_key
	///
	/// One handler for both directions; `down` says which. Returning true marks
	/// the event handled, which is what stops the page scrolling on WASD.
	bool apply_key(const char* const code, const bool down) {
		const std::string key = code;
		if (key == "KeyW") { g_camera.forward = down; return true; }
		if (key == "KeyS") { g_camera.back = down; return true; }
		if (key == "KeyA") { g_camera.left = down; return true; }
		if (key == "KeyD") { g_camera.right = down; return true; }
		if (key == "KeyE") { g_camera.up = down; return true; }
		if (key == "KeyQ") { g_camera.down = down; return true; }
		if (key == "ShiftLeft" || key == "ShiftRight") { g_camera.fast = down; return true; }
		return false;
	}

	/// imgui_active
	///
	/// True once the renderer has made an ImGui context, which it only does when
	/// the page asked for one (`ui=imgui`).
	bool imgui_active() {
		return ImGui::GetCurrentContext() != nullptr;
	}

	/// to_imgui_key
	///
	/// The subset of KeyboardEvent.code that text fields and menus need. Letters
	/// and digits are contiguous in ImGuiKey, so they map by offset.
	ImGuiKey to_imgui_key(const char* const code) {
		const std::string key = code;
		if (key.size() == 4 && key.compare(0, 3, "Key") == 0 && key[3] >= 'A' && key[3] <= 'Z') {
			return (ImGuiKey)(ImGuiKey_A + (key[3] - 'A'));
		}
		if (key.size() == 6 && key.compare(0, 5, "Digit") == 0 && key[5] >= '0' && key[5] <= '9') {
			return (ImGuiKey)(ImGuiKey_0 + (key[5] - '0'));
		}
		static const std::map<std::string, ImGuiKey> k_keys = {
			{ "Backspace", ImGuiKey_Backspace }, { "Delete", ImGuiKey_Delete },
			{ "Enter", ImGuiKey_Enter }, { "NumpadEnter", ImGuiKey_KeypadEnter },
			{ "Tab", ImGuiKey_Tab }, { "Escape", ImGuiKey_Escape }, { "Space", ImGuiKey_Space },
			{ "ArrowLeft", ImGuiKey_LeftArrow }, { "ArrowRight", ImGuiKey_RightArrow },
			{ "ArrowUp", ImGuiKey_UpArrow }, { "ArrowDown", ImGuiKey_DownArrow },
			{ "Home", ImGuiKey_Home }, { "End", ImGuiKey_End },
			{ "PageUp", ImGuiKey_PageUp }, { "PageDown", ImGuiKey_PageDown },
		};
		const auto found = k_keys.find(key);
		return (found != k_keys.end()) ? found->second : ImGuiKey_None;
	}

	/// feed_imgui_key
	///
	/// Returns true when ImGui wants the key, which keeps WASD from moving the
	/// camera while a text field has focus.
	bool feed_imgui_key(const EmscriptenKeyboardEvent* const event, const bool down) {
		if (!imgui_active()) {
			return false;
		}

		ImGuiIO& io = ImGui::GetIO();
		io.AddKeyEvent(ImGuiMod_Ctrl, event->ctrlKey);
		io.AddKeyEvent(ImGuiMod_Shift, event->shiftKey);
		io.AddKeyEvent(ImGuiMod_Alt, event->altKey);

		const ImGuiKey key = to_imgui_key(event->code);
		if (key != ImGuiKey_None) {
			io.AddKeyEvent(key, down);
		}

		// `key` is the produced character ("a", "A", "%") for a printable key
		// and a name ("Enter", "Shift") for anything else.
		if (down && !event->ctrlKey && !event->altKey && strlen(event->key) == 1) {
			io.AddInputCharactersUTF8(event->key);
		}

		return io.WantTextInput;
	}

	EM_BOOL on_key_down(int, const EmscriptenKeyboardEvent* const event, void*) {
		if (feed_imgui_key(event, true)) {
			return EM_TRUE;
		}
		return apply_key(event->code, true) ? EM_TRUE : EM_FALSE;
	}

	EM_BOOL on_key_up(int, const EmscriptenKeyboardEvent* const event, void*) {
		feed_imgui_key(event, false);
		return apply_key(event->code, false) ? EM_TRUE : EM_FALSE;
	}

	/// on_canvas_mouse_move
	///
	/// Registered on the canvas, unlike the camera's window-wide handler, so its
	/// coordinates are canvas-relative CSS pixels. The canvas can be shown
	/// smaller than its 1280x720 backing store, so scale to backing pixels.
	/// While the pointer is locked the mouse belongs to the camera, and ImGui is
	/// told it is off in the distance.
	EM_BOOL on_canvas_mouse_move(int, const EmscriptenMouseEvent* const event, void*) {
		if (!imgui_active()) {
			return EM_FALSE;
		}

		EmscriptenPointerlockChangeEvent lock = {};
		const bool locked = emscripten_get_pointerlock_status(&lock) == EMSCRIPTEN_RESULT_SUCCESS && lock.isActive;

		double css_width = 0.0, css_height = 0.0;
		emscripten_get_element_css_size("#canvas", &css_width, &css_height);
		if (locked || css_width <= 0.0 || css_height <= 0.0) {
			ImGui::GetIO().AddMousePosEvent(-FLT_MAX, -FLT_MAX);
			return EM_FALSE;
		}

		ImGui::GetIO().AddMousePosEvent(
			(float)(event->targetX * k_frame_width / css_width),
			(float)(event->targetY * k_frame_height / css_height));
		return EM_FALSE;
	}

	int to_imgui_button(const unsigned short button) {
		// DOM: 0 left, 1 middle, 2 right. ImGui: 0 left, 1 right, 2 middle.
		return (button == 1) ? 2 : (button == 2) ? 1 : (int)button;
	}

	/// on_window_mouse_button
	///
	/// On the window so a release outside the canvas still reaches ImGui, or a
	/// drag would leave the button stuck down.
	EM_BOOL on_window_mouse_down(int, const EmscriptenMouseEvent* const event, void*) {
		if (imgui_active()) {
			ImGui::GetIO().AddMouseButtonEvent(to_imgui_button(event->button), true);
		}
		return EM_FALSE;
	}

	EM_BOOL on_window_mouse_up(int, const EmscriptenMouseEvent* const event, void*) {
		if (imgui_active()) {
			ImGui::GetIO().AddMouseButtonEvent(to_imgui_button(event->button), false);
		}
		return EM_FALSE;
	}

	EM_BOOL on_canvas_wheel(int, const EmscriptenWheelEvent* const event, void*) {
		if (!imgui_active()) {
			return EM_FALSE;
		}
		// deltaY is in pixels (mode 0); ImGui counts lines, and a browser notch
		// is about 100 pixels. Positive deltaY is scrolling down, ImGui's negative.
		ImGui::GetIO().AddMouseWheelEvent(-(float)event->deltaX / 100.0f, -(float)event->deltaY / 100.0f);
		// Keep the page from scrolling under a panel that took the wheel.
		return ImGui::GetIO().WantCaptureMouse ? EM_TRUE : EM_FALSE;
	}

	/// on_mouse_move
	///
	/// Only steers while the pointer is locked, so moving the mouse over the page
	/// before clicking the canvas does nothing.
	EM_BOOL on_mouse_move(int, const EmscriptenMouseEvent* const event, void*) {
		EmscriptenPointerlockChangeEvent lock = {};
		if (emscripten_get_pointerlock_status(&lock) != EMSCRIPTEN_RESULT_SUCCESS || !lock.isActive) {
			return EM_FALSE;
		}

		constexpr float k_sensitivity = 0.0025f;
		constexpr float k_pitch_limit = 1.55f; // just under pi/2, so up never flips
		// Minus, not plus: Quat4's rotation about +Y turns forward toward -X (its
		// row 2 comes out as (-sin, 0, cos)), so turning right - toward +X - is a
		// negative yaw. Likewise a positive pitch looks up, so moving the mouse
		// down (positive movementY) is a negative pitch.
		g_camera.yaw -= event->movementX * k_sensitivity;
		g_camera.pitch = blk::clamp(g_camera.pitch - event->movementY * k_sensitivity, -k_pitch_limit, k_pitch_limit);
		return EM_TRUE;
	}

	/// Clicking the canvas grabs the pointer; Escape releases it, which the
	/// browser handles itself.
	EM_BOOL on_mouse_down(int, const EmscriptenMouseEvent*, void*) {
		// A click on a panel is the panel's, not a request to look around.
		if (imgui_active() && ImGui::GetIO().WantCaptureMouse) {
			return EM_FALSE;
		}
		emscripten_request_pointerlock("#canvas", EM_TRUE);
		return EM_TRUE;
	}

	/// A lost pointer lock must not leave a key stuck down.
	EM_BOOL on_pointerlock_change(int, const EmscriptenPointerlockChangeEvent* const event, void*) {
		if (!event->isActive) {
			g_camera.forward = g_camera.back = g_camera.left = g_camera.right = false;
			g_camera.up = g_camera.down = g_camera.fast = false;
		}
		return EM_TRUE;
	}

	/// install_input
	void install_input() {
		// Keys go on the window: the canvas only sees them when focused, and a
		// click that grabs the pointer does not necessarily focus it.
		emscripten_set_keydown_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, nullptr, EM_TRUE, on_key_down);
		emscripten_set_keyup_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, nullptr, EM_TRUE, on_key_up);
		emscripten_set_mousedown_callback("#canvas", nullptr, EM_TRUE, on_mouse_down);
		emscripten_set_mousemove_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, nullptr, EM_TRUE, on_mouse_move);
		emscripten_set_mousemove_callback("#canvas", nullptr, EM_TRUE, on_canvas_mouse_move);
		emscripten_set_mousedown_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, nullptr, EM_TRUE, on_window_mouse_down);
		emscripten_set_mouseup_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, nullptr, EM_TRUE, on_window_mouse_up);
		emscripten_set_wheel_callback("#canvas", nullptr, EM_TRUE, on_canvas_wheel);
		emscripten_set_pointerlockchange_callback(EMSCRIPTEN_EVENT_TARGET_DOCUMENT, nullptr, EM_TRUE, on_pointerlock_change);
	}

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
			// Seed the fly camera from the level's saved view. The yaw/pitch come
			// back out of the saved rotation's forward vector rather than being
			// stored, since the camera keeps angles and the level keeps a quat.
			g_camera.position = level_settings->m_CameraPosition;

			// The sign here is not arbitrary: with -asinf the camera starts aimed
			// at the sky, because a positive pitch about X tips the forward vector
			// down in this engine's convention, not up.
			const Vec3 forward = level_settings->m_CameraRotation.to_mat4()[2].ToVec3();
			g_camera.yaw = atan2f(-forward.x, forward.z);
			g_camera.pitch = asinf(blk::clamp(forward.y, -1.f, 1.f));

			g_renderer->set_camera_transform(g_camera.position, g_camera.rotation());
			blk::log("viewer - camera from level settings (%.1f, %.1f, %.1f), yaw %.2f pitch %.2f",
				g_camera.position.x, g_camera.position.y, g_camera.position.z, g_camera.yaw, g_camera.pitch);
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

		g_camera.update(delta_time);
		g_renderer->set_camera_transform(g_camera.position, g_camera.rotation());

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
	// Read by Renderer_WebGpu::initialize_internal, so set before initialize().
	g_web_imgui = (argc > 3) && std::string(argv[3]) == "imgui";

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

	install_input();
	blk::log("viewer - entering main loop (click the canvas to look, WASD/QE to move, shift for speed)");
	g_frame_timer.Reset();
	g_tick_timer.Reset();

	// 0 fps means "use requestAnimationFrame"; the 1 makes Emscripten throw to
	// unwind out of main() while keeping the runtime (and our globals) alive.
	emscripten_set_main_loop(tick, 0, 1);

	return 0;
}
