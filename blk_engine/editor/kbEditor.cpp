/// kbEditor.cpp
///
/// 2016 blk

#include <iomanip>
#include <sstream>
#include "blk_core.h"
#include <commdlg.h>
#include <windowsx.h>
#include "blk_containers.h"
#include "Matrix.h"
#include "Quaternion.h"
#include "game.h"
#include "editor_panel.h"
#include "kbManipulator.h"
#include "file.h"
#include "kbMainTab.h"
#include "resources_panel.h"
#include "type_info.h"
#include "outliner_panel.h"
#include "properties_panel.h"
#include "workbench_panel.h"
#include "kbEditor.h"
#include "kbEditorEntity.h"
#include "renderer.h"
#include "imgui.h"

// Phase 3, Milestone 8: the editor window's Win32 class name. Matches the
// class that owns the window, and the one other window class in the codebase
// (blaise's "kbEngine", main.cpp) -- blk_ is the namespace/library prefix
// here, not an identifier prefix.
static const char* const g_EditorWindowClassName = "kbEditor";

kbEditor* g_Editor = nullptr;
bool g_bEditorIsUndoingAnAction = false;

// Phase 3, Milestone 4 Step 2: read directly by WorkbenchPanel::DrawOutputLog()
// via an extern declaration in workbench_panel.cpp -- replaces the old
// Fl_Text_Buffer* g_OutputBuffer/g_StyleBuffer pair.
std::vector<LogEntry> g_OutputLog;

// Editor camera speed
struct EditorCamSpeedBind {
	EditorCamSpeedBind(const kbString& displayName, const float multiplier) :
		m_DisplayName(displayName),
		m_SpeedMultiplier(multiplier) { }

	kbString m_DisplayName;
	float m_SpeedMultiplier;
};

const static EditorCamSpeedBind g_EditorCamSpeedBindings[] = {
	EditorCamSpeedBind(kbString("0.05x"), 0.05f),
	EditorCamSpeedBind(kbString("0.25x"), 0.25f),
	EditorCamSpeedBind(kbString("1x"), 1.0f),
	EditorCamSpeedBind(kbString("5x"), 5.0f),
	EditorCamSpeedBind(kbString("15x"), 15.0f),
	EditorCamSpeedBind(kbString("35x"), 35.0f),
	EditorCamSpeedBind(kbString("50x"), 50.0f)
};
const static size_t g_NumEditorCamSpeedBindings = sizeof(g_EditorCamSpeedBindings) / sizeof(EditorCamSpeedBind);


/// kbEditor
kbEditor::kbEditor() {

	m_bGameUpdating = false;
	const float editorInitStartTime = g_GlobalTimer.TimeElapsedSeconds();

	m_UndoIDAtLastSave = UINT64_MAX;
	m_CurrentLevelFileName = "Untitled";

	g_Editor = this;

	m_pGame = nullptr;

	const int Screen_Width = GetSystemMetrics(SM_CXFULLSCREEN);
	const int Screen_Height = GetSystemMetrics(SM_CYFULLSCREEN);
	const int Menu_Bar_Height = MenuBarHeight();
	const int Menu_Buttons_Height = ToolbarHeight();
	const int Left_Panel = 200;
	const int Bottom_Panel_Height = BottomPanelHeight();
	const int Right_Panel = 300;

	g_OutputCB = kbEditor::OutputCB;

	// Phase 3, Milestone 8: replaces the Fl_Window base ctor and, with it, the
	// child kbEditorWindow -- one window now, so ImGui's HWND and the message
	// source coincide. Deliberately not resizable
	// (WS_THICKFRAME/WS_MAXIMIZEBOX stripped), matching an Fl_Window that never
	// called resizable(): resizing would need swapchain-resize handling that
	// doesn't exist, since the swapchain is a fixed
	// g_screen_width x g_screen_height with DXGI_SCALING_STRETCH.
	//
	// Placement is the whole *window* inset 12px into the work area, rather
	// than FLTK's behavior of placing the client there. Fl_X::make() ran the
	// requested x/y through fake_X_wm(), which subtracts the decoration, so
	// the editor's title bar always sat ~19px above the top of the screen and
	// was clipped -- reproducing that faithfully is not worth keeping.
	// SPI_GETWORKAREA (rather than SM_CXFULLSCREEN/SM_CYFULLSCREEN, which
	// describe a *maximized* window's client) is what guarantees the frame,
	// title bar included, lands fully on screen and clear of the taskbar.
	//
	// A-suffixed calls throughout: blk_engine builds MultiByte (blaise builds
	// Unicode), and an ANSI window is also the path imgui_impl_win32's WM_CHAR
	// handling explicitly supports via its MultiByteToWideChar branch.
	{
		WNDCLASSEXA window_class = {};
		window_class.cbSize = sizeof(window_class);
		window_class.style = CS_HREDRAW | CS_VREDRAW;
		window_class.lpfnWndProc = kbEditor::WndProc;
		window_class.hInstance = GetModuleHandleA(nullptr);
		window_class.hCursor = LoadCursor(nullptr, IDC_ARROW);
		window_class.lpszClassName = g_EditorWindowClassName;
		RegisterClassExA(&window_class);

		const DWORD window_style = WS_OVERLAPPEDWINDOW & ~WS_THICKFRAME & ~WS_MAXIMIZEBOX;

		RECT work_area = {};
		if (SystemParametersInfoA(SPI_GETWORKAREA, 0, &work_area, 0) == FALSE) {
			work_area = { 0, 0, Screen_Width, Screen_Height };
		}

		const int Window_Margin = 12;
		m_hwnd = CreateWindowExA(
			0, g_EditorWindowClassName, "blk 1.0", window_style,
			work_area.left + Window_Margin, work_area.top + Window_Margin,
			(work_area.right - work_area.left) - Window_Margin * 2,
			(work_area.bottom - work_area.top) - Window_Margin * 2,
			nullptr, nullptr, GetModuleHandleA(nullptr), nullptr);

		blk::error_check(m_hwnd != nullptr, "kbEditor::kbEditor() - Failed to create the editor window.");
	}

	// Phase 3, Milestone 4: menu bar and toolbar are now drawn by
	// WorkbenchPanel (ImGui) -- see its construction below, alongside
	// Outliner/Properties.

	// Phase 3, Milestone 7: the viewport fills the whole window. It used to be
	// inset by Left_Panel/Right_Panel/toolbar/output-log margins to leave room
	// for the FLTK sidebars, menu bar and log -- all of which are gone, so
	// those margins were just exposing the bare FLTK window background. The
	// ImGui panels float over the scene rather than sitting beside it.
	m_pMainTab = new kbMainTab(0, 0, Screen_Width, Screen_Height);
	RegisterImGuiPanel(m_pMainTab);

	// Phase 3, Milestone 5: replaces the FLTK ResourceTab with an ImGui
	// equivalent -- full behavioral parity, not additive.
	m_pResourcesPanel = new ResourcesPanel(0, Menu_Bar_Height + Menu_Buttons_Height, Left_Panel, Screen_Height - Menu_Bar_Height - Menu_Bar_Height - Bottom_Panel_Height);
	RegisterImGuiPanel(m_pResourcesPanel);

	// Phase 3, Milestone 2: first ImGui panel bridged into the live editor.
	m_pOutlinerPanel = new OutlinerPanel(0, 0, 260, 400);
	RegisterImGuiPanel(m_pOutlinerPanel);

	// Phase 3, Milestones 3 and 6: reflection-driven property grid. Milestone 6
	// closed the last gaps against the FLTK "Entity Info" tab (kbPropertiesTab)
	// and deleted it, so this is now the only property grid.
	m_pPropertiesPanel = new PropertiesPanel(300, 20, 340, 480);
	RegisterImGuiPanel(m_pPropertiesPanel);

	// Phase 3, Milestone 4: replaces the FLTK menu bar + toolbar row with
	// ImGui equivalents -- full behavioral parity, not additive.
	m_pWorkbenchPanel = new WorkbenchPanel(0, 0, Screen_Width, Screen_Height);
	RegisterImGuiPanel(m_pWorkbenchPanel);

	ShowWindow(m_hwnd, SW_SHOW);
	UpdateWindow(m_hwnd);

	// setup the renderer
	/*if (g_pRenderer == nullptr) {
		g_pRenderer = new kbRenderer_DX11();
		g_pRenderer->Init(main_viewport_hwnd(), 1920, 1080);
		g_pRenderer->EnableDebugBillboards(true);
	}*/

	m_pResourcesPanel->PostRendererInit();

	m_bIsRunning = true;

	m_Timer.Reset();

	// reserve textures
	//g_pRenderer->LoadTexture("../../blk_engine/assets/Textures/Editor/EntityIcon.jpg", 1);
	//g_pRenderer->LoadTexture("../../blk_engine/assets/Textures/Editor/directionalLightIcon.jpg", 2);

	SetWindowTextA(m_hwnd, "kbEditor");

	// Load Editor Settings
	kbEditorGlobalSettingsComponent* pEditorGlobalComponent = nullptr;

	kbFile levelEditorFile;
	if (levelEditorFile.Open("./assets/editorSettings.txt", kbFile::FT_Read)) {
		GameEntity* const gameEntity = levelEditorFile.ReadGameEntity();
		pEditorGlobalComponent = (kbEditorGlobalSettingsComponent*)gameEntity->GetComponentByType(kbEditorGlobalSettingsComponent::GetType());
		levelEditorFile.Close();
	}

	if (pEditorGlobalComponent == nullptr) {
		SetCamSpeedIndex(0);
	} else {
		if (pEditorGlobalComponent->m_CameraSpeedIdx >= 0 && pEditorGlobalComponent->m_CameraSpeedIdx < g_NumEditorCamSpeedBindings) {
			SetCamSpeedIndex(pEditorGlobalComponent->m_CameraSpeedIdx);
		} else {
			SetCamSpeedIndex(0);
		}
	}

	blk::log("Editor init time took %f seconds", g_GlobalTimer.TimeElapsedSeconds() - editorInitStartTime);
}

