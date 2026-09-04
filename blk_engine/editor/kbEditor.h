/// kbEditor.h
///
// 2016 blk

#pragma once

#include <functional>

#include "editor_panel.h"
#include "game.h"
#include "kbUndoAction.h"

class EditorPanel;
class OutlinerPanel;
class PropertiesPanel;
class WorkbenchPanel;
class ResourcesPanel;
class kbEditorEntity;
class kbTypeInfoClass;

enum widgetCBType_t;

// Phase 3, Milestone 4 Step 2: kbEditor::OutputCB now pushes into a
// std::vector<LogEntry> (g_OutputLog, kbEditor.cpp) instead of appending to
// an Fl_Text_Buffer -- WorkbenchPanel::DrawOutputLog() reads it directly via
// an extern declaration.
struct LogEntry {
	kbOutputMessageType_t type;
	std::string text;
};

 /// kbEditor
///
/// Phase 3, Milestone 8: no longer an Fl_Window. It owns a raw Win32 top-level
/// window (m_hwnd), created in the constructor, with WndProc/handle_message
/// below in place of FLTK's handle(). That same window is the viewport the
/// swapchain and ImGui_ImplWin32_Init() target -- the separate child
/// kbEditorWindow is gone -- so ImGui's HWND and the source of the input
/// messages are finally the same window. That is what lets
/// ImGui_ImplWin32_WndProcHandler do the mouse/keyboard translation natively,
/// and why Milestone 3's hand-rolled input bridge (the FLTK keysym table, the
/// ClientToScreen/ScreenToClient round-trip, the explicit AddMousePosEvent/
/// AddKeyEvent/AddMouseButtonEvent calls) was deleted rather than ported.
class kbEditor {
	friend class WorkbenchPanel;

public:
	kbEditor();
	~kbEditor();

	void shut_down();

	void UnloadMap();
	void LoadMap(const std::string& mapName);
	void SetGame(class kbGame* pGame) { m_pGame = pGame; }

	void Update();

	HWND hwnd() const { return m_hwnd; }

	// The viewport filled the whole window as of Milestone 7 and that window
	// became the top-level one in Milestone 8, so this is m_hwnd. Kept as its
	// own name because callers mean "the swapchain/ImGui target" rather than
	// "the editor's window".
	HWND main_viewport_hwnd() const { return m_hwnd; }

	const bool IsRunning() const { return m_bIsRunning; }
	const bool IsRunningGame() const { return m_pGame != nullptr && m_pGame->IsPlaying(); }

	void RegisterUpdate(EditorPanel* const widget) { m_UpdateWidgets.push_back(widget); }
	void RegisterEvent(EditorPanel* const widget, const widgetCBType_t eventType) { m_EventReceivers[eventType].push_back(widget); }
	void BroadcastEvent(const class widgetCBObject& cbObject);

	void RegisterImGuiPanel(EditorPanel* const panel) { m_ImGuiPanels.push_back(panel); }
	void DrawImGuiPanels();

	// Phase 3, Milestone 4: DrawImGuiPanels() runs mid-frame, inside the
	// D3D12 command list's recording for the "ui_overlay" pass (see
	// Renderer_Dx12::render_ui_overlay()'s m_ui_draw_callback). Any action
	// that touches the renderer's per-frame command allocator/list --
	// level load/save eagerly loads textures via Renderer_Dx12::load_texture(),
	// which Reset()s that same allocator -- must not run from inside an
	// ImGui widget callback. Queue it here instead; kbEditor::Update() drains
	// the queue right after render() returns for the frame, matching the
	// timing FLTK's menu bar always had (its callbacks fired from Windows'
	// message dispatch, strictly before render() each loop iteration).
	void DeferAction(std::function<void()> action) { m_DeferredActions.push_back(std::move(action)); }

	void SetMainCameraPos(const Vec3& newCamPos);
	Vec3 GetMainCameraPos() const;

	void SetMainCameraRot(const Quat4& newCamRot);
	Quat4 GetMainCameraRot() const;

	void AddEntity(kbEditorEntity* const pEditorEntity);
	void SelectEntities(std::vector< kbEditorEntity* >& entitiesToSelect, bool AppendToSelectedList);
	void DeselectEntities();
		 
	void PushUndoAction(kbUndoAction* pUndoAction) { m_UndoStack.Push(pUndoAction); }
	void DeleteEntities(std::vector<kbEditorEntity*>& editorEntityList);

	std::vector<kbEditorEntity*>& GetGameEntities() { return m_GameEntities; }
	std::vector<kbEditorEntity*>& GetSelectedObjects() { return m_SelectedObjects; }

	const kbPrefab* GetCurrentlySelectedPrefab() const;

	const widgetCBInputObject& get_input() const { return m_WidgetInputObject; }

	bool IsGameUpdating() const { return m_bGameUpdating; }

private:
	void SaveLevel_Internal(const std::string& fileName, const bool bForceSave);

