/// editor.cpp
///
/// 2016 blk

#include <filesystem>
#include "blk_core.h"
#if defined(_WIN32)
	#include <windowsx.h>
#endif
#include "blk_containers.h"
#include "game.h"
#include "file.h"
#include "viewport_panel.h"
#include "resources_panel.h"
#include "outliner_panel.h"
#include "properties_panel.h"
#include "workbench_panel.h"
#include "editor.h"
#include "editor_entity.h"
#include "editor_platform.h"
#include "renderer.h"
// Exposes DockBuilder*, which DrawDockSpace() needs for the default layout.
#include "imgui_internal.h"

static const char* const g_EditorWindowClassName = "blk Editor";

Editor* g_Editor = nullptr;
bool g_bEditorIsUndoingAnAction = false;

// Keep the type in sync with the extern declaration in workbench_panel.cpp.
std::vector<LogEntry> g_OutputLog;

// Camera-speed presets, indexed by the value persisted in editorSettings.txt.
struct EditorCamSpeedBind {
	EditorCamSpeedBind(const String& displayName, const float multiplier) :
		m_DisplayName(displayName),
		m_SpeedMultiplier(multiplier) {}

	String m_DisplayName;
	float m_SpeedMultiplier;
};

static const EditorCamSpeedBind g_EditorCamSpeedBindings[] = {
	EditorCamSpeedBind(String("0.05x"), 0.05f),
	EditorCamSpeedBind(String("0.25x"), 0.25f),
	EditorCamSpeedBind(String("1x"), 1.0f),
	EditorCamSpeedBind(String("5x"), 5.0f),
	EditorCamSpeedBind(String("15x"), 15.0f),
	EditorCamSpeedBind(String("35x"), 35.0f),
	EditorCamSpeedBind(String("50x"), 50.0f)
};
static const size_t g_NumEditorCamSpeedBindings = sizeof(g_EditorCamSpeedBindings) / sizeof(EditorCamSpeedBind);

/// Editor
Editor::Editor() {
	m_bGameUpdating = false;
	const float editorInitStartTime = g_GlobalTimer.TimeElapsedSeconds();

	m_UndoIDAtLastSave = UINT64_MAX;
	m_CurrentLevelFileName = "Untitled";

	g_Editor = this;

	m_pGame = nullptr;

	g_OutputCB = Editor::OutputCB;

#if defined(_WIN32)
	{
		const int Screen_Width = GetSystemMetrics(SM_CXFULLSCREEN);
		const int Screen_Height = GetSystemMetrics(SM_CYFULLSCREEN);

		// Calls A-suffixed Win32 explicitly: blk_engine builds MultiByte, and imgui_impl_win32 handles ANSI WM_CHAR via MultiByteToWideChar.
		WNDCLASSEXA window_class = {};
		window_class.cbSize = sizeof(window_class);
		window_class.style = CS_HREDRAW | CS_VREDRAW;
		window_class.lpfnWndProc = Editor::WndProc;
		window_class.hInstance = GetModuleHandleA(nullptr);
		window_class.hCursor = LoadCursor(nullptr, IDC_ARROW);
		window_class.lpszClassName = g_EditorWindowClassName;
		RegisterClassExA(&window_class);

		// TODO: Blocks resizing until the swapchain, fixed-size with DXGI_SCALING_STRETCH, gets a resize path.
		const DWORD window_style = WS_OVERLAPPEDWINDOW & ~WS_THICKFRAME & ~WS_MAXIMIZEBOX;

		// Insets the whole frame within SPI_GETWORKAREA, not SM_CXFULLSCREEN (a maximized client), so the title bar clears the taskbar.
		RECT work_area = {};
		if (!SystemParametersInfoA(SPI_GETWORKAREA, 0, &work_area, 0)) {
			work_area = { 0, 0, Screen_Width, Screen_Height };
		}

		const int Window_Margin = 12;
		m_hwnd = CreateWindowExA(
			0, g_EditorWindowClassName, "blk 1.0", window_style,
			work_area.left + Window_Margin, work_area.top + Window_Margin,
			(work_area.right - work_area.left) - Window_Margin * 2,
			(work_area.bottom - work_area.top) - Window_Margin * 2,
			nullptr, nullptr, GetModuleHandleA(nullptr), nullptr);

		blk::error_check(m_hwnd, "Editor::Editor() - Failed to create the editor window.");
	}
#endif

	// Fills the window with the viewport so ImGui panels float over the scene, not beside it.
	m_pViewportPanel = new ViewportPanel();
	RegisterImGuiPanel(m_pViewportPanel);

	m_pResourcesPanel = new ResourcesPanel();
	RegisterImGuiPanel(m_pResourcesPanel);

	m_pOutlinerPanel = new OutlinerPanel();
	RegisterImGuiPanel(m_pOutlinerPanel);

	m_pPropertiesPanel = new PropertiesPanel();
	RegisterImGuiPanel(m_pPropertiesPanel);

	m_pWorkbenchPanel = new WorkbenchPanel();
	RegisterImGuiPanel(m_pWorkbenchPanel);

#if defined(_WIN32)
	ShowWindow(m_hwnd, SW_SHOW);
	UpdateWindow(m_hwnd);
#endif

	m_pResourcesPanel->PostRendererInit();

	m_bIsRunning = true;

	m_Timer.Reset();

	editor_platform::set_window_title("blk Editor");

	// Restores the camera-speed preset saved by ~Editor.
	EditorGlobalSettingsComponent* pEditorGlobalComponent = nullptr;

	File levelEditorFile;
	if (levelEditorFile.Open("./assets/editorSettings.txt", File::FT_Read)) {
		const GameEntity* const gameEntity = levelEditorFile.ReadGameEntity();
		pEditorGlobalComponent = (EditorGlobalSettingsComponent*)gameEntity->GetComponentByType(EditorGlobalSettingsComponent::GetType());
		levelEditorFile.Close();
	}

	if (!pEditorGlobalComponent) {
		SetCamSpeedIndex(0);
	} else {
		SetCamSpeedIndex(pEditorGlobalComponent->m_CameraSpeedIdx);
	}

	blk::log("Editor init time took %f seconds", g_GlobalTimer.TimeElapsedSeconds() - editorInitStartTime);
}