/// ~kbEditor
kbEditor::~kbEditor() {
	shut_down();

	// Phase 3, Milestone 8: the window outlives shut_down() deliberately --
	// shut_down() is what the main loop watches to exit (IsRunning()), and it
	// also runs on the File/Quit path where the window should stay up until
	// teardown actually reaches here. FLTK behaved the same way: kbEditor
	// installed its own callback(), which replaced Fl_Window's default
	// hide-on-close.
	if (m_hwnd != nullptr) {
		DestroyWindow(m_hwnd);
		m_hwnd = nullptr;
	}
}

/// kbEditor::UnloadMap
void kbEditor::UnloadMap() {
	// Remove old entities
	DeselectEntities();

	for (int i = 0; i < g_Editor->m_GameEntities.size(); i++) {
		delete m_GameEntities[i];
	}
	m_GameEntities.clear();

	m_CurrentLevelFileName = "Untitled";
	m_UndoIDAtLastSave = UINT64_MAX;
	m_UndoStack.Reset();
}

/// kbEditor::LoadMap
void kbEditor::LoadMap(const std::string& InMapName) {
	blk::log("LoadMap() called for map %s", InMapName.c_str());
	const float loadMapStartTime = g_GlobalTimer.TimeElapsedSeconds();

	UnloadMap();

	const kbEditorLevelSettingsComponent* pLevelSettings = nullptr;

	// Load map
	if (InMapName.empty() == false) {
		m_CurrentLevelFileName = InMapName;

		TCHAR NPath[MAX_PATH];

		GetCurrentDirectory(MAX_PATH, NPath);

		WIN32_FIND_DATA fdFile;
		HANDLE hFind = nullptr;

		std::string LevelPath = NPath;
		LevelPath += "/Assets/Levels/";
		std::string curLevelFolder = "";

		if (m_CurrentLevelFileName.find(".") == std::string::npos) {
			m_CurrentLevelFileName += ".kbLevel";
		}

		hFind = FindFirstFile((LevelPath + "*").c_str(), &fdFile);
		BOOL nextFileFound = (hFind != INVALID_HANDLE_VALUE);
		do {
			std::string nextFileName = LevelPath + curLevelFolder + m_CurrentLevelFileName;

			kbFile inFile;
			if (inFile.Open(nextFileName.c_str(), kbFile::FT_Read)) {
				m_CurrentLevelFileName = nextFileName;

				GameEntity* pGameEntity = inFile.ReadGameEntity();

				while (pGameEntity != nullptr) {

					if (pLevelSettings == nullptr) {
						pLevelSettings = (kbEditorLevelSettingsComponent*)pGameEntity->GetComponentByType(kbEditorLevelSettingsComponent::GetType());
						if (pLevelSettings != nullptr) {
							// Track it (so the normal delete-all-on-unload lifecycle
							// owns it -- previously this GameEntity was never stored
							// anywhere and leaked, leaving its GUID registered in
							// g_GUIDToEntityMap forever and colliding with a fresh
							// entity of the same GUID the next time this same level
							// loaded), but hidden -- it's level metadata, not a
							// placeable entity, so it shouldn't appear in the
							// Outliner/ResourcesPanel entity lists.
							kbEditorEntity* const levelSettingsEntity = new kbEditorEntity(pGameEntity);
							levelSettingsEntity->SetHidden(true);
							g_Editor->m_GameEntities.push_back(levelSettingsEntity);

							pGameEntity = inFile.ReadGameEntity();
							continue;
						}
					}

					kbEditorEntity* const newEditorEntity = new kbEditorEntity(pGameEntity);
					g_Editor->m_GameEntities.push_back(newEditorEntity);
					pGameEntity = inFile.ReadGameEntity();
				}
				inFile.Close();

				const std::string windowText = "kbEditor - " + InMapName;
				SetWindowTextA(m_hwnd, windowText.c_str());

				break;
			}

			if (nextFileFound == FALSE) {
				break;
			}

			do {
				if ((fdFile.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0 && strcmp(fdFile.cFileName, ".") != 0 && strcmp(fdFile.cFileName, "..") != 0) {
					curLevelFolder = "/";
					curLevelFolder += fdFile.cFileName;
					curLevelFolder += "/";
					break;
				}

			} while (nextFileFound = FindNextFile(hFind, &fdFile) != FALSE);

			if (nextFileFound != 0) {
				nextFileFound = FindNextFile(hFind, &fdFile);
			}
		} while (true);
	}

	if (pLevelSettings != nullptr) {
		SetMainCameraPos(pLevelSettings->m_CameraPosition);
		SetMainCameraRot(pLevelSettings->m_CameraRotation);
	} else {
		SetMainCameraPos(Vec3::zero);
		SetMainCameraRot(Quat4::identity);
	}

	m_UndoStack.Reset();


	std::sort(g_Editor->m_GameEntities.begin(), g_Editor->m_GameEntities.end(),
			  [](const kbEditorEntity* a, const kbEditorEntity* b) -> bool {

				  if (a->GetGameEntity()->GetComponentByType(kbLevelComponent::GetType())) {
					  return true;
				  } else if (b->GetGameEntity()->GetComponentByType(kbLevelComponent::GetType())) {
					  return false;
				  }

				  return a->GetGameEntity()->name().stl_str().compare(b->GetGameEntity()->name().stl_str()) < 0;
	});


	blk::log("	LoadMap finished.  Took %f seconds", g_GlobalTimer.TimeElapsedSeconds() - loadMapStartTime);
}

/// kbEditor::Update
void kbEditor::Update() {
	if (m_bIsRunning == false) {
		return;
	}

	// Phase 3, Milestone 4: drain actions queued via DeferAction() (see its
	// declaration in kbEditor.h) -- Update() runs right after g_renderer->render()
	// returns each loop iteration (blaise/src/main.cpp), so the frame's D3D12
	// command list is guaranteed closed here, unlike inside DrawImGuiPanels().
	if (m_DeferredActions.empty() == false) {
		std::vector<std::function<void()>> actions;
		actions.swap(m_DeferredActions);
		for (const std::function<void()>& action : actions) {
			action();
		}
	}

	if (m_bIsRunning == false) {
		return;
	}

	if (m_bGameUpdating && GetAsyncKeyState(VK_BACK)) {
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

/*	{//if ( g_ShowFPS.GetBool() ) {
		std::string fpsString = "FPS: ";
		std::stringstream stream;
		stream << std::fixed << std::setprecision(2) << FPS;
		fpsString += stream.str();
//		g_pRenderer->DrawDebugText(fpsString, 0.85f, 0, g_DebugTextSize, g_DebugTextSize, kbColor::green);
	}*/

	{
		// Inside kbEditor::Update()
		static bool bSpeedKeyWasDown = false;
		bool bSpeedKeyDown = (GetAsyncKeyState('V') & 0x8000);

		if (bSpeedKeyDown && !bSpeedKeyWasDown) {
			const int nextValue = (m_CamSpeedIdx + 1) % NumCamSpeedBindings();
			SetCamSpeedIndex(nextValue);
		}
		bSpeedKeyWasDown = bSpeedKeyDown;
	}

	for (int i = 0; i < m_UpdateWidgets.size(); i++) {
		m_UpdateWidgets[i]->update(dt);
	}

	// Update editor entities and components
	for (int i = 0; i < m_GameEntities.size(); i++) {
		m_GameEntities[i]->render_sync();
	}

	for (int i = 0; i < m_UpdateWidgets.size(); i++) {
		m_UpdateWidgets[i]->render_sync();
	}

	// Remove any undeleted actors
	if (m_RemovedEntities.size() > 0) {
		std::vector<kbUndoDeleteActor::DeletedActorInfo_t> deletedEntities;
		for (int i = 0; i < m_RemovedEntities.size(); i++) {
			blk::std_remove_swap(m_GameEntities, m_RemovedEntities[i]);

			kbUndoDeleteActor::DeletedActorInfo_t deletedActor;
			deletedActor.m_pEditorEntity = m_RemovedEntities[i];

			for (int j = 0; j < m_RemovedEntities[i]->GetGameEntity()->num_components(); j++) {
				deletedActor.m_bComponentEnabled.push_back(m_RemovedEntities[i]->GetGameEntity()->component(j)->IsEnabled());
				m_RemovedEntities[i]->GetGameEntity()->component(j)->Enable(false);
				m_RemovedEntities[i]->render_sync();
			}

			deletedEntities.push_back(deletedActor);
		}

		g_Editor->GetSelectedObjects().clear();
		g_Editor->m_UndoStack.Push(new kbUndoDeleteActor(deletedEntities));

		g_Editor->BroadcastEvent(widgetCBEntityDeselected());
		m_RemovedEntities.clear();
	}

//	g_pRenderer->render_sync();

	g_ResourceManager.render_sync();

//	g_pGame->GetParticleManager().render_sync();

//	g_pRenderer->SetReadyToRender();

	//m_pMainTab->GetCurrentWindow()->GetCamera().Update();

	if (GetFocus() == m_hwnd) {

		/*if ( GetAsyncKeyState( 'C' ) ) {

			kbEditorEntity* pMasterBridge = nullptr;
			RenderComponent* pMasterComp = nullptr;
			static kbString skMasterBridge( "Bridge - Master" );

			for ( int i = 0; i < m_GameEntities.size(); i++ ) {
				kbEditorEntity* const pEditorEntity = m_GameEntities[i];
				if ( pEditorEntity == nullptr ) {
					continue;
				}

				GameEntity* const pGameEnt = pEditorEntity->GetGameEntity();
				if ( pGameEnt == nullptr ) {
					continue;
				}

				if ( pGameEnt->GetName() == skMasterBridge ) {
					pMasterBridge = m_GameEntities[i];
					pMasterComp = pMasterBridge->GetGameEntity()->GetComponent<RenderComponent>();
					break;
				}
			}

			if ( pMasterBridge != nullptr ) {
				for ( int i = 0; i < m_GameEntities.size(); i++ ) {

					kbEditorEntity* const pEditorEntity = m_GameEntities[i];
					if ( pEditorEntity == nullptr ) {
						continue;
					}

					GameEntity* const pGameEnt = pEditorEntity->GetGameEntity();
					if ( pGameEnt == nullptr ) {
						continue;
					}
					if ( pGameEnt->GetName().stl_str().find( "Bridge" ) != std::string::npos ) {
						RenderComponent* pTargetComp = pGameEnt->GetComponent<RenderComponent>();
						if ( pTargetComp != nullptr ) {
							pTargetComp->CopyMaterials( pMasterComp->Materials() );
							continue;
						}
					}
				}
			}
		}*/
		// input
		//
		// Phase 3, Milestone 3: gate WASD/Ctrl/Shift polling behind
		// WantCaptureKeyboard now that real text fields exist (PropertiesPanel's
		// Name/int/float editing) -- this is raw GetAsyncKeyState polling, so
		// without this guard typing "a"/"d"/etc. into a field would also drive
		// the camera at the same time. Keys still reach ImGui itself through
		// the Win32 backend's WM_KEYDOWN/WM_CHAR handling, so no functionality
		// is lost while a field has keyboard focus.
		if (g_imgui_enabled == false || ImGui::GetCurrentContext() == nullptr || ImGui::GetIO().WantCaptureKeyboard == false) {
			if (GetAsyncKeyState('W')) {
				m_WidgetInputObject.keys.push_back(widgetCBInputObject::keyType_t::WidgetInput_Forward);
			} else if (GetAsyncKeyState('S')) {
				m_WidgetInputObject.keys.push_back(widgetCBInputObject::keyType_t::WidgetInput_Back);
			}

			if (GetAsyncKeyState('A')) {
				m_WidgetInputObject.keys.push_back(widgetCBInputObject::keyType_t::WidgetInput_Left);
			} else if (GetAsyncKeyState('D')) {
				m_WidgetInputObject.keys.push_back(widgetCBInputObject::keyType_t::WidgetInput_Right);
			}

			if (GetAsyncKeyState(VK_LCONTROL)) {
				m_WidgetInputObject.keys.push_back(widgetCBInputObject::keyType_t::WidgetInput_Ctrl);
			}

			if (GetAsyncKeyState(VK_LSHIFT)) {
				m_WidgetInputObject.keys.push_back(widgetCBInputObject::keyType_t::WidgetInput_Shift);
			}
		}

		m_WidgetInputObject.dt = dt;
		if (m_WidgetInputObject.keys.size() > 0 || m_WidgetInputObject.mouseDeltaX != 0 || m_WidgetInputObject.mouseDeltaY != 0 ||
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

	float DT = m_Timer.TimeElapsedSeconds();
	m_Timer.Reset();

	if (DT > 0.05f) {
		DT = 0.05f;
	}
	// Update editor entities and components
	for (int i = 0; i < m_GameEntities.size(); i++) {
		m_GameEntities[i]->Update(DT);
	}

	if (m_pGame != nullptr && m_bGameUpdating) {
		m_pGame->HackEditorUpdate(DT, m_pMainTab->GetEditorWindowCamera());
	}

	// Update title bar dirty status
	if (m_UndoIDAtLastSave != m_UndoStack.GetLastDirtyActionId()) {
		SetWindowTextA(m_hwnd, ("blk 1.0 - " + m_CurrentLevelFileName + "*").c_str());
	} else {
		SetWindowTextA(m_hwnd, ("blk 1.0  - " + m_CurrentLevelFileName).c_str());
	}
}

// kbEditor::shut_down
void kbEditor::shut_down() {
	// Save Editor Settings
	kbFile outFile;
	outFile.Open("./assets/editorSettings.txt", kbFile::FT_Write);

	GameEntity levelInfoEnt;
	kbEditorGlobalSettingsComponent* const pLevelInfo = new kbEditorGlobalSettingsComponent();
	pLevelInfo->m_CameraSpeedIdx = m_CamSpeedIdx;
	levelInfoEnt.add_component(pLevelInfo);
	outFile.WriteGameEntity(&levelInfoEnt);
	outFile.Close();

	if (m_bIsRunning == false) {
		return;
	}

	for (int i = 0; i < m_GameEntities.size(); i++) {
		delete m_GameEntities[i];
	}
	m_GameEntities.clear();

	g_ResourceManager.shut_down();

	m_bIsRunning = false;
}

/// kbEditor::ShutDown
void kbEditor::BroadcastEvent(const widgetCBObject& cbObject) {

	std::vector< EditorPanel* >& receivers = m_EventReceivers[cbObject.widgetType];

	for (int i = 0; i < receivers.size(); i++) {
		receivers[i]->EventCB(&cbObject);
	}
}

/// kbEditor::DrawImGuiPanels
void kbEditor::DrawImGuiPanels() {
	for (EditorPanel* const panel : m_ImGuiPanels) {
		panel->draw_imgui();
	}
}

/// kbEditor::SetMainCameraPos
void kbEditor::SetMainCameraPos(const Vec3& newCamPos) {
	m_pMainTab->GetEditorWindowCamera()->m_position = newCamPos;
}

/// kbEditor::GetMainCameraPos
Vec3 kbEditor::GetMainCameraPos() const {
	return m_pMainTab->GetEditorWindowCamera()->m_position;
}

/// kbEditor::SetMainCameraRot
void kbEditor::SetMainCameraRot(const Quat4& new_rot) {
	m_pMainTab->GetEditorWindowCamera()->m_rotation = new_rot;
	m_pMainTab->GetEditorWindowCamera()->m_rotation_target = new_rot;
	m_pMainTab->GetEditorWindowCamera()->m_rotation_current = new_rot;
}

/// kbEditor::GetMainCameraRot
Quat4 kbEditor::GetMainCameraRot() const {
	return m_pMainTab->GetEditorWindowCamera()->m_rotation;
}

/// kbEditor::DeselectEntities
void kbEditor::DeselectEntities() {

	for (int i = 0; i < m_GameEntities.size(); i++) {
		m_GameEntities[i]->SetIsSelected(false);
	}

	m_SelectedObjects.clear();
	g_Editor->BroadcastEvent(widgetCBEntityDeselected());
}

/// kbEditor::AddEntity
void kbEditor::AddEntity(kbEditorEntity* const pEditorEntity) {
	blk::error_check(blk::std_contains(m_GameEntities, pEditorEntity) == false, "kbEditor::AddEntity() - Called on an entity that has already been added.");

	m_GameEntities.push_back(pEditorEntity);
}

/// kbEditor::SelectEntities
void kbEditor::SelectEntities(std::vector< kbEditorEntity* >& entitiesToSelect, const bool bAppendToSelectedEntities) {
	if (g_bEditorIsUndoingAnAction == false) {
		m_UndoStack.Push(new kbUndoSelectActor(m_SelectedObjects, entitiesToSelect));
	}

	if (bAppendToSelectedEntities == false) {
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

/// kbEditor::WndProc
///
/// Phase 3, Milestone 8: replaces FLTK's window proc. m_bIsRunning is the
/// guard: it stays false until the constructor finishes and goes false again
/// the instant shut_down() runs, so the messages Windows delivers during
/// CreateWindowEx (before m_pMainTab and the panels exist) and everything
/// after teardown fall through to DefWindowProc instead of reaching
/// half-constructed state.
LRESULT CALLBACK kbEditor::WndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
	if (g_Editor != nullptr && g_Editor->m_bIsRunning) {
		return g_Editor->handle_message(hwnd, msg, wparam, lparam);
	}

	return DefWindowProcA(hwnd, msg, wparam, lparam);
}

/// kbEditor::handle_message
///
/// Phase 3, Milestone 8: replaces kbEditor::handle(). ImGui was initialized
/// against this very window (ImGui_ImplWin32_Init, renderer_dx12.cpp), so
/// ImGui_ImplWin32_WndProcHandler -- reached below through the renderer's
/// handle_platform_message() -- now does all the mouse/keyboard/focus
/// translation that Milestone 3 had to hand-roll off FLTK's event system.
/// It has to run first: the ImGui-capture latching reads WantCaptureMouse
/// straight after the button event is queued, which is the same
/// value-from-last-frame press-time latch Milestone 2 established and
/// verified.
LRESULT kbEditor::handle_message(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
	if (g_renderer != nullptr && g_renderer->handle_platform_message(hwnd, msg, wparam, lparam)) {
		return 0;
	}

	const bool imgui_active = (g_imgui_enabled && ImGui::GetCurrentContext() != nullptr);

	switch (msg) {
		case WM_CLOSE:
		case WM_DESTROY: {
			// Replaces the Fl_Widget callback kbEditor installed on itself.
			// The main loop (blaise/src/main.cpp) watches IsRunning() and
			// exits on the next iteration.
			shut_down();
			return 0;
		}

		// Phase 3, Milestone 4: explicit shortcut dispatch replacing the FLTK
		// Fl_Menu_Bar's implicit shortcut-matching, now that the menu bar
		// itself is gone (WorkbenchPanel draws an ImGui menu bar, which has no
		// built-in accelerator-key handling of its own).
		case WM_KEYDOWN: {
			// Same guard the old FL_KEYDOWN path had, so a shortcut can't fire
			// while an ImGui text field has keyboard focus.
			if (imgui_active && ImGui::GetIO().WantCaptureKeyboard) {
				return 0;
			}

			// Virtual-key codes for letters are the *uppercase* ASCII values,
			// unlike Fl::event_key()'s lowercase keysyms.
			if (GetKeyState(VK_CONTROL) & 0x8000) {
				switch (wparam) {
					case 'N': NewLevel(); return 0;
					case 'O': OpenLevel(); return 0;
					case 'S': SaveLevel(); return 0;
					case 'Z': Undo(); return 0;
					case 'Y': Redo(); return 0;
					case 'P': PlayGameFromHere(); return 0;
					case 'Q': StopGame(); return 0;
					default: break;
				}
			} else if (wparam == VK_DELETE) {
				DeleteEntitiesCB();
				return 0;
			}
			return 0;
		}

		case WM_LBUTTONDOWN:
		case WM_RBUTTONDOWN: {
			const int newMouseX = GET_X_LPARAM(lparam);
			const int newMouseY = GET_Y_LPARAM(lparam);

			m_bRightMouseButtonDragged = false;

			// Phase 3, Milestone 2: latch capture for the whole gesture (see
			// m_bLeftMouseButtonCapturedByImGui's declaration in kbEditor.h).
			// The button event itself was already handed to ImGui by
			// handle_platform_message() above -- which is also what takes the
			// Win32 mouse capture, so a drag that leaves the window keeps
			// delivering WM_MOUSEMOVE here exactly as FL_DRAG used to.
			if (imgui_active) {
				const bool captured = ImGui::GetIO().WantCaptureMouse;
				if (msg == WM_LBUTTONDOWN) m_bLeftMouseButtonCapturedByImGui = captured;
				if (msg == WM_RBUTTONDOWN) m_bRightMouseButtonCapturedByImGui = captured;
			}

			m_WidgetInputObject.mouseX = newMouseX;
			m_WidgetInputObject.mouseY = newMouseY;

			if (msg == WM_RBUTTONDOWN && !m_bRightMouseButtonCapturedByImGui) {
				m_WidgetInputObject.rightMouseButtonPressed = true;
			} else if (msg == WM_LBUTTONDOWN && !m_bLeftMouseButtonCapturedByImGui) {
				m_WidgetInputObject.leftMouseButtonPressed = true;
			}
			return 0;
		}

		case WM_LBUTTONUP:
		case WM_RBUTTONUP: {
			const int newMouseX = GET_X_LPARAM(lparam);
			const int newMouseY = GET_Y_LPARAM(lparam);

			// Phase 3, Milestone 7: the viewport now spans the whole window, so
			// the bounds test alone no longer distinguishes "right-clicked the
			// scene" from "right-clicked a panel" -- it used to, only because
			// the panels sat in margins outside the viewport rect. The ImGui
			// capture latched at button-down is what separates them now.
			// Reached on either button's release, matching FL_RELEASE, but the
			// rightMouseButtonDown test means only a right-click gets here.
			if (m_WidgetInputObject.rightMouseButtonDown && m_bRightMouseButtonDragged == false &&
				m_bRightMouseButtonCapturedByImGui == false) {
				if (m_pMainTab->IsPointWithinBounds(newMouseX, newMouseY)) {
					RightClickOnMainTab();
				}
			}

			m_WidgetInputObject.leftMouseButtonDown = false;
			m_WidgetInputObject.leftMouseButtonPressed = false;
			m_WidgetInputObject.rightMouseButtonDown = false;
			m_WidgetInputObject.rightMouseButtonPressed = false;
			m_bLeftMouseButtonCapturedByImGui = false;
			m_bRightMouseButtonCapturedByImGui = false;
			return 0;
		}

		// FLTK raised FL_DRAG only while a button was held, whereas WM_MOUSEMOVE
		// fires for every move -- but both branches here were already gated on a
		// button being down, so a plain move falls through untouched exactly as
		// FL_MOVE did.
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

				RECT rc;
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


/// kbEditor::Close
void kbEditor::Close() {
	g_Editor->shut_down();
}

/// kbEditor::CreateGameEntity
void kbEditor::CreateGameEntity() {

	const kbCamera* const editorCamera = g_Editor->m_pMainTab->GetEditorWindowCamera();

	if (editorCamera == nullptr) {
		return;
	}

	kbEditorEntity* const pEditorEntity = new kbEditorEntity();
	const Vec3 entityLocation = editorCamera->m_position + (editorCamera->m_rotation.to_mat4()[2] * 4.0f).ToVec3();
	pEditorEntity->set_position(entityLocation);

	g_Editor->m_GameEntities.push_back(pEditorEntity);
}

/// kbEditor::AddComponent
void kbEditor::add_component(const kbTypeInfoClass* const typeInfoClass) {
	if (typeInfoClass == nullptr || g_Editor == nullptr)
		return;

	std::vector<kbEditorEntity*>& selectedObjects = g_Editor->GetSelectedObjects();

	if (selectedObjects.size() > 0) {
		kbGameComponent* const newComponent = (kbGameComponent*)typeInfoClass->ConstructInstance();		// ENTITY HACK

		const_cast<GameEntity*>(selectedObjects[0]->GetGameEntity())->add_component(newComponent);
		newComponent->Enable(true);

		widgetCBObject widgetCB;
		widgetCB.widgetType = WidgetCB_ComponentCreated;
		g_Editor->BroadcastEvent(widgetCB);
	}
}

/// kbEditor::TranslationButtonCB
void kbEditor::TranslationButtonCB() {
	widgetCBObject cbObject;
	cbObject.widgetType = WidgetCB_TranslationButtonPressed;
	g_Editor->BroadcastEvent(cbObject);
}

/// kbEditor::RotationButtonCB
void kbEditor::RotationButtonCB() {
	widgetCBObject cbObject;
	cbObject.widgetType = WidgetCB_RotationButtonPressed;
	g_Editor->BroadcastEvent(cbObject);
}

/// kbEditor::ScaleButtonCB
void kbEditor::ScaleButtonCB() {
	widgetCBObject cbObject;
	cbObject.widgetType = WidgetCB_ScaleButtonPressed;
	g_Editor->BroadcastEvent(cbObject);
}

/// XFormEntities
void XFormEntities(const kbManipulator& manipulator, const Vec4 xForm) {
	std::vector<kbEditorEntity*>& entityList = g_Editor->GetGameEntities();
	for (int i = 0; i < entityList.size(); i++) {
		if (entityList[i]->IsSelected()) {

			if (manipulator.GetMode() == kbManipulator::Translate) {
				entityList[i]->set_position(entityList[i]->position() + xForm.ToVec3() * xForm.w);
			} else if (manipulator.GetMode() == kbManipulator::Rotate) {
				Quat4 rot(xForm.ToVec3(), xForm.a);
				rot = (entityList[i]->rotation() * rot).normalize_safe();
				entityList[i]->set_rotation(rot);
			} else if (manipulator.GetMode() == kbManipulator::Scale) {
				entityList[i]->set_scale(entityList[i]->scale() + xForm.ToVec3() * xForm.w);
			}
		}
	}
}

/// kbEditor::XPlusAdjustButtonCB
void kbEditor::XPlusAdjustButtonCB() {

	XFormEntities(g_Editor->m_pMainTab->m_Manipulator, Vec4(1.0f, 0.0f, 0.0f, g_Editor->m_XFormAmount));
}

/// kbEditor::XNegAdjustButtonCB
void kbEditor::XNegAdjustButtonCB() {

	XFormEntities(g_Editor->m_pMainTab->m_Manipulator, Vec4(-1.0f, 0.0f, 0.0f, g_Editor->m_XFormAmount));
}

/// kbEditor::YPlusAdjustButtonCB
void kbEditor::YPlusAdjustButtonCB() {

	XFormEntities(g_Editor->m_pMainTab->m_Manipulator, Vec4(0.0f, 1.0f, 0.0f, g_Editor->m_XFormAmount));
}

/// kbEditor::YNegAdjustButtonCB
void kbEditor::YNegAdjustButtonCB() {

	XFormEntities(g_Editor->m_pMainTab->m_Manipulator, Vec4(0.0f, -1.0f, 0.0f, g_Editor->m_XFormAmount));
}

/// kbEditor::ZPlusAdjustButtonCB
void kbEditor::ZPlusAdjustButtonCB() {

	XFormEntities(g_Editor->m_pMainTab->m_Manipulator, Vec4(0.0f, 0.0f, 1.0f, g_Editor->m_XFormAmount));
}

/// kbEditor::ZNegAdjustButtonCB
void kbEditor::ZNegAdjustButtonCB() {

	XFormEntities(g_Editor->m_pMainTab->m_Manipulator, Vec4(0.0f, 0.0f, -1.0f, g_Editor->m_XFormAmount));
}

/// kbEditor::SetCamSpeedIndex
void kbEditor::SetCamSpeedIndex(int idx) {
	if (idx < 0 || idx >= (int)g_NumEditorCamSpeedBindings) {
		blk::warn("kbEditor::SetCamSpeedIndex() - Invalid index %d.", idx);
		return;
	}

	m_CamSpeedIdx = idx;
	m_pMainTab->SetCameraSpeedMultiplier(g_EditorCamSpeedBindings[idx].m_SpeedMultiplier);
}

/// kbEditor::NumCamSpeedBindings
int kbEditor::NumCamSpeedBindings() {
	return (int)g_NumEditorCamSpeedBindings;
}

/// kbEditor::CamSpeedBindingName
const char* kbEditor::CamSpeedBindingName(int idx) {
	return g_EditorCamSpeedBindings[idx].m_DisplayName.c_str();
}

/// kbEditor::ToggleIconsCB
bool g_bBillboardsEnabled = true;
void kbEditor::ToggleIconsCB() {

	g_bBillboardsEnabled = !g_bBillboardsEnabled;
}

/// kbEditor::NewLevel
void kbEditor::NewLevel() {
	const int areYouSure = MessageBoxA(g_Editor->m_hwnd, "Creating a new level.  Any unsaved changes will be lost.  Are you sure?", "New Level", MB_YESNO | MB_ICONQUESTION);
	if (areYouSure != IDYES) {
		return;
	}

	g_Editor->UnloadMap();

	g_Editor->m_GameEntities.clear();
	g_Editor->DeselectEntities();
}

/// kbEditor::OpenLevel
void kbEditor::OpenLevel() {
	char fileNameBuf[MAX_PATH] = {};

	OPENFILENAMEA ofn = {};
	ofn.lStructSize = sizeof(ofn);
	ofn.hwndOwner = g_Editor->m_hwnd;
	ofn.lpstrFile = fileNameBuf;
	ofn.nMaxFile = MAX_PATH;
	ofn.lpstrFilter = "Level Files (*.kbLevel)\0*.kbLevel\0";
	ofn.lpstrInitialDir = "./assets/levels";
	ofn.lpstrTitle = "Open Level";
	// OFN_NOCHANGEDIR: GetOpenFileNameA/GetSaveFileNameA change the process's
	// current directory to match the last-browsed folder by default -- every
	// relative path in the app (asset loading, level saves, imgui.ini) is
	// resolved against that CWD, so an unnoticed change here silently breaks
	// them for the rest of the session.
	ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;

	if (!GetOpenFileNameA(&ofn)) {
		return;
	}

	const char* const fileName = fileNameBuf;

	const int areYouSure = MessageBoxA(g_Editor->m_hwnd, "You have unsaved changes.  Are you sure you want to open a new level?", "Open Level", MB_YESNO | MB_ICONQUESTION);
	if (areYouSure != IDYES) {
		return;
	}

//	g_pRenderer->WaitForRenderingToComplete();

	// Remove old entities
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
}

/// kbEditor::SaveLevel_Internal
void kbEditor::SaveLevel_Internal(const std::string& fileNameStr, const bool bForceSave) {
	if (bForceSave == false) {
		std::ifstream f(fileNameStr.c_str());
		if (f.good()) {
			const int overWriteIt = MessageBoxA(g_Editor->m_hwnd, "File already exists.  Do you wish to overwrite it?", "Save Level", MB_YESNO | MB_ICONQUESTION);
			if (overWriteIt != IDYES) {
				f.close();
				return;
			}
		}
		f.close();
	}

	kbFile outFile;
	outFile.Open(fileNameStr.c_str(), kbFile::FT_Write);

	{
		const kbCamera* const pCam = m_pMainTab->GetEditorWindowCamera();

		kbEditorLevelSettingsComponent* const pLevelSettingsComp = new kbEditorLevelSettingsComponent();
		pLevelSettingsComp->m_CameraPosition = pCam->m_position;
		pLevelSettingsComp->m_CameraRotation = pCam->m_rotation;

		GameEntity* const pLevelSettingsEnt = new GameEntity();
		pLevelSettingsEnt->add_component(pLevelSettingsComp);

		outFile.WriteGameEntity(pLevelSettingsEnt);
		delete pLevelSettingsEnt;
	}

	for (int i = 0; i < g_Editor->m_GameEntities.size(); i++) {
		outFile.WriteGameEntity(g_Editor->m_GameEntities[i]->GetGameEntity());
	}

	outFile.Close();

	m_UndoIDAtLastSave = m_UndoStack.GetLastDirtyActionId();
}

/// kbEditor::SaveLevelAs
void kbEditor::SaveLevelAs() {

	char fileNameBuf[MAX_PATH] = {};

	OPENFILENAMEA ofn = {};
	ofn.lStructSize = sizeof(ofn);
	ofn.hwndOwner = g_Editor->m_hwnd;
	ofn.lpstrFile = fileNameBuf;
	ofn.nMaxFile = MAX_PATH;
	ofn.lpstrFilter = "Level Files (*.kbLevel)\0*.kbLevel\0";
	ofn.lpstrInitialDir = "./assets/levels";
	ofn.lpstrTitle = "Save Level";
	ofn.Flags = OFN_NOCHANGEDIR;

	if (!GetSaveFileNameA(&ofn)) {
		return;
	}

	std::string fileName = fileNameBuf;
	if (fileName.empty()) {
		return;
	}

	const std::string fileExt = GetFileExtension(fileName);
	if (fileExt != "kbLevel" && fileExt != "kblevel") {
		fileName += ".kblevel";
	}
	g_Editor->SaveLevel_Internal(fileName, false);
}

/// kbEditor::SaveLevel
void kbEditor::SaveLevel() {

	if (g_Editor->m_CurrentLevelFileName.empty()) {
		return;
	}

	g_Editor->SaveLevel_Internal(g_Editor->m_CurrentLevelFileName, true);
}

/// kbEditor::Undo
void kbEditor::Undo() {
	g_Editor->m_UndoStack.Undo();
}

/// kbEditor::Redo
void kbEditor::Redo() {
	g_Editor->m_UndoStack.Redo();
}

/// kbEditor::PlayGameFromHere
void kbEditor::PlayGameFromHere() {
	if (g_Editor == nullptr || g_Editor->m_pGame == nullptr || g_Editor->m_pGame->IsPlaying()) {
		return;
	}

	g_Editor->m_bGameUpdating = true;
	// The commented-out D3D11-era block that used to sit here spun up a
	// separate game window (m_pGameWindow / GetGameWindow()) and broadcast
	// WidgetCB_GameStarted. Milestone 7 deleted that window -- the running
	// game renders into the editor viewport via HackEditorInit above -- and
	// Milestone 8 deleted the Fl_Window base its show()/Fl::check() calls
	// needed, so it was referencing three separate things that no longer
	// exist. Removed rather than left to rot.
}

/// kbEditor::StopGame
void kbEditor::StopGame() {
	if (g_Editor == nullptr || g_Editor->m_pGame == nullptr) {
		return;
	}

	g_Editor->m_bGameUpdating = false;
	ShowCursor(true);

	g_pGame->HackEditorShutdown();
	// Its PlayGameFromHere counterpart's commented-out block went the same way,
	// for the same reason -- it deleted m_pGameWindow, which no longer exists.
}

/// kbEditor::DeleteEntities
void kbEditor::DeleteEntities(std::vector<kbEditorEntity*>& editorEntityList) {
	std::vector<kbUndoDeleteActor::DeletedActorInfo_t> deletedEntities;

	for (int i = 0; i < editorEntityList.size(); i++) {
		g_Editor->m_RemovedEntities.push_back(editorEntityList[i]);
		blk::std_remove_swap(m_GameEntities, editorEntityList[i]);
	}

}

/// kbEditor::DeleteEntitiesCB
void kbEditor::DeleteEntitiesCB() {
	std::vector<kbEditorEntity*> SelectedObjects = g_Editor->GetSelectedObjects();
	g_Editor->DeleteEntities(SelectedObjects);
}

/// kbEditor::OutputCB
void kbEditor::OutputCB(kbOutputMessageType_t messageType, const char* output) {
	g_OutputLog.push_back({ messageType, std::string(output) });

	if (messageType == kbOutputMessageType_t::Message_Assert) {
		MessageBoxA(nullptr, output, "Assert", MB_OK | MB_ICONERROR);
	}
}

/// kbEditor::RightClickOnMainTab
///
/// Phase 3, Milestone 8: was an Fl_Menu_Item[] built and run inline here via
/// ::popup() -- the last FLTK widget left in the editor, and an easy one to
/// miss since it was never a member widget and never appeared in a
/// constructor. This runs from the WndProc during Windows' message dispatch,
/// outside any active ImGui frame, and ImGui::OpenPopup() needs a valid
/// window/ID-stack context it doesn't have there, so all this does is raise a
/// flag; WorkbenchPanel::DrawViewportContextMenu() opens and draws the menu
/// from inside draw_imgui(). Same split (and same reason) as
/// AddEntityAsPrefab/DrawAddPrefabPopup -- and note DeferAction() would not
/// work here either, since its queue also drains outside the frame.
void kbEditor::RightClickOnMainTab() {
	m_bWantOpenViewportContextMenu = true;
}

/// kbEditor::GetCurrentlySelectedPrefab
const kbPrefab* kbEditor::GetCurrentlySelectedPrefab() const {
	return m_pResourcesPanel->GetSelectedPrefab();
}

/// kbEditor::ReplaceCurrentlySelectedPrefab
void kbEditor::ReplaceCurrentlySelectedPrefab() {
	if (g_Editor->m_SelectedObjects.size() != 1) {
		return;
	}

	kbPrefab* const pPrefab = g_Editor->m_pResourcesPanel->GetSelectedPrefab();
	if (pPrefab == nullptr) {
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

/// kbEditor::DuplicateEntity
void kbEditor::DuplicateEntity() {
	auto& selectedObjects = g_Editor->GetSelectedObjects();
	if (selectedObjects.size() == 0) {
		return;
	}

	GameEntity* const pSrcEntity = selectedObjects[0]->GetGameEntity();
	GameEntity* const pDstEntity = new GameEntity(pSrcEntity, false);

	kbEditorEntity* const pEditorEntity = new kbEditorEntity(pDstEntity);
	pEditorEntity->set_position(pSrcEntity->position());
	g_Editor->m_GameEntities.push_back(pEditorEntity);
}

/// kbEditor::AddEntityAsPrefab
///
/// Phase 3, Milestone 4 Step 3: the old kbDialogBox::Run() blocking loop is
/// gone. This just sets a flag; WorkbenchPanel::DrawAddPrefabPopup() calls
/// OpenPopup() itself from inside draw_imgui() when it sees the flag set.
/// Milestone 8: its one call site is now the viewport context menu's
/// "Create New Prefab" item, which reaches this through DeferAction() -- so
/// the flag is raised after the frame ends, and the popup opens on the next
/// one, still clear of DrawViewportContextMenu()'s own popup.
void kbEditor::AddEntityAsPrefab() {
	g_Editor->m_bWantOpenAddPrefabPopup = true;
}

/// kbEditor::AddEntityAsPrefab_Internal
void kbEditor::AddEntityAsPrefab_Internal(const std::string& PackageName, const std::string& FolderName, const std::string& PrefabName) {

	if (m_SelectedObjects.size() != 1) {
		return;
	}

	if (PackageName.empty() || FolderName.empty() || PrefabName.empty()) {
		MessageBoxA(g_Editor->m_hwnd, "Incomplete fields.  Prefab was not created", "Add Prefab", MB_OK | MB_ICONWARNING);
		return;
	}

	kbPrefab* prefab;
	if (g_ResourceManager.add_prefab(m_SelectedObjects[0]->GetGameEntity(), PackageName, FolderName, PrefabName, false, &prefab) == false) {
		const int shouldOverwrite = MessageBoxA(g_Editor->m_hwnd, "Prefab with that name and path already exist.  Overwrite?", "Add Prefab", MB_YESNO | MB_ICONQUESTION);

		if (shouldOverwrite != IDYES)
			return;

		if (g_ResourceManager.add_prefab(m_SelectedObjects[0]->GetGameEntity(), PackageName, FolderName, PrefabName, true, &prefab) == false) {
			MessageBoxA(g_Editor->m_hwnd, "Unable to add prefab", "Add Prefab", MB_OK | MB_ICONERROR);
			return;
		}
	}

	m_pResourcesPanel->AddPrefab(prefab, PackageName, FolderName, PrefabName);
	//g_ResourceManager.DumpPackageInfo();
	//g_ResourceManager.SavePackages();

	MessageBoxA(g_Editor->m_hwnd, "Prefab added successfully", "Add Prefab", MB_OK | MB_ICONINFORMATION);
}

/// kbEditor::InsertSelectedPrefabIntoScene
void kbEditor::InsertSelectedPrefabIntoScene() {

	const kbPrefab* const prefabToCreate = g_Editor->m_pResourcesPanel->GetSelectedPrefab();
	if (prefabToCreate == nullptr) {
		return;
	}

	const kbCamera* const editorCamera = g_Editor->m_pMainTab->GetEditorWindowCamera();
	if (editorCamera == nullptr) {
		return;
	}

	const Vec3 entityLocation = editorCamera->m_position + (editorCamera->m_rotation.to_mat4()[2] * 4.0f).ToVec3();

	for (int i = 0; i < prefabToCreate->NumGameEntities(); i++) {
		GameEntity* const pNewEntity = new GameEntity(prefabToCreate->m_GameEntities[i], false);
		kbEditorEntity* const pEditorEntity = new kbEditorEntity(pNewEntity);
		pEditorEntity->set_position(entityLocation);
		g_Editor->m_GameEntities.push_back(pEditorEntity);
	}
}