	// Phase 3, Milestone 8: replaces the Fl_Window base and kbEditorWindow's
	// child HWND both. Registered/created in the constructor; the window proc
	// is guarded on m_bIsRunning so the messages Windows delivers during
	// CreateWindowEx (before the panels exist) and after shut_down() fall
	// through to DefWindowProc rather than reaching half-built state.
	static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);
	LRESULT handle_message(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);

	HWND m_hwnd = nullptr;

	std::string	m_CurrentLevelFileName;

	std::vector<EditorPanel*> m_UpdateWidgets;
	std::map<widgetCBType_t, std::vector< EditorPanel*>> m_EventReceivers;
	std::vector<EditorPanel*> m_ImGuiPanels;
	std::vector<kbEditorEntity*> m_GameEntities;
	std::vector<kbEditorEntity*> m_SelectedObjects;
	std::vector<kbEditorEntity*> m_RemovedEntities;
	std::vector<std::function<void()>> m_DeferredActions;

	kbUndoStack	m_UndoStack;

	kbGame* m_pGame = nullptr;
	int m_CamSpeedIdx = 0;

	// Phase 3, Milestone 4 Step 3: AddEntityAsPrefab() fires from a menu
	// callback during Windows' message pump -- outside any active ImGui frame
	// (before NewFrame()/after EndFrame()). ImGui::OpenPopup() needs a valid
	// window/ID-stack context, so it can't be called directly from there; this
	// flag lets WorkbenchPanel::DrawAddPrefabPopup() open it from inside
	// draw_imgui() instead, where that context exists. Note DeferAction() is
	// not a substitute -- its queue drains outside the frame too.
	bool m_bWantOpenAddPrefabPopup = false;

	// Phase 3, Milestone 8: same flag-across-the-frame-boundary trick for the
	// viewport's Duplicate/Create Prefab/Replace Prefab/Place Prefab menu,
	// raised by RightClickOnMainTab() from the WndProc and consumed by
	// WorkbenchPanel::DrawViewportContextMenu().
	bool m_bWantOpenViewportContextMenu = false;

	float m_XFormAmount = 0.0f;

	// widgets
	class kbMainTab* m_pMainTab = nullptr;
	int m_ViewModeIdx = 0;
	OutlinerPanel* m_pOutlinerPanel = nullptr;
	PropertiesPanel* m_pPropertiesPanel = nullptr;
	WorkbenchPanel* m_pWorkbenchPanel = nullptr;
	ResourcesPanel* m_pResourcesPanel = nullptr;

	kbTimer	m_Timer;

	// input
	widgetCBInputObject	m_WidgetInputObject;

	bool m_bIsRunning = false;
	bool m_bRightMouseButtonDragged = false;
	bool m_bGameUpdating = false;

	// Phase 3, Milestone 2: latched at FL_PUSH and held for the whole
	// mouse gesture, mirroring m_bRightMouseButtonDragged's press-time-latch
	// pattern -- so a drag that starts over an ImGui panel doesn't also
	// drive the camera/manipulator even if the cursor leaves the panel mid-drag.
	bool m_bLeftMouseButtonCapturedByImGui = false;
	bool m_bRightMouseButtonCapturedByImGui = false;

	// Stores a copy of the current undo action's id.  The level is dirty if the two values don't match
	uint64_t m_UndoIDAtLastSave = 0;

	// internal functions and callbacks
	//
	// Phase 3, Milestone 8: these were all typed (Fl_Widget*, void*) as FLTK
	// widget callbacks. Nothing has passed a widget since Milestone 4 moved
	// the menu bar and toolbar to ImGui, and the void* userdata was only ever
	// read by add_component (now a typed parameter) and Close (which used it
	// for a this-pointer g_Editor already provides), so the signatures follow
	// the FLTK dependency out.

	static void NewLevel();
	static void OpenLevel();
	static void SaveLevelAs();
	static void SaveLevel();

	static void	Undo();
	static void	Redo();
	static void	Close();
	static void	CreateGameEntity();
	static void	add_component(const kbTypeInfoClass* const typeInfoClass);
	static void	TranslationButtonCB();
	static void	RotationButtonCB();
	static void	ScaleButtonCB();
	static void	XPlusAdjustButtonCB();
	static void	YPlusAdjustButtonCB();
	static void	ZPlusAdjustButtonCB();
	static void	XNegAdjustButtonCB();
	static void	YNegAdjustButtonCB();
	static void	ZNegAdjustButtonCB();
	void SetCamSpeedIndex(int idx);
	static void	ToggleIconsCB();
	static void	OutputCB(kbOutputMessageType_t, const char*);
	static void	PlayGameFromHere();
	static void	StopGame();
	static void	DeleteEntitiesCB();



	void RightClickOnMainTab();

	static void DuplicateEntity();
	static void ReplaceCurrentlySelectedPrefab();
	static void AddEntityAsPrefab();
	void AddEntityAsPrefab_Internal(const std::string& PackageName, const std::string& FolderName, const std::string& PrefabeName);
	static void InsertSelectedPrefabIntoScene();

public:
	static const int TabHeight() { return 25; }
	static const int PanelBorderSize(int Multiplier = 1) { return 5 * Multiplier; }

	// Phase 3, Milestone 4: shared layout constants so every panel positions off
	// one source of truth instead of several independently-varying ones.
	static const int MenuBarHeight() { return 20; }
	static const int ToolbarHeight() { return 30; }
	static const int BottomPanelHeight() { return 125; }

	static int NumCamSpeedBindings();
	static const char* CamSpeedBindingName(int idx);
};

extern kbEditor* g_Editor;