/// ~Editor
Editor::~Editor() {
	// Disarms WndProc first so late messages (WM_DESTROY) fall to DefWindowProcA, not freed state.
	// Covers exits that skip request_quit(), which normally clears it.
	m_bIsRunning = false;

	// Persists the camera-speed preset for the next session.
	File outFile;
	outFile.Open("./assets/editorSettings.txt", File::FT_Write);

	GameEntity levelInfoEnt;
	EditorGlobalSettingsComponent* const pLevelInfo = new EditorGlobalSettingsComponent();
	pLevelInfo->m_CameraSpeedIdx = m_CamSpeedIdx;
	levelInfoEnt.add_component(pLevelInfo);
	outFile.WriteGameEntity(&levelInfoEnt);
	outFile.Close();

	for (int i = 0; i < m_GameEntities.size(); i++) {
		delete m_GameEntities[i];
	}
	m_GameEntities.clear();

	g_ResourceManager.shut_down();

	// Destroys the window last: WM_CLOSE and File/Quit only call request_quit(),
	// so the in-flight frame finishes against a live window.
#if defined(_WIN32)
	if (m_hwnd) {
		DestroyWindow(m_hwnd);
		m_hwnd = nullptr;
	}
#endif
}

/// Editor::UnloadMap
void Editor::UnloadMap() {
	DeselectEntities();

	for (int i = 0; i < g_Editor->m_GameEntities.size(); i++) {
		delete m_GameEntities[i];
	}
	m_GameEntities.clear();

	m_CurrentLevelFileName = "Untitled";
	m_UndoIDAtLastSave = UINT64_MAX;
	m_UndoStack.Reset();
}

/// Editor::LoadMap
void Editor::LoadMap(const std::string& InMapName) {
	blk::log("LoadMap() called for map %s", InMapName.c_str());
	const float loadMapStartTime = g_GlobalTimer.TimeElapsedSeconds();

	UnloadMap();

	const EditorLevelSettingsComponent* level_settings = nullptr;

	if (!InMapName.empty()) {
		m_CurrentLevelFileName = InMapName;

		std::string level_path = std::filesystem::current_path().string();
		level_path += "/Assets/Levels/";
#if defined(__EMSCRIPTEN__)
		// The web build's staged assets are lowercased end to end, and its filesystem is case-sensitive.
		StringToLower(level_path);
#endif

		// Bare map names try .blklevel first, then the legacy .kblevel.
		std::string legacy_file_name;
		if (m_CurrentLevelFileName.find('.') == std::string::npos) {
			legacy_file_name = m_CurrentLevelFileName + ".kblevel";
			m_CurrentLevelFileName += ".blklevel";
		}

		// Tries the levels folder itself, then each subfolder one level down.
		std::vector<std::string> level_folders = { "" };
		{
			std::error_code directory_error;
			for (const std::filesystem::directory_entry& folder_entry : std::filesystem::directory_iterator(level_path, directory_error)) {
				if (folder_entry.is_directory(directory_error)) {
					level_folders.push_back("/" + folder_entry.path().filename().string() + "/");
				}
			}
		}

		for (const std::string& cur_level_folder : level_folders) {
			std::string next_file_name = level_path + cur_level_folder + m_CurrentLevelFileName;

			File in_file;
			bool opened = in_file.Open(next_file_name.c_str(), File::FT_Read);
			if (!opened && !legacy_file_name.empty()) {
				next_file_name = level_path + cur_level_folder + legacy_file_name;
				opened = in_file.Open(next_file_name.c_str(), File::FT_Read);
			}
			if (opened) {
				m_CurrentLevelFileName = next_file_name;

				GameEntity* game_entity = in_file.ReadGameEntity();
				bool level_entity_found = false;

				while (game_entity) {
					const EditorLevelSettingsComponent* const settings = (EditorLevelSettingsComponent*)game_entity->GetComponentByType(EditorLevelSettingsComponent::GetType());
					if (settings) {
						// Drops duplicate settings entities so they don't load as visible entities.
						if (level_settings) {
							blk::warn("Editor::LoadMap() - Map %s has more than one level settings entity.  Dropping '%s'.",
								InMapName.c_str(), game_entity->name().stl_str().c_str());
							delete game_entity;
							game_entity = in_file.ReadGameEntity();
							continue;
						}

						// Tracks the settings entity for unload-time deletion, hidden from the Outliner as level metadata.
						level_settings = settings;
						EditorEntity* const level_settings_entity = new EditorEntity(game_entity);
						level_settings_entity->SetHidden(true);
						g_Editor->m_GameEntities.push_back(level_settings_entity);

						game_entity = in_file.ReadGameEntity();
						continue;
					}

					// Drops every level entity after the first: LevelComponent owns the global scales through
					// a file-static pointer, and a second instance would silently take it over.
					if (game_entity->GetComponentByType(LevelComponent::GetType())) {
						if (level_entity_found) {
							blk::warn("Editor::LoadMap() - Map %s has more than one level entity.  Dropping '%s'.",
								InMapName.c_str(), game_entity->name().stl_str().c_str());
							delete game_entity;
							game_entity = in_file.ReadGameEntity();
							continue;
						}
						level_entity_found = true;
					}

					EditorEntity* const new_editor_entity = new EditorEntity(game_entity);
					g_Editor->m_GameEntities.push_back(new_editor_entity);
					game_entity = in_file.ReadGameEntity();
				}
				in_file.Close();

				const std::string window_text = "blk Editor - " + InMapName;
				editor_platform::set_window_title(window_text);

				break;
			}
		}
	}

	if (level_settings) {
		SetMainCameraPos(level_settings->m_CameraPosition);
		SetMainCameraRot(level_settings->m_CameraRotation);
	} else {
		SetMainCameraPos(Vec3::zero);
		SetMainCameraRot(Quat4::identity);
	}

	m_UndoStack.Reset();

	// Sorts the level entity first, then everything else alphabetically (Outliner order).
	std::sort(g_Editor->m_GameEntities.begin(), g_Editor->m_GameEntities.end(),
		[](const EditorEntity* a, const EditorEntity* b) -> bool {
			// Compares the flags together, since an early return per flag breaks strict weak ordering (UB) when both are level entities.
			const bool a_is_level = a->GetGameEntity()->GetComponentByType(LevelComponent::GetType());
			const bool b_is_level = b->GetGameEntity()->GetComponentByType(LevelComponent::GetType());
			if (a_is_level != b_is_level) {
				return a_is_level;
			}

			return a->GetGameEntity()->name().stl_str().compare(b->GetGameEntity()->name().stl_str()) < 0;
		});

	blk::log("	LoadMap finished.  Took %f seconds", g_GlobalTimer.TimeElapsedSeconds() - loadMapStartTime);
}

