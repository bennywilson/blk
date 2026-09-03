/// kbEditor.h
///
// 2016 blk

#pragma once

#include <functional>

#include "editor_panel.h"
#include "editor_window.h"
#include "game.h"
#include "kbUndoAction.h"

#pragma warning(push)
#pragma warning(disable:4312)
#include <fl/fl.h>
#pragma warning(pop)

class EditorPanel;
class OutlinerPanel;
class PropertiesPanel;
class WorkbenchPanel;
class ResourcesPanel;
class kbEditorEntity;
class Fl_Widget;

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
class kbEditor : Fl_Window {
	friend class WorkbenchPanel;

public:
	kbEditor();
	~kbEditor();

	void shut_down();

	void UnloadMap();
	void LoadMap(const std::string& mapName);
	void SetGame(class kbGame* pGame) { m_pGame = pGame; }

	void Update();
	virtual int	handle(int theEvent);

	HWND main_viewport_hwnd() const;

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
	kbEditorWindow* m_pGameWindow = nullptr;
	int m_CamSpeedIdx = 0;

	// Phase 3, Milestone 4 Step 3: AddEntityAsPrefab() fires from an FLTK
	// menu callback during Windows' message pump -- outside any active
	// ImGui frame (before NewFrame()/after EndFrame()). ImGui::OpenPopup()
	// needs a valid window/ID-stack context, so it can't be called directly
	// from there; this flag lets WorkbenchPanel::DrawAddPrefabPopup() open
	// it from inside draw_imgui() instead, where that context exists.
	bool m_bWantOpenAddPrefabPopup = false;

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

	static void NewLevel(Fl_Widget*, void*);
	static void OpenLevel(Fl_Widget*, void*);
	static void SaveLevelAs(Fl_Widget*, void*);
	static void SaveLevel(Fl_Widget*, void*);
			    
	static void	Undo(Fl_Widget*, void*);
	static void	Redo(Fl_Widget*, void*);
	static void	Close(Fl_Widget*, void*);
	static void	CreateGameEntity(Fl_Widget*, void*);
	static void	add_component(Fl_Widget*, void*);
	static void	TranslationButtonCB(Fl_Widget*, void*);
	static void	RotationButtonCB(Fl_Widget*, void*);
	static void	ScaleButtonCB(Fl_Widget*, void*);
	static void	XPlusAdjustButtonCB(Fl_Widget*, void*);
	static void	YPlusAdjustButtonCB(Fl_Widget*, void*);
	static void	ZPlusAdjustButtonCB(Fl_Widget*, void*);
	static void	XNegAdjustButtonCB(Fl_Widget*, void*);
	static void	YNegAdjustButtonCB(Fl_Widget*, void*);
	static void	ZNegAdjustButtonCB(Fl_Widget*, void*);
	void SetCamSpeedIndex(int idx);
	static void	ToggleIconsCB(Fl_Widget*, void*);
	static void	OutputCB(kbOutputMessageType_t, const char*);
	static void	PlayGameFromHere(Fl_Widget*, void*);
	static void	StopGame(Fl_Widget*, void*);
	static void	DeleteEntitiesCB(Fl_Widget*, void*);



	void RightClickOnMainTab();

	static void DuplicateEntity(Fl_Widget*, void*);
	static void ReplaceCurrentlySelectedPrefab(Fl_Widget*, void*);
	static void AddEntityAsPrefab(Fl_Widget*, void*);
	void AddEntityAsPrefab_Internal(const std::string& PackageName, const std::string& FolderName, const std::string& PrefabeName);
	static void InsertSelectedPrefabIntoScene(Fl_Widget*, void*);

public:
	static const int TabHeight() { return 25; }
	static const int PanelBorderSize(int Multiplier = 1) { return 5 * Multiplier; }

	// Phase 3, Milestone 4: shared layout constants so the ImGui panels and the
	// still-FLTK kbMainTab position off the same source of truth instead of two
	// independently-varying layout systems.
	static const int MenuBarHeight() { return 20; }
	static const int ToolbarHeight() { return 30; }
	static const int BottomPanelHeight() { return 125; }

	static int NumCamSpeedBindings();
	static const char* CamSpeedBindingName(int idx);
};

extern kbEditor* g_Editor;
