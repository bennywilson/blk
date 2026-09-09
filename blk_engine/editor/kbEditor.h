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

// Output log entry with a message type
struct LogEntry {
	kbOutputMessageType_t type;
	std::string text;
};

/// kbEditor
///
/// Owns a raw Win32 top-level window (m_hwnd), created in the constructor,
/// with WndProc/handle_message. That same window is the viewport the
/// swapchain and ImGui_ImplWin32_Init() target
class kbEditor {
	friend class WorkbenchPanel;

public:
	kbEditor();
	~kbEditor();

	// Signals the main loop to exit; it polls IsRunning() each iteration. Does
	// no teardown of its own -- that happens once, in the destructor.
	void request_quit();

	void UnloadMap();
	void LoadMap(const std::string& mapName);
	void SetGame(class kbGame* pGame) { m_pGame = pGame; }

	void Update();

	HWND hwnd() const { return m_hwnd; }

	const bool IsRunning() const { return m_bIsRunning; }
	const bool IsRunningGame() const { return m_pGame != nullptr && m_pGame->IsPlaying(); }

	void RegisterUpdate(EditorPanel* const widget) { m_UpdateWidgets.push_back(widget); }
	void RegisterEvent(EditorPanel* const widget, const widgetCBType_t eventType) { m_EventReceivers[eventType].push_back(widget); }
	void BroadcastEvent(const class widgetCBObject& cbObject);

	void RegisterImGuiPanel(EditorPanel* const panel) { m_ImGuiPanels.push_back(panel); }
	void DrawImGuiPanels();

	// Hosts the dockspace the panels dock into. it has to be submitted before every
	// dockable window in the frame, which the m_ImGuiPanels loop can't guarantee.
	void DrawDockSpace();

	// DrawImGuiPanels() runs mid-frame, inside the "ui_overlay" pass's command
	// list recording (Renderer_Dx12::render_ui_overlay()'s m_ui_draw_callback),
	// so a widget callback must not touch the renderer's per-frame command
	// allocator/list. (Ex: level load/save eagerly loads textures and
	// Reset()s it). Queue the action here instead and drain when render()
	// returns for the frame.
	void DeferAction(std::function<void()> action) { m_DeferredActions.push_back(std::move(action)); }

	// The viewport whose camera "the camera" refers to. Only one exists today,
	// but callers wanting the main camera (Ex: level load restoring a saved
	// position, ResourcesPanel framing an asset) really want the active
	// viewport's camera. Going through here keeps that distinction if a second
	// viewport is ever added -- see ViewportPanel's multi-viewport notes for
	// what else would have to change.
	class ViewportPanel* active_viewport() const { return m_pViewportPanel; }

	void SetCamSpeedIndex(int idx);
	int cam_speed_index() const { return m_CamSpeedIdx; }

	// Thin forwarders onto active_viewport()'s camera, kept because several
	// panels read naturally in these terms.
	void SetMainCameraPos(const Vec3& newCamPos);
	Vec3 GetMainCameraPos() const;

	void SetMainCameraRot(const Quat4& newCamRot);

	void AddEntity(kbEditorEntity* const pEditorEntity);
	void SelectEntities(std::vector< kbEditorEntity* >& entitiesToSelect, bool AppendToSelectedList);
	void DeselectEntities();
		 
	void PushUndoAction(kbUndoAction* pUndoAction) { m_UndoStack.Push(pUndoAction); }
	void DeleteEntities(std::vector<kbEditorEntity*>& editorEntityList);

	std::vector<kbEditorEntity*>& GetGameEntities() { return m_GameEntities; }
	std::vector<kbEditorEntity*>& GetSelectedObjects() { return m_SelectedObjects; }

	const kbPrefab* GetCurrentlySelectedPrefab() const;

private:
	// Teardown, and deliberately not callable from outside: it runs exactly once,
	// from ~kbEditor. Anything that wants to end the session calls request_quit().
	void shut_down();

	void SaveLevel_Internal(const std::string& fileName, const bool bForceSave);

	// Registered/created in the constructor; the window proc is guarded on m_bIsRunning
	// so the messages Windows delivers during CreateWindowEx (before the panels exist)
	// and after the session ends fall through to DefWindowProc rather than reaching half-built state.
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

	// AddEntityAsPrefab() runs off the viewport context menu's DeferAction()
	// queue, which drains outside any active ImGui frame. ImGui::OpenPopup()
	// needs a valid window/ID-stack context, so it can't be called from there;
	// this flag lets WorkbenchPanel::DrawAddPrefabPopup() open it from inside
	// draw_imgui() instead.
	bool m_bWantOpenAddPrefabPopup = false;

	// Similar to the above.  Raised by RightClickOnViewport() from the WndProc
	// and consumed by WorkbenchPanel::DrawViewportContextMenu(). DeferAction()
	// is no substitute here -- its queue drains outside the frame too.
	bool m_bWantOpenViewportContextMenu = false;

	// Raised when DrawDockSpace() builds the default layout, consumed
	// after every panel has been submitted for the frame. Focusing a docked
	// window is what selects its tab, and it only takes effect once that window
	// exists for the frame. Calling it during the layout build is too early.
	bool m_bApplyDefaultDockFocus = false;

	float m_XFormAmount = 0.0f;

	// widgets
	class ViewportPanel* m_pViewportPanel = nullptr;
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

	// Latched at WM_LBUTTONDOWN/WM_RBUTTONDOWN and held until released.
	// So a drag that starts over an ImGui panel doesn't also drive the
	// camera/manipulator even if the cursor leaves the panel mid-drag.
	bool m_bLeftMouseButtonCapturedByImGui = false;
	bool m_bRightMouseButtonCapturedByImGui = false;

	// Stores a copy of the current undo action's id.  The level is dirty if the two values don't match
	uint64_t m_UndoIDAtLastSave = 0;

	// internal functions and callbacks

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
	static void	ToggleIconsCB();
	static void	OutputCB(kbOutputMessageType_t, const char*);
	static void	PlayGameFromHere();
	static void	StopGame();
	static void	DeleteEntitiesCB();

	void RightClickOnViewport();

	static void DuplicateEntity();
	static void ReplaceCurrentlySelectedPrefab();
	static void AddEntityAsPrefab();
	void AddEntityAsPrefab_Internal(const std::string& PackageName, const std::string& FolderName, const std::string& PrefabeName);
	static void InsertSelectedPrefabIntoScene();

public:
	// Shared layout constant, so the toolbar and the dockspace beneath it
	// position off one source of truth rather than two that can drift.
	static const int ToolbarHeight() { return 30; }

	static int NumCamSpeedBindings();
	static const char* CamSpeedBindingName(int idx);
};

extern kbEditor* g_Editor;