/// Editor::Update
void Editor::Update() {
	if (!m_bIsRunning) {
		return;
	}

	// Drains DeferAction() work here: Update() runs after g_renderer->render() returns (blaise/src/main.cpp),
	// so the frame's D3D12 command list is closed, unlike inside DrawImGuiPanels().
	if (!m_DeferredActions.empty()) {
		std::vector<std::function<void()>> actions;
		actions.swap(m_DeferredActions);
		for (const std::function<void()>& action : actions) {
			action();
		}
	}

	if (!m_bIsRunning) {
		return;
	}

	if (m_bGameUpdating && owns_keyboard() && editor_platform::key_down(editor_platform::Key::Backspace)) {
		StopGame();
	}

	static int num_frames = 0;
	static f32 start_time = g_GlobalTimer.TimeElapsedSeconds();
	static f32 last_time = start_time;
	static f32 FPS = 0;

	const f32 cur_time = g_GlobalTimer.TimeElapsedSeconds();
	const f32 dt = (cur_time - last_time);
	last_time = cur_time;

	num_frames++;
	if (num_frames > 100) {
		FPS = (f32)num_frames / dt;
		num_frames = 0;
		start_time = cur_time;
	}

	for (int i = 0; i < m_UpdateWidgets.size(); i++) {
		m_UpdateWidgets[i]->update(dt);
	}

	// Syncs entity and widget state to the renderer.
	for (int i = 0; i < m_GameEntities.size(); i++) {
		m_GameEntities[i]->render_sync();
	}

	for (int i = 0; i < m_UpdateWidgets.size(); i++) {
		m_UpdateWidgets[i]->render_sync();
	}

	// Commits entities queued by DeleteEntities(): disables their components and pushes one undoable delete.
	if (!m_RemovedEntities.empty()) {
		std::vector<UndoDeleteActor::DeletedActorInfo_t> deletedEntities;
		for (int i = 0; i < m_RemovedEntities.size(); i++) {
			blk::std_remove_swap(m_GameEntities, m_RemovedEntities[i]);

			UndoDeleteActor::DeletedActorInfo_t deletedActor;
			deletedActor.m_pEditorEntity = m_RemovedEntities[i];

			for (int j = 0; j < m_RemovedEntities[i]->GetGameEntity()->num_components(); j++) {
				deletedActor.m_bComponentEnabled.push_back(m_RemovedEntities[i]->GetGameEntity()->component(j)->IsEnabled());
				m_RemovedEntities[i]->GetGameEntity()->component(j)->Enable(false);
				m_RemovedEntities[i]->render_sync();
			}

			deletedEntities.push_back(deletedActor);
		}

		g_Editor->GetSelectedObjects().clear();
		g_Editor->m_UndoStack.Push(new UndoDeleteActor(deletedEntities));

		g_Editor->BroadcastEvent(widgetCBEntityDeselected());
		m_RemovedEntities.clear();
	}

//	g_pRenderer->render_sync();

	g_ResourceManager.render_sync();

//	g_pGame->GetParticleManager().render_sync();

//	g_pRenderer->SetReadyToRender();

	//m_pViewportPanel->GetCurrentWindow()->GetCamera().Update();

	if (editor_platform::window_has_focus()) {
		// Skips WASD/Ctrl/Shift polling while ImGui owns the keyboard so typing in a field can't drive the camera.
		// ImGui still gets the keys through the Win32 backend's WM_KEYDOWN/WM_CHAR handling.
		if (!ImGui::GetCurrentContext() || !ImGui::GetIO().WantCaptureKeyboard) {
			if (editor_platform::key_down(editor_platform::Key::W)) {
				m_WidgetInputObject.keys.push_back(widgetCBInputObject::keyType_t::WidgetInput_Forward);
			} else if (editor_platform::key_down(editor_platform::Key::S)) {
				m_WidgetInputObject.keys.push_back(widgetCBInputObject::keyType_t::WidgetInput_Back);
			}

			if (editor_platform::key_down(editor_platform::Key::A)) {
				m_WidgetInputObject.keys.push_back(widgetCBInputObject::keyType_t::WidgetInput_Left);
			} else if (editor_platform::key_down(editor_platform::Key::D)) {
				m_WidgetInputObject.keys.push_back(widgetCBInputObject::keyType_t::WidgetInput_Right);
			}

			if (editor_platform::key_down(editor_platform::Key::LeftCtrl)) {
				m_WidgetInputObject.keys.push_back(widgetCBInputObject::keyType_t::WidgetInput_Ctrl);
			}

			if (editor_platform::key_down(editor_platform::Key::LeftShift)) {
				m_WidgetInputObject.keys.push_back(widgetCBInputObject::keyType_t::WidgetInput_Shift);
			}
		}

		m_WidgetInputObject.dt = dt;
		if (!m_WidgetInputObject.keys.empty() || m_WidgetInputObject.mouseDeltaX != 0 || m_WidgetInputObject.mouseDeltaY != 0 ||
			m_WidgetInputObject.leftMouseButtonDown || m_WidgetInputObject.rightMouseButtonDown) {
			BroadcastEvent(m_WidgetInputObject);
		}

		if (m_WidgetInputObject.leftMouseButtonPressed) {
			m_WidgetInputObject.leftMouseButtonPressed = false;
			m_WidgetInputObject.leftMouseButtonDown = true;
		}

		if (m_WidgetInputObject.rightMouseButtonPressed) {
			m_WidgetInputObject.rightMouseButtonPressed = false;
			m_WidgetInputObject.rightMouseButtonDown = true;
		}
	} else {
		m_WidgetInputObject.leftMouseButtonPressed = false;
		m_WidgetInputObject.rightMouseButtonPressed = false;
		m_WidgetInputObject.leftMouseButtonDown = false;
		m_WidgetInputObject.rightMouseButtonDown = false;
	}

	m_WidgetInputObject.ClearKeys();
	m_WidgetInputObject.mouseDeltaX = 0;
	m_WidgetInputObject.mouseDeltaY = 0;
	m_WidgetInputObject.leftMouseButtonPressed = false;
	m_WidgetInputObject.rightMouseButtonPressed = false;

	const float DT = (std::min)(m_Timer.TimeElapsedSeconds(), 0.05f);
	m_Timer.Reset();

	for (int i = 0; i < m_GameEntities.size(); i++) {
		m_GameEntities[i]->Update(DT);
	}

	if (m_pGame && m_bGameUpdating) {
		m_pGame->HackEditorUpdate(DT, active_viewport()->GetEditorWindowCamera());
	}

	// Flags unsaved changes with '*' in the title bar.
	if (m_UndoIDAtLastSave != m_UndoStack.GetLastDirtyActionId()) {
		editor_platform::set_window_title("blk 1.0 - " + m_CurrentLevelFileName + "*");
	} else {
		editor_platform::set_window_title("blk 1.0  - " + m_CurrentLevelFileName);
	}
}

