/// kbEditor.cpp
///
/// 2016 blk

#pragma warning(push)
#pragma warning(disable:4312)
#include <FL/FL_Window.h>
#include <FL/Fl_Menu_Bar.h>
#include <FL/Fl_Select_Browser.h>
#pragma warning(pop)

#include <iomanip>
#include <sstream>
#include "blk_core.h"
#include <commdlg.h>
#include "blk_containers.h"
#include "Matrix.h"
#include "Quaternion.h"
#include "game.h"
#include "editor_panel.h"
#include "editor_window.h"
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
#include "imgui.h"

// shutdown cb
static void shutdown_cb(Fl_Widget* widget, void* const data) {
	kbEditor* const self = (kbEditor*)data;
	self->shut_down();
}

/// fl_key_to_imgui_key
///
/// Phase 3, Milestone 3: FLTK has no built-in ImGui backend, so key codes
/// from Fl::event_key() (FLTK's own keysyms -- for printable ASCII keys
/// these are just the character code) need mapping to ImGuiKey by hand.
/// Covers the keys a text-editing field actually needs (typing, navigation,
/// commit/cancel, copy/paste modifiers) -- not an exhaustive keysym table.
static ImGuiKey fl_key_to_imgui_key(const int fl_key) {
	if (fl_key >= 'a' && fl_key <= 'z') return (ImGuiKey)(ImGuiKey_A + (fl_key - 'a'));
	if (fl_key >= 'A' && fl_key <= 'Z') return (ImGuiKey)(ImGuiKey_A + (fl_key - 'A'));
	if (fl_key >= '0' && fl_key <= '9') return (ImGuiKey)(ImGuiKey_0 + (fl_key - '0'));
	if (fl_key >= FL_F && fl_key <= FL_F_Last) return (ImGuiKey)(ImGuiKey_F1 + (fl_key - FL_F - 1));

	switch (fl_key) {
	case FL_BackSpace: return ImGuiKey_Backspace;
	case FL_Tab: return ImGuiKey_Tab;
	case FL_Enter: return ImGuiKey_Enter;
	case FL_Escape: return ImGuiKey_Escape;
	case ' ': return ImGuiKey_Space;
	case FL_Left: return ImGuiKey_LeftArrow;
	case FL_Right: return ImGuiKey_RightArrow;
	case FL_Up: return ImGuiKey_UpArrow;
	case FL_Down: return ImGuiKey_DownArrow;
	case FL_Page_Up: return ImGuiKey_PageUp;
	case FL_Page_Down: return ImGuiKey_PageDown;
	case FL_Home: return ImGuiKey_Home;
	case FL_End: return ImGuiKey_End;
	case FL_Insert: return ImGuiKey_Insert;
	case FL_Delete: return ImGuiKey_Delete;
	case FL_Shift_L: return ImGuiKey_LeftShift;
	case FL_Shift_R: return ImGuiKey_RightShift;
	case FL_Control_L: return ImGuiKey_LeftCtrl;
	case FL_Control_R: return ImGuiKey_RightCtrl;
	case FL_Alt_L: return ImGuiKey_LeftAlt;
	case FL_Alt_R: return ImGuiKey_RightAlt;
	case '-': return ImGuiKey_Minus;
	case '=': return ImGuiKey_Equal;
	case '.': return ImGuiKey_Period;
	case ',': return ImGuiKey_Comma;
	case '/': return ImGuiKey_Slash;
	case '\\': return ImGuiKey_Backslash;
	case ';': return ImGuiKey_Semicolon;
	case '\'': return ImGuiKey_Apostrophe;
	case '[': return ImGuiKey_LeftBracket;
	case ']': return ImGuiKey_RightBracket;
	case '`': return ImGuiKey_GraveAccent;
	default: return ImGuiKey_None;
	}
}

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
kbEditor::kbEditor() :
	Fl_Window(12, 12, GetSystemMetrics(SM_CXFULLSCREEN) - 16, GetSystemMetrics(SM_CYFULLSCREEN) - 16) {

	m_bGameUpdating = false;
	const float editorInitStartTime = g_GlobalTimer.TimeElapsedSeconds();

	m_UndoIDAtLastSave = UINT64_MAX;
	m_CurrentLevelFileName = "Untitled";

	g_Editor = this;

	m_pGame = nullptr;
	m_pGameWindow = nullptr;

	const int Screen_Width = GetSystemMetrics(SM_CXFULLSCREEN);
	const int Screen_Height = GetSystemMetrics(SM_CYFULLSCREEN);
	const int Menu_Bar_Height = MenuBarHeight();
	const int Menu_Buttons_Height = ToolbarHeight();
	const int Left_Panel = 200;
	const int Bottom_Panel_Height = BottomPanelHeight();
	const int Right_Panel = 300;

	g_OutputCB = kbEditor::OutputCB;

	this->callback(shutdown_cb, this);

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

	end();
	show();

	//buff->text( "asdlkjasldkjalskdjlaskjd" );
	//Fl::run();

	// setup the renderer
	/*if (g_pRenderer == nullptr) {
		g_pRenderer = new kbRenderer_DX11();
		g_pRenderer->Init(m_pMainTab->GetEditorWindow()->GetWindowHandle(), 1920, 1080);
		g_pRenderer->EnableDebugBillboards(true);
	}*/

	m_pResourcesPanel->PostRendererInit();

	m_bIsRunning = true;

	m_Timer.Reset();

	// reserve textures
	//g_pRenderer->LoadTexture("../../blk_engine/assets/Textures/Editor/EntityIcon.jpg", 1);
	//g_pRenderer->LoadTexture("../../blk_engine/assets/Textures/Editor/directionalLightIcon.jpg", 2);

	SetWindowText(m_pMainTab->GetEditorWindow()->GetWindowHandle(), "kbEditor");

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
				SetWindowText(fl_xid(this), windowText.c_str());

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
		StopGame(nullptr, nullptr);
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

	if (GetFocus() == fl_xid(this)) {

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
		// the camera at the same time. Modifier keys are separately fed to
		// ImGui via fl_key_to_imgui_key in handle(), so no functionality is
		// lost while a field has keyboard focus.
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

	Fl::flush();

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
		SetWindowText(fl_xid(this), ("blk 1.0 - " + m_CurrentLevelFileName + "*").c_str());
	} else {
		SetWindowText(fl_xid(this), ("blk 1.0  - " + m_CurrentLevelFileName).c_str());
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

/// kbEditor::main_viewport_hwnd
HWND kbEditor::main_viewport_hwnd() const {
	return m_pMainTab->GetEditorWindow()->GetWindowHandle();
}

/// kbEditor::handle
int kbEditor::handle(int theEvent) {
	const int button = Fl::event_button();
	const int state = Fl::event_state();

	int newMouseX = Fl::event_x();
	int newMouseY = Fl::event_y();

	// Phase 3, Milestone 3: feed mouse position explicitly for every event
	// that reaches here (including plain FL_MOVE, which has no dedicated
	// branch below) instead of relying solely on imgui_impl_win32's passive
	// GetCursorPos() fallback (which is also fragile to is_app_focused
	// slipping). Fl::event_x()/event_y() are relative to THIS top-level
	// kbEditor window, but ImGui_ImplWin32_Init() was given
	// main_viewport_hwnd() -- a child window offset within this one by the
	// Resources sidebar/toolbar -- so ImGui's coordinate space is relative
	// to that child, not this window. Feeding the raw top-level-relative
	// value directly shifts every position ImGui sees by exactly that
	// child's offset. Route through screen space to convert correctly
	// regardless of current layout/DPI.
	if (g_imgui_enabled && ImGui::GetCurrentContext() != nullptr) {
		POINT screen_point = { newMouseX, newMouseY };
		ClientToScreen(fl_xid(this), &screen_point);
		ScreenToClient(main_viewport_hwnd(), &screen_point);
		ImGui::GetIO().AddMousePosEvent((float)screen_point.x, (float)screen_point.y);
	}

	// Phase 3, Milestone 3: keyboard was never fed to ImGui at all -- WASD
	// camera movement bypasses FLTK's event system entirely (raw
	// GetAsyncKeyState polling in Update()), so nothing else needed it until
	// text fields (KBSTRING/INT/FLOAT editing) existed. Feed both the key
	// event (for navigation/backspace/enter/etc via fl_key_to_imgui_key)
	// and the resulting text (for what actually gets typed) -- FLTK already
	// does the keyboard-layout/shift-state translation into Fl::event_text().
	if (g_imgui_enabled && ImGui::GetCurrentContext() != nullptr && (theEvent == FL_KEYDOWN || theEvent == FL_KEYUP)) {
		const int fl_key = Fl::event_key();
		const ImGuiKey imgui_key = fl_key_to_imgui_key(fl_key);
		if (imgui_key != ImGuiKey_None) {
			ImGui::GetIO().AddKeyEvent(imgui_key, theEvent == FL_KEYDOWN);
		}
		if (theEvent == FL_KEYDOWN) {
			const char* const event_text = Fl::event_text();
			if (event_text != nullptr && event_text[0] != '\0') {
				ImGui::GetIO().AddInputCharactersUTF8(event_text);
			}
		}
		if (ImGui::GetIO().WantCaptureKeyboard) {
			return 1;
		}
	}

	// Phase 3, Milestone 4: explicit shortcut dispatch replacing the FLTK
	// Fl_Menu_Bar's implicit shortcut-matching, now that the menu bar itself
	// is gone (WorkbenchPanel draws an ImGui menu bar, which has no built-in
	// accelerator-key handling of its own). The WantCaptureKeyboard early-out
	// above already guards against firing while typing into an ImGui field.
	if (theEvent == FL_KEYDOWN) {
		if (Fl::event_state() & FL_CTRL) {
			switch (Fl::event_key()) {
			case 'n': NewLevel(nullptr, nullptr); return 1;
			case 'o': OpenLevel(nullptr, nullptr); return 1;
			case 's': SaveLevel(nullptr, nullptr); return 1;
			case 'z': Undo(nullptr, nullptr); return 1;
			case 'y': Redo(nullptr, nullptr); return 1;
			case 'p': PlayGameFromHere(nullptr, nullptr); return 1;
			case 'q': StopGame(nullptr, nullptr); return 1;
			default: break;
			}
		} else if (Fl::event_key() == FL_Delete) {
			DeleteEntitiesCB(nullptr, nullptr);
			return 1;
		}
	}

	// check for right mouse button
	if (theEvent == FL_PUSH) {
		m_bRightMouseButtonDragged = false;

		// Phase 3, Milestone 2: feed the button transition into ImGui and
		// latch capture for the whole gesture (see m_bLeftMouseButtonCapturedByImGui's
		// declaration in kbEditor.h). Position is now fed explicitly above
		// (Milestone 3) rather than relying on the backend's passive fallback.
		if (g_imgui_enabled && ImGui::GetCurrentContext() != nullptr) {
			const int imgui_button = (button == 1) ? 0 : (button == 3) ? 1 : 2;
			ImGui::GetIO().AddMouseButtonEvent(imgui_button, true);
			const bool captured = ImGui::GetIO().WantCaptureMouse;
			if (button == 1) m_bLeftMouseButtonCapturedByImGui = captured;
			if (button == 3) m_bRightMouseButtonCapturedByImGui = captured;
		}

		// don't allow both buttons to be down
		/*if ( ( button == 3 && m_WidgetInputObject.leftMouseButtonDown ) ||
			( button == 1 && m_WidgetInputObject.rightMouseButtonDown ) ) {
				return 1;
		}*/

		m_WidgetInputObject.mouseX = newMouseX;
		m_WidgetInputObject.mouseY = newMouseY;

		if (button == 3 && !m_bRightMouseButtonCapturedByImGui) {
			//	m_WidgetInputObject.rightMouseButtonDown = true;
			m_WidgetInputObject.rightMouseButtonPressed = true;
		} else if (button == 1 && !m_bLeftMouseButtonCapturedByImGui) {
			//m_WidgetInputObject.leftMouseButtonDown = true;
			m_WidgetInputObject.leftMouseButtonPressed = true;
		}

		Fl_Window::handle(theEvent);
		return 1;
	} else if (theEvent == FL_RELEASE) {

		if (g_imgui_enabled && ImGui::GetCurrentContext() != nullptr) {
			const int imgui_button = (button == 1) ? 0 : (button == 3) ? 1 : 2;
			ImGui::GetIO().AddMouseButtonEvent(imgui_button, false);
		}

		// Phase 3, Milestone 7: the viewport now spans the whole window, so the
		// bounds test alone no longer distinguishes "right-clicked the scene"
		// from "right-clicked a panel" -- it used to, only because the panels
		// sat in margins outside the viewport rect. The ImGui capture latched
		// at FL_PUSH is what separates them now.
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
		Fl_Window::handle(theEvent);
		return 1;
	} else if (m_WidgetInputObject.leftMouseButtonDown && !m_bLeftMouseButtonCapturedByImGui && theEvent == FL_DRAG) {
		m_WidgetInputObject.mouseDeltaX = newMouseX - m_WidgetInputObject.mouseX;
		m_WidgetInputObject.mouseDeltaY = newMouseY - m_WidgetInputObject.mouseY;

		m_WidgetInputObject.mouseX = newMouseX;
		m_WidgetInputObject.mouseY = newMouseY;
	} else if (m_WidgetInputObject.rightMouseButtonDown && !m_bRightMouseButtonCapturedByImGui && theEvent == FL_DRAG) {
		m_bRightMouseButtonDragged = true;
		m_WidgetInputObject.mouseDeltaX = newMouseX - m_WidgetInputObject.mouseX;
		m_WidgetInputObject.mouseDeltaY = newMouseY - m_WidgetInputObject.mouseY;

		m_WidgetInputObject.mouseX = newMouseX;
		m_WidgetInputObject.mouseY = newMouseY;
		HWND hWnd = fl_xid(this);
		RECT rc;
		GetClientRect(hWnd, &rc);

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

			ClientToScreen(hWnd, &point);
			SetCursorPos(point.x, point.y);
		}

		m_WidgetInputObject.mouseX = newMouseX;
		m_WidgetInputObject.mouseY = newMouseY;
	} else if (m_WidgetInputObject.rightMouseButtonDown) {

	}

	return Fl_Window::handle(theEvent);
}


/// kbEditor::Close
void kbEditor::Close(Fl_Widget* widget, void* thisPtr) {
	kbEditor* editor = static_cast<kbEditor*>(thisPtr);
	editor->shut_down();
}

/// kbEditor::CreateGameEntity
void kbEditor::CreateGameEntity(Fl_Widget* widget, void* thisPtr) {

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
void kbEditor::add_component(Fl_Widget* widget, void* voidPtr) {
	if (voidPtr == nullptr || g_Editor == nullptr)
		return;

	kbTypeInfoClass* const typeInfoClass = static_cast<kbTypeInfoClass*>(voidPtr);
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
void kbEditor::TranslationButtonCB(class Fl_Widget*, void*) {
	widgetCBObject cbObject;
	cbObject.widgetType = WidgetCB_TranslationButtonPressed;
	g_Editor->BroadcastEvent(cbObject);
}

/// kbEditor::RotationButtonCB
void kbEditor::RotationButtonCB(class Fl_Widget*, void*) {
	widgetCBObject cbObject;
	cbObject.widgetType = WidgetCB_RotationButtonPressed;
	g_Editor->BroadcastEvent(cbObject);
}

/// kbEditor::ScaleButtonCB
void kbEditor::ScaleButtonCB(class Fl_Widget*, void*) {
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
void kbEditor::XPlusAdjustButtonCB(Fl_Widget*, void*) {

	XFormEntities(g_Editor->m_pMainTab->m_Manipulator, Vec4(1.0f, 0.0f, 0.0f, g_Editor->m_XFormAmount));
}

/// kbEditor::XNegAdjustButtonCB
void kbEditor::XNegAdjustButtonCB(Fl_Widget*, void*) {

	XFormEntities(g_Editor->m_pMainTab->m_Manipulator, Vec4(-1.0f, 0.0f, 0.0f, g_Editor->m_XFormAmount));
}

/// kbEditor::YPlusAdjustButtonCB
void kbEditor::YPlusAdjustButtonCB(Fl_Widget*, void*) {

	XFormEntities(g_Editor->m_pMainTab->m_Manipulator, Vec4(0.0f, 1.0f, 0.0f, g_Editor->m_XFormAmount));
}

/// kbEditor::YNegAdjustButtonCB
void kbEditor::YNegAdjustButtonCB(Fl_Widget*, void*) {

	XFormEntities(g_Editor->m_pMainTab->m_Manipulator, Vec4(0.0f, -1.0f, 0.0f, g_Editor->m_XFormAmount));
}

/// kbEditor::ZPlusAdjustButtonCB
void kbEditor::ZPlusAdjustButtonCB(Fl_Widget*, void*) {

	XFormEntities(g_Editor->m_pMainTab->m_Manipulator, Vec4(0.0f, 0.0f, 1.0f, g_Editor->m_XFormAmount));
}

/// kbEditor::ZNegAdjustButtonCB
void kbEditor::ZNegAdjustButtonCB(Fl_Widget*, void*) {

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
void kbEditor::ToggleIconsCB(Fl_Widget* widget, void* userData) {

	g_bBillboardsEnabled = !g_bBillboardsEnabled;
}

/// kbEditor::NewLevel
void kbEditor::NewLevel(Fl_Widget*, void*) {
	const int areYouSure = MessageBoxA(fl_xid(g_Editor), "Creating a new level.  Any unsaved changes will be lost.  Are you sure?", "New Level", MB_YESNO | MB_ICONQUESTION);
	if (areYouSure != IDYES) {
		return;
	}

	g_Editor->UnloadMap();

	g_Editor->m_GameEntities.clear();
	g_Editor->DeselectEntities();
}

/// kbEditor::OpenLevel
void kbEditor::OpenLevel(class Fl_Widget*, void*) {
	char fileNameBuf[MAX_PATH] = {};

	OPENFILENAMEA ofn = {};
	ofn.lStructSize = sizeof(ofn);
	ofn.hwndOwner = fl_xid(g_Editor);
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

	const int areYouSure = MessageBoxA(fl_xid(g_Editor), "You have unsaved changes.  Are you sure you want to open a new level?", "Open Level", MB_YESNO | MB_ICONQUESTION);
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
			const int overWriteIt = MessageBoxA(fl_xid(g_Editor), "File already exists.  Do you wish to overwrite it?", "Save Level", MB_YESNO | MB_ICONQUESTION);
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
void kbEditor::SaveLevelAs(class Fl_Widget*, void*) {

	char fileNameBuf[MAX_PATH] = {};

	OPENFILENAMEA ofn = {};
	ofn.lStructSize = sizeof(ofn);
	ofn.hwndOwner = fl_xid(g_Editor);
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
void kbEditor::SaveLevel(class Fl_Widget*, void*) {

	if (g_Editor->m_CurrentLevelFileName.empty()) {
		return;
	}

	g_Editor->SaveLevel_Internal(g_Editor->m_CurrentLevelFileName, true);
}

/// kbEditor::Undo
void kbEditor::Undo(class Fl_Widget*, void*) {
	g_Editor->m_UndoStack.Undo();
}

/// kbEditor::Redo
void kbEditor::Redo(class Fl_Widget*, void*) {
	g_Editor->m_UndoStack.Redo();
}

/// kbEditor::PlayGameFromHere
void kbEditor::PlayGameFromHere(class Fl_Widget*, void*) {
	if (g_Editor == nullptr || g_Editor->m_pGame == nullptr || g_Editor->m_pGame->IsPlaying()) {
		return;
	}

	g_Editor->m_bGameUpdating = true;
	g_pGame->HackEditorInit(g_Editor->m_pMainTab->GetEditorWindow()->GetWindowHandle(), g_Editor->m_GameEntities);
	/*std::vector< const GameEntity * > GameEntitiesList;

	for ( int i = 0; i < g_Editor->m_GameEntities.size(); i++ ) {
		GameEntitiesList.push_back( g_Editor->m_GameEntities[i]->GetGameEntity() );
	}

	g_Editor->m_pMainTab->GetGameWindow()->show();
	g_pRenderer->CreateRenderView( g_Editor->m_pMainTab->GetGameWindow()->GetWindowHandle() );
	g_Editor->m_pGame->InitGame( g_Editor->m_pMainTab->GetGameWindow()->GetWindowHandle(), 1600, 900, GameEntitiesList );

	widgetCBObject widgetCB;
	widgetCB.widgetType = WidgetCB_GameStarted;
	g_Editor->BroadcastEvent( widgetCB );
	g_Editor->show();
	Fl::check();*/
}

/// kbEditor::StopGame
void kbEditor::StopGame(class Fl_Widget*, void*) {
	if (g_Editor == nullptr || g_Editor->m_pGame == nullptr) {
		return;
	}

	g_Editor->m_bGameUpdating = false;
	ShowCursor(true);

	g_pGame->HackEditorShutdown();
	/*
	g_Editor->m_pGame->StopGame();

	delete g_Editor->m_pGameWindow;
	g_Editor->m_pGameWindow = nullptr;

	g_pRenderer->SetRenderWindow( nullptr );

	widgetCBObject widgetCB;
	widgetCB.widgetType = WidgetCB_GameStopped;
	g_Editor->BroadcastEvent( widgetCB );*/
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
void kbEditor::DeleteEntitiesCB(class Fl_Widget*, void*) {
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
void kbEditor::RightClickOnMainTab() {

	const kbPrefab* const prefab = g_Editor->m_pResourcesPanel->GetSelectedPrefab();
	std::string ReplacePrefabMessage = "Replace Prefab";
	std::string PlacePrefabMessage = "Place Prefab";
	std::string DuplicateMessage = "Duplicate Entity";

	if (g_Editor->GetSelectedObjects().size() > 0) {
		DuplicateMessage += g_Editor->GetSelectedObjects()[0]->GetGameEntity()->name().stl_str();
	}

	if (prefab == nullptr) {
		PlacePrefabMessage += "into scene";
	} else {
		PlacePrefabMessage += "[" + prefab->GetPrefabName() + "] into scene.";
		ReplacePrefabMessage += "[" + prefab->GetPrefabName() + "]";
	}

	Fl_Menu_Item rclick_menu[] = {
		{ DuplicateMessage.c_str(), 0, DuplicateEntity, 0 },
		{ "Create New Prefab",  0, AddEntityAsPrefab, (void*)0 },
		{ ReplacePrefabMessage.c_str(), 0, ReplaceCurrentlySelectedPrefab, (void*)1 },
		{ PlacePrefabMessage.c_str(),  0, InsertSelectedPrefabIntoScene, (void*)this },
		{ 0 } };

	if (g_Editor->m_SelectedObjects.size() != 1) {
		rclick_menu[0].deactivate();
		rclick_menu[1].deactivate();
		rclick_menu[2].deactivate();
	}

	if (prefab == nullptr) {
		rclick_menu[3].deactivate();
	}

	const Fl_Menu_Item* m = rclick_menu->popup(Fl::event_x(), Fl::event_y(), 0, 0, 0);
	if (m) {
		m->do_callback(0, m->user_data());
	}
}

/// kbEditor::GetCurrentlySelectedPrefab
const kbPrefab* kbEditor::GetCurrentlySelectedPrefab() const {
	return m_pResourcesPanel->GetSelectedPrefab();
}

/// kbEditor::ReplaceCurrentlySelectedPrefab
void kbEditor::ReplaceCurrentlySelectedPrefab(class Fl_Widget*, void*) {
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
void kbEditor::DuplicateEntity(Fl_Widget*, void* userdata) {
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
/// gone. Fires from RightClickOnMainTab's FLTK context menu (stays as-is,
/// out of scope for this milestone) during Windows' message pump, outside
/// any active ImGui frame -- ImGui::OpenPopup() needs a valid window/ID-
/// stack context it doesn't have there, so this just sets a flag;
/// WorkbenchPanel::DrawAddPrefabPopup() calls OpenPopup() itself from
/// inside draw_imgui() when it sees the flag set.
void kbEditor::AddEntityAsPrefab(Fl_Widget*, void* userdata) {
	g_Editor->m_bWantOpenAddPrefabPopup = true;
}

/// kbEditor::AddEntityAsPrefab_Internal
void kbEditor::AddEntityAsPrefab_Internal(const std::string& PackageName, const std::string& FolderName, const std::string& PrefabName) {

	if (m_SelectedObjects.size() != 1) {
		return;
	}

	if (PackageName.empty() || FolderName.empty() || PrefabName.empty()) {
		MessageBoxA(fl_xid(g_Editor), "Incomplete fields.  Prefab was not created", "Add Prefab", MB_OK | MB_ICONWARNING);
		return;
	}

	kbPrefab* prefab;
	if (g_ResourceManager.add_prefab(m_SelectedObjects[0]->GetGameEntity(), PackageName, FolderName, PrefabName, false, &prefab) == false) {
		const int shouldOverwrite = MessageBoxA(fl_xid(g_Editor), "Prefab with that name and path already exist.  Overwrite?", "Add Prefab", MB_YESNO | MB_ICONQUESTION);

		if (shouldOverwrite != IDYES)
			return;

		if (g_ResourceManager.add_prefab(m_SelectedObjects[0]->GetGameEntity(), PackageName, FolderName, PrefabName, true, &prefab) == false) {
			MessageBoxA(fl_xid(g_Editor), "Unable to add prefab", "Add Prefab", MB_OK | MB_ICONERROR);
			return;
		}
	}

	m_pResourcesPanel->AddPrefab(prefab, PackageName, FolderName, PrefabName);
	//g_ResourceManager.DumpPackageInfo();
	//g_ResourceManager.SavePackages();

	MessageBoxA(fl_xid(g_Editor), "Prefab added successfully", "Add Prefab", MB_OK | MB_ICONINFORMATION);
}

/// kbEditor::InsertSelectedPrefabIntoScene
void kbEditor::InsertSelectedPrefabIntoScene(Fl_Widget*, void* pUserdata) {

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