/// Editor::request_quit
void Editor::request_quit() {
	m_bIsRunning = false;
}

/// Editor::owns_keyboard
bool Editor::owns_keyboard() const {
	return editor_platform::window_has_focus() && (!ImGui::GetCurrentContext() || !ImGui::GetIO().WantCaptureKeyboard);
}

/// Editor::BroadcastEvent
void Editor::BroadcastEvent(const widgetCBObject& cbObject) {
	const std::vector<EditorPanel*>& receivers = m_EventReceivers[cbObject.widgetType];

	for (int i = 0; i < receivers.size(); i++) {
		receivers[i]->EventCB(&cbObject);
	}
}

/// Editor::DrawDockSpace
///
/// Bounds the dockspace below the toolbar, which DockSpaceOverViewport() would cover.
/// PassthruCentralNode keeps the empty centre transparent and routes its clicks to the 3D scene and gizmos.
void Editor::DrawDockSpace() {
	const ImGuiIO& io = ImGui::GetIO();

	// Offsets by GetFrameHeight() to match BeginMainMenuBar exactly, since a fixed constant leaves a seam.
	const float top = ImGui::GetFrameHeight() + (float)ToolbarHeight();
	const float bottom = io.DisplaySize.y;
	if (bottom <= top) {
		return;
	}

	// Pins a chrome-less, transparent host behind the panels it docks.
	ImGui::SetNextWindowPos(ImVec2(0.0f, top), ImGuiCond_Always);
	ImGui::SetNextWindowSize(ImVec2(io.DisplaySize.x, bottom - top), ImGuiCond_Always);

	constexpr ImGuiWindowFlags host_flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
		ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar |
		ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus |
		ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoDocking;

	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
	ImGui::Begin("##DockSpaceHost", nullptr, host_flags);
	ImGui::PopStyleVar(3);

	const ImGuiID dockspace_id = ImGui::GetID("##EditorDockSpace");

	// Seeds the default layout only when imgui.ini has no dock node, so user rearrangements persist.
	if (!ImGui::DockBuilderGetNode(dockspace_id)) {
		ImGui::DockBuilderRemoveNode(dockspace_id);
		ImGui::DockBuilderAddNode(dockspace_id, ImGuiDockNodeFlags_DockSpace);
		ImGui::DockBuilderSetNodeSize(dockspace_id, ImVec2(io.DisplaySize.x, bottom - top));

		// Constructs Unreal-style layout: full-width bottom shelf, right panel stack, transparent 3D centre.
		ImGuiID centre = dockspace_id;
		const ImGuiID bottom_dock = ImGui::DockBuilderSplitNode(centre, ImGuiDir_Down, 0.28f, nullptr, &centre);
		const ImGuiID right = ImGui::DockBuilderSplitNode(centre, ImGuiDir_Right, 0.22f, nullptr, &centre);
		ImGuiID right_bottom = right;
		const ImGuiID right_top = ImGui::DockBuilderSplitNode(right, ImGuiDir_Up, 0.45f, nullptr, &right_bottom);

		// Tabs Resources and Output Log into the bottom shelf, in dock order.
		ImGui::DockBuilderDockWindow("Resources", bottom_dock);
		ImGui::DockBuilderDockWindow("Output Log", bottom_dock);
		ImGui::DockBuilderDockWindow("Outliner", right_top);
		ImGui::DockBuilderDockWindow("Properties", right_bottom);
		ImGui::DockBuilderFinish(dockspace_id);

		// Forces Resources as the active tab over the last-submitted Output Log.
		// Defers until every panel is submitted, since focusing here is too early to take effect.
		m_bApplyDefaultDockFocus = true;
	}

	ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_PassthruCentralNode);
	ImGui::End();
}

/// Editor::DrawImGuiPanels
void Editor::DrawImGuiPanels() {
	DrawDockSpace();

	for (EditorPanel* const panel : m_ImGuiPanels) {
		panel->draw_imgui();
	}

	if (m_bApplyDefaultDockFocus) {
		m_bApplyDefaultDockFocus = false;
		ImGui::SetWindowFocus("Resources");
	}

	// Last, so a modal opens over every panel.
	editor_platform::draw_modals();
}

/// Editor::SetMainCameraPos
void Editor::SetMainCameraPos(const Vec3& newCamPos) {
	active_viewport()->GetEditorWindowCamera()->m_position = newCamPos;
}

/// Editor::GetMainCameraPos
Vec3 Editor::GetMainCameraPos() const {
	return active_viewport()->GetEditorWindowCamera()->m_position;
}

/// Editor::SetMainCameraRot
void Editor::SetMainCameraRot(const Quat4& new_rot) {
	active_viewport()->GetEditorWindowCamera()->m_rotation = new_rot;
	active_viewport()->GetEditorWindowCamera()->m_rotation_target = new_rot;
	active_viewport()->GetEditorWindowCamera()->m_rotation_current = new_rot;
}

/// Editor::DeselectEntities
void Editor::DeselectEntities() {
	for (int i = 0; i < m_GameEntities.size(); i++) {
		m_GameEntities[i]->SetIsSelected(false);
	}

	m_SelectedObjects.clear();
	g_Editor->BroadcastEvent(widgetCBEntityDeselected());
}

/// Editor::AddEntity
void Editor::AddEntity(EditorEntity* const pEditorEntity) {
	blk::error_check(!blk::std_contains(m_GameEntities, pEditorEntity), "Editor::AddEntity() - Called on an entity that has already been added.");

	m_GameEntities.push_back(pEditorEntity);
}

/// Editor::SelectEntities
void Editor::SelectEntities(std::vector<EditorEntity*>& entitiesToSelect, const bool bAppendToSelectedEntities) {
	if (!g_bEditorIsUndoingAnAction) {
		m_UndoStack.Push(new UndoSelectActor(m_SelectedObjects, entitiesToSelect));
	}

	if (!bAppendToSelectedEntities) {
		DeselectEntities();
	}

	for (int i = 0; i < entitiesToSelect.size(); i++) {
		entitiesToSelect[i]->SetIsSelected(true);
	}

	m_SelectedObjects.insert(m_SelectedObjects.end(), entitiesToSelect.begin(), entitiesToSelect.end());

	widgetCBEntitySelected entitySelectedCB;
	entitySelectedCB.entitiesSelected = entitiesToSelect;

	g_Editor->BroadcastEvent(entitySelectedCB);
}

#if defined(_WIN32)
/// Editor::WndProc
LRESULT CALLBACK Editor::WndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
	// Gates dispatch on m_bIsRunning, which is false during CreateWindowEx (panels don't exist yet)
	// and after request_quit(), so those messages fall through to DefWindowProcA.
	if (g_Editor && g_Editor->m_bIsRunning) {
		return g_Editor->handle_message(hwnd, msg, wparam, lparam);
	}

	return DefWindowProcA(hwnd, msg, wparam, lparam);
}

/// Editor::handle_message
///
/// Runs the renderer's ImGui_ImplWin32_WndProcHandler first so ImGui takes Win32 capture on button-down.
/// The capture latch below reads last frame's WantCaptureMouse (hover-based), which is the right test at press time.
LRESULT Editor::handle_message(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
	if (g_renderer && g_renderer->handle_platform_message(hwnd, msg, wparam, lparam)) {
		return 0;
	}

	const bool imgui_active = ImGui::GetCurrentContext();

	switch (msg) {
		case WM_CLOSE:
		case WM_DESTROY: {
			// Signals an exit. ~Editor does the actual teardown and DestroyWindow, so the in-flight
			// frame keeps its entities. Returning 0 from WM_CLOSE also stops DefWindowProc from
			// destroying the window.
			request_quit();
			return 0;
		}

		// Dispatches accelerators by hand because the ImGui menu bar has none.
		case WM_KEYDOWN: {
			// Suppresses shortcuts while an ImGui text field has keyboard focus.
			if (imgui_active && ImGui::GetIO().WantCaptureKeyboard) {
				return 0;
			}

			on_key_shortcut((GetKeyState(VK_CONTROL) & 0x8000) != 0, (wparam == VK_DELETE) ? k_key_delete : (int)wparam);
			return 0;
		}

		case WM_LBUTTONDOWN:
		case WM_RBUTTONDOWN: {
			// handle_platform_message() already took Win32 capture, so drags off-window still send WM_MOUSEMOVE.
			on_mouse_button(msg == WM_RBUTTONDOWN, true, GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam));
			return 0;
		}

		case WM_LBUTTONUP:
		case WM_RBUTTONUP: {
			on_mouse_button(msg == WM_RBUTTONUP, false, 0, 0);
			return 0;
		}

		// Both branches require a held button, so plain moves fall through untouched.
		case WM_MOUSEMOVE: {
			int newMouseX = GET_X_LPARAM(lparam);
			int newMouseY = GET_Y_LPARAM(lparam);

			if (m_WidgetInputObject.leftMouseButtonDown && !m_bLeftMouseButtonCapturedByImGui) {
				m_WidgetInputObject.mouseDeltaX = newMouseX - m_WidgetInputObject.mouseX;
				m_WidgetInputObject.mouseDeltaY = newMouseY - m_WidgetInputObject.mouseY;

				m_WidgetInputObject.mouseX = newMouseX;
				m_WidgetInputObject.mouseY = newMouseY;
			} else if (m_WidgetInputObject.rightMouseButtonDown && !m_bRightMouseButtonCapturedByImGui) {
				m_bRightMouseButtonDragged = true;
				m_WidgetInputObject.mouseDeltaX = newMouseX - m_WidgetInputObject.mouseX;
				m_WidgetInputObject.mouseDeltaY = newMouseY - m_WidgetInputObject.mouseY;

				m_WidgetInputObject.mouseX = newMouseX;
				m_WidgetInputObject.mouseY = newMouseY;

				// Wraps the cursor at a 10px client border so a right-drag camera look never stalls at the edge.
				RECT rc = {};
				GetClientRect(m_hwnd, &rc);

				const int leftBorder = rc.left + 10;
				const int rightBorder = rc.right - 10;
				const int topBorder = rc.top + 10;
				const int bottomBorder = rc.bottom - 10;

				bool updateCursor = false;

				if (newMouseX < leftBorder) {
					updateCursor = true;
					newMouseX = rightBorder;
				} else if (newMouseX > rightBorder) {
					updateCursor = true;
					newMouseX = leftBorder;
				}

				if (newMouseY < topBorder) {
					updateCursor = true;
					newMouseY = bottomBorder;
				} else if (newMouseY > bottomBorder) {
					updateCursor = true;
					newMouseY = topBorder;
				}

				if (updateCursor) {
					POINT point = {};
					point.x = (LONG)newMouseX;
					point.y = (LONG)newMouseY;

					ClientToScreen(m_hwnd, &point);
					SetCursorPos(point.x, point.y);
				}

				m_WidgetInputObject.mouseX = newMouseX;
				m_WidgetInputObject.mouseY = newMouseY;
			}
			return 0;
		}
	}

	return DefWindowProcA(hwnd, msg, wparam, lparam);
}

#endif // defined(_WIN32)

/// Editor::on_mouse_button
void Editor::on_mouse_button(const bool is_right, const bool is_down, const int x, const int y) {
	const bool imgui_active = ImGui::GetCurrentContext();

	if (is_down) {
		m_bRightMouseButtonDragged = false;

		// Latches ImGui capture for the whole gesture (see m_bLeftMouseButtonCapturedByImGui).
		if (imgui_active) {
			const bool captured = ImGui::GetIO().WantCaptureMouse;
			if (!is_right) {
				m_bLeftMouseButtonCapturedByImGui = captured;
			} else {
				m_bRightMouseButtonCapturedByImGui = captured;
			}
		}

		m_WidgetInputObject.mouseX = x;
		m_WidgetInputObject.mouseY = y;

		if (is_right && !m_bRightMouseButtonCapturedByImGui) {
			m_WidgetInputObject.rightMouseButtonPressed = true;
		} else if (!is_right && !m_bLeftMouseButtonCapturedByImGui) {
			m_WidgetInputObject.leftMouseButtonPressed = true;
		}
		return;
	}

	// Separates scene from panel right-clicks by the button-down capture latch, because the viewport spans
	// the whole window and a bounds test can't. rightMouseButtonDown limits this to right-clicks.
	if (m_WidgetInputObject.rightMouseButtonDown && !m_bRightMouseButtonDragged &&
		!m_bRightMouseButtonCapturedByImGui) {
		RightClickOnViewport();
	}

	m_WidgetInputObject.leftMouseButtonDown = false;
	m_WidgetInputObject.leftMouseButtonPressed = false;
	m_WidgetInputObject.rightMouseButtonDown = false;
	m_WidgetInputObject.rightMouseButtonPressed = false;
	m_bLeftMouseButtonCapturedByImGui = false;
	m_bRightMouseButtonCapturedByImGui = false;
}

/// Editor::on_mouse_drag_by
///
/// The relative twin of the WM_MOUSEMOVE drag branch, for hosts that report movement
/// rather than a position. Accumulates, since a browser can deliver several move events
/// per frame; Update() zeroes the delta once it has been broadcast.
void Editor::on_mouse_drag_by(const int delta_x, const int delta_y) {
	const bool left_dragging = m_WidgetInputObject.leftMouseButtonDown && !m_bLeftMouseButtonCapturedByImGui;
	const bool right_dragging = m_WidgetInputObject.rightMouseButtonDown && !m_bRightMouseButtonCapturedByImGui;
	if (!left_dragging && !right_dragging) {
		return;
	}

	if (!left_dragging && right_dragging) {
		m_bRightMouseButtonDragged = true;
	}

	m_WidgetInputObject.mouseDeltaX += delta_x;
	m_WidgetInputObject.mouseDeltaY += delta_y;
}

/// Editor::on_key_shortcut
void Editor::on_key_shortcut(const bool ctrl_down, const int key) {
	if (ctrl_down) {
		switch (key) {
			case 'N': NewLevel(); return;
			case 'O': OpenLevel(); return;
			case 'S': SaveLevel(); return;
			case 'Z': Undo(); return;
			case 'Y': Redo(); return;
			case 'P': PlayGameFromHere(); return;
			case 'Q': StopGame(); return;
			default: break;
		}
	} else if (key == k_key_delete) {
		DeleteEntitiesCB();
	}
}

/// Editor::Close
void Editor::Close() {
	g_Editor->request_quit();
}

/// Editor::CreateGameEntity
void Editor::CreateGameEntity() {
	const Camera* const editorCamera = g_Editor->active_viewport()->GetEditorWindowCamera();

	if (!editorCamera) {
		return;
	}

	EditorEntity* const pEditorEntity = new EditorEntity();
	const Vec3 entityLocation = editorCamera->m_position + (editorCamera->m_rotation.to_mat4()[2] * 4.0f).ToVec3();
	pEditorEntity->set_position(entityLocation);

	g_Editor->m_GameEntities.push_back(pEditorEntity);
}

/// Editor::add_component
void Editor::add_component(const TypeInfoClass* const typeInfoClass) {
	if (!typeInfoClass || !g_Editor) {
		return;
	}

	const std::vector<EditorEntity*>& selectedObjects = g_Editor->GetSelectedObjects();

	if (!selectedObjects.empty()) {
		GameComponent* const newComponent = (GameComponent*)typeInfoClass->ConstructInstance();  // ENTITY HACK

		selectedObjects[0]->GetGameEntity()->add_component(newComponent);
		newComponent->Enable(true);

		widgetCBObject widgetCB;
		widgetCB.widgetType = WidgetCB_ComponentCreated;
		g_Editor->BroadcastEvent(widgetCB);
	}
}

/// Editor::TranslationButtonCB
void Editor::TranslationButtonCB() {
	widgetCBObject cbObject;
	cbObject.widgetType = WidgetCB_TranslationButtonPressed;
	g_Editor->BroadcastEvent(cbObject);
}

/// Editor::RotationButtonCB
void Editor::RotationButtonCB() {
	widgetCBObject cbObject;
	cbObject.widgetType = WidgetCB_RotationButtonPressed;
	g_Editor->BroadcastEvent(cbObject);
}

/// Editor::ScaleButtonCB
void Editor::ScaleButtonCB() {
	widgetCBObject cbObject;
	cbObject.widgetType = WidgetCB_ScaleButtonPressed;
	g_Editor->BroadcastEvent(cbObject);
}

/// XFormEntities
void XFormEntities(const Manipulator& manipulator, const Vec4 xForm) {
	const std::vector<EditorEntity*>& entityList = g_Editor->GetGameEntities();
	for (int i = 0; i < entityList.size(); i++) {
		if (entityList[i]->IsSelected()) {
			if (manipulator.GetMode() == Manipulator::Translate) {
				entityList[i]->set_position(entityList[i]->position() + xForm.ToVec3() * xForm.w);
			} else if (manipulator.GetMode() == Manipulator::Rotate) {
				Quat4 rot(xForm.ToVec3(), xForm.a);
				rot = (entityList[i]->rotation() * rot).normalize_safe();
				entityList[i]->set_rotation(rot);
			} else if (manipulator.GetMode() == Manipulator::Scale) {
				entityList[i]->set_scale(entityList[i]->scale() + xForm.ToVec3() * xForm.w);
			}
		}
	}
}

/// Editor::XPlusAdjustButtonCB
void Editor::XPlusAdjustButtonCB() {
	XFormEntities(g_Editor->m_pViewportPanel->m_Manipulator, Vec4(1.0f, 0.0f, 0.0f, g_Editor->m_XFormAmount));
}

/// Editor::XNegAdjustButtonCB
void Editor::XNegAdjustButtonCB() {
	XFormEntities(g_Editor->m_pViewportPanel->m_Manipulator, Vec4(-1.0f, 0.0f, 0.0f, g_Editor->m_XFormAmount));
}

/// Editor::YPlusAdjustButtonCB
void Editor::YPlusAdjustButtonCB() {
	XFormEntities(g_Editor->m_pViewportPanel->m_Manipulator, Vec4(0.0f, 1.0f, 0.0f, g_Editor->m_XFormAmount));
}

/// Editor::YNegAdjustButtonCB
void Editor::YNegAdjustButtonCB() {
	XFormEntities(g_Editor->m_pViewportPanel->m_Manipulator, Vec4(0.0f, -1.0f, 0.0f, g_Editor->m_XFormAmount));
}

/// Editor::ZPlusAdjustButtonCB
void Editor::ZPlusAdjustButtonCB() {
	XFormEntities(g_Editor->m_pViewportPanel->m_Manipulator, Vec4(0.0f, 0.0f, 1.0f, g_Editor->m_XFormAmount));
}

/// Editor::ZNegAdjustButtonCB
void Editor::ZNegAdjustButtonCB() {
	XFormEntities(g_Editor->m_pViewportPanel->m_Manipulator, Vec4(0.0f, 0.0f, -1.0f, g_Editor->m_XFormAmount));
}

/// Editor::SetCamSpeedIndex
void Editor::SetCamSpeedIndex(const int idx) {
	if (idx < 0 || idx >= (int)g_NumEditorCamSpeedBindings) {
		blk::warn("Editor::SetCamSpeedIndex() - Invalid index %d.", idx);
		return;
	}

	m_CamSpeedIdx = idx;
	m_pViewportPanel->SetCameraSpeedMultiplier(g_EditorCamSpeedBindings[idx].m_SpeedMultiplier);
}

/// Editor::NumCamSpeedBindings
int Editor::NumCamSpeedBindings() {
	return (int)g_NumEditorCamSpeedBindings;
}

/// Editor::CamSpeedBindingName
const char* Editor::CamSpeedBindingName(const int idx) {
	return g_EditorCamSpeedBindings[idx].m_DisplayName.c_str();
}

bool g_bBillboardsEnabled = true;

/// Editor::ToggleIconsCB
void Editor::ToggleIconsCB() {
	g_bBillboardsEnabled = !g_bBillboardsEnabled;
}

/// Editor::NewLevel
void Editor::NewLevel() {
	editor_platform::confirm("New Level", "Creating a new level.  Any unsaved changes will be lost.  Are you sure?", []() {
		g_Editor->UnloadMap();

		g_Editor->m_GameEntities.clear();
		g_Editor->DeselectEntities();
	});
}

/// Editor::OpenLevel
void Editor::OpenLevel() {
	const editor_platform::FileFilter filter = { "Level Files", { "blklevel", "kblevel" } };

	editor_platform::pick_open_file("Open Level", filter, "./assets/levels", [](const std::string& fileName) {
		editor_platform::confirm("Open Level", "You have unsaved changes.  Are you sure you want to open a new level?", [fileName]() {
//			g_pRenderer->WaitForRenderingToComplete();

			g_Editor->DeselectEntities();

			for (int i = 0; i < g_Editor->m_GameEntities.size(); i++) {
				delete g_Editor->m_GameEntities[i];
			}
			g_Editor->m_GameEntities.clear();

			std::string fileNameStr = fileName;
			const size_t pos = fileNameStr.find_last_of("\\/");
			if (pos != std::string::npos) {
				fileNameStr = fileNameStr.substr(pos + 1, fileNameStr.length() - pos);
			}
			g_Editor->LoadMap(fileNameStr.c_str());
		});
	});
}

/// Editor::SaveLevel_Internal
void Editor::SaveLevel_Internal(const std::string& fileNameStr, const bool bForceSave) {
	if (!bForceSave) {
		std::ifstream f(fileNameStr.c_str());
		const bool bExists = f.good();
		f.close();

		if (bExists) {
			editor_platform::confirm("Save Level", "File already exists.  Do you wish to overwrite it?", [this, fileNameStr]() {
				WriteLevelFile(fileNameStr);
			});
			return;
		}
	}

	WriteLevelFile(fileNameStr);
}

/// Editor::WriteLevelFile
void Editor::WriteLevelFile(const std::string& fileNameStr) {
	File outFile;
	outFile.Open(fileNameStr.c_str(), File::FT_Write);

	{
		const Camera* const pCam = active_viewport()->GetEditorWindowCamera();

		EditorLevelSettingsComponent* const pLevelSettingsComp = new EditorLevelSettingsComponent();
		pLevelSettingsComp->m_CameraPosition = pCam->m_position;
		pLevelSettingsComp->m_CameraRotation = pCam->m_rotation;

		GameEntity* const pLevelSettingsEnt = new GameEntity();
		pLevelSettingsEnt->add_component(pLevelSettingsComp);

		outFile.WriteGameEntity(pLevelSettingsEnt);
		delete pLevelSettingsEnt;
	}

	for (int i = 0; i < g_Editor->m_GameEntities.size(); i++) {
		GameEntity* const game_entity = g_Editor->m_GameEntities[i]->GetGameEntity();

		// Skips the loaded settings entity, since the block above writes a fresh one.
		if (game_entity->GetComponentByType(EditorLevelSettingsComponent::GetType())) {
			continue;
		}
		outFile.WriteGameEntity(game_entity);
	}

	outFile.Close();

	m_UndoIDAtLastSave = m_UndoStack.GetLastDirtyActionId();
}

/// Editor::SaveLevelAs
void Editor::SaveLevelAs() {
	const editor_platform::FileFilter filter = { "Level Files", { "blklevel" } };

	editor_platform::pick_save_file("Save Level", filter, "./assets/levels", [](const std::string& picked) {
		std::string fileName = picked;
		if (fileName.empty()) {
			return;
		}

		const std::string fileExt = GetFileExtension(fileName);
		if (!blk::is_level_extension(fileExt)) {
			fileName += ".blklevel";
		}
		g_Editor->SaveLevel_Internal(fileName, false);
	});
}

/// Editor::SaveLevel
void Editor::SaveLevel() {
	if (g_Editor->m_CurrentLevelFileName.empty()) {
		return;
	}

	g_Editor->SaveLevel_Internal(g_Editor->m_CurrentLevelFileName, true);
}

/// Editor::Undo
void Editor::Undo() {
	g_Editor->m_UndoStack.Undo();
}

/// Editor::Redo
void Editor::Redo() {
	g_Editor->m_UndoStack.Redo();
}

/// Editor::PlayGameFromHere
void Editor::PlayGameFromHere() {
	if (!g_Editor || !g_Editor->m_pGame || g_Editor->m_pGame->IsPlaying()) {
		return;
	}

	g_Editor->m_bGameUpdating = true;
}

/// Editor::StopGame
void Editor::StopGame() {
	if (!g_Editor || !g_Editor->m_pGame) {
		return;
	}

	g_Editor->m_bGameUpdating = false;
	editor_platform::show_cursor(true);

	g_pGame->HackEditorShutdown();
}

/// Editor::DeleteEntities
void Editor::DeleteEntities(std::vector<EditorEntity*>& editorEntityList) {
	std::vector<UndoDeleteActor::DeletedActorInfo_t> deletedEntities;

	for (int i = 0; i < editorEntityList.size(); i++) {
		g_Editor->m_RemovedEntities.push_back(editorEntityList[i]);
		blk::std_remove_swap(m_GameEntities, editorEntityList[i]);
	}
}

/// Editor::DeleteEntitiesCB
void Editor::DeleteEntitiesCB() {
	std::vector<EditorEntity*> SelectedObjects = g_Editor->GetSelectedObjects();
	g_Editor->DeleteEntities(SelectedObjects);
}

/// Editor::OutputCB
void Editor::OutputCB(const OutputMessageType_t messageType, const char* const output) {
	g_OutputLog.push_back({ messageType, std::string(output) });

	if (messageType == OutputMessageType_t::Message_Assert) {
		editor_platform::notify(editor_platform::MessageKind::Error, "Assert", output);
	}
}

/// Editor::RightClickOnViewport
///
/// Raises a flag for WorkbenchPanel::DrawViewportContextMenu() to open the menu in-frame.
/// OpenPopup() needs an active ImGui frame, and both WndProc and DeferAction() run outside one.
void Editor::RightClickOnViewport() {
	m_bWantOpenViewportContextMenu = true;
}

/// Editor::GetCurrentlySelectedPrefab
const Prefab* Editor::GetCurrentlySelectedPrefab() const {
	return m_pResourcesPanel->GetSelectedPrefab();
}

/// Editor::ReplaceCurrentlySelectedPrefab
void Editor::ReplaceCurrentlySelectedPrefab() {
	if (g_Editor->m_SelectedObjects.size() != 1) {
		return;
	}

	Prefab* const pPrefab = g_Editor->m_pResourcesPanel->GetSelectedPrefab();
	if (!pPrefab) {
		return;
	}

	std::vector<GameEntity*> GameEntityList;
	for (int i = 0; i < g_Editor->m_SelectedObjects.size(); i++) {
		GameEntityList.push_back(g_Editor->m_SelectedObjects[i]->GetGameEntity());
	}

	g_ResourceManager.update_prefab(pPrefab, GameEntityList);
	g_Editor->m_pResourcesPanel->MarkPrefabDirty(pPrefab);
	//	g_ResourceManager.DumpPackageInfo();
		//g_ResourceManager.SavePackages();
}

/// Editor::DuplicateEntity
void Editor::DuplicateEntity() {
	const auto& selectedObjects = g_Editor->GetSelectedObjects();
	if (selectedObjects.empty()) {
		return;
	}

	GameEntity* const pSrcEntity = selectedObjects[0]->GetGameEntity();
	GameEntity* const pDstEntity = new GameEntity(pSrcEntity, false);

	EditorEntity* const pEditorEntity = new EditorEntity(pDstEntity);
	pEditorEntity->set_position(pSrcEntity->position());
	g_Editor->m_GameEntities.push_back(pEditorEntity);
}

/// Editor::AddEntityAsPrefab
///
/// Raises a flag that WorkbenchPanel::DrawAddPrefabPopup() consumes to open the popup inside draw_imgui().
/// The context menu reaches this via DeferAction(), so the popup opens next frame, clear of the menu's own popup.
void Editor::AddEntityAsPrefab() {
	g_Editor->m_bWantOpenAddPrefabPopup = true;
}

/// Editor::AddEntityAsPrefab_Internal
void Editor::AddEntityAsPrefab_Internal(const std::string& PackageName, const std::string& FolderName, const std::string& PrefabName) {
	if (m_SelectedObjects.size() != 1) {
		return;
	}

	if (PackageName.empty() || FolderName.empty() || PrefabName.empty()) {
		editor_platform::notify(editor_platform::MessageKind::Warning, "Add Prefab", "Incomplete fields.  Prefab was not created");
		return;
	}

	GameEntity* const source = m_SelectedObjects[0]->GetGameEntity();

	const auto finish = [this, PackageName, FolderName, PrefabName](Prefab* const prefab) {
		m_pResourcesPanel->AddPrefab(prefab, PackageName, FolderName, PrefabName);
		//g_ResourceManager.DumpPackageInfo();
		//g_ResourceManager.SavePackages();

		editor_platform::notify(editor_platform::MessageKind::Info, "Add Prefab", "Prefab added successfully");
	};

	Prefab* prefab = nullptr;
	if (g_ResourceManager.add_prefab(source, PackageName, FolderName, PrefabName, false, &prefab)) {
		finish(prefab);
		return;
	}

	editor_platform::confirm("Add Prefab", "Prefab with that name and path already exist.  Overwrite?", [this, source, PackageName, FolderName, PrefabName, finish]() {
		// The answer may arrive frames later, so the selection has to still be the
		// entity the prefab was asked for.
		if (m_SelectedObjects.size() != 1 || m_SelectedObjects[0]->GetGameEntity() != source) {
			return;
		}

		Prefab* overwritten = nullptr;
		if (!g_ResourceManager.add_prefab(source, PackageName, FolderName, PrefabName, true, &overwritten)) {
			editor_platform::notify(editor_platform::MessageKind::Error, "Add Prefab", "Unable to add prefab");
			return;
		}
		finish(overwritten);
	});
}

/// Editor::InsertSelectedPrefabIntoScene
void Editor::InsertSelectedPrefabIntoScene() {
	const Prefab* const prefabToCreate = g_Editor->m_pResourcesPanel->GetSelectedPrefab();
	if (!prefabToCreate) {
		return;
	}

	const Camera* const editorCamera = g_Editor->active_viewport()->GetEditorWindowCamera();
	if (!editorCamera) {
		return;
	}

	const Vec3 entityLocation = editorCamera->m_position + (editorCamera->m_rotation.to_mat4()[2] * 4.0f).ToVec3();

	for (int i = 0; i < prefabToCreate->NumGameEntities(); i++) {
		GameEntity* const pNewEntity = new GameEntity(prefabToCreate->m_GameEntities[i], false);
		EditorEntity* const pEditorEntity = new EditorEntity(pNewEntity);
		pEditorEntity->set_position(entityLocation);
		g_Editor->m_GameEntities.push_back(pEditorEntity);
	}
}