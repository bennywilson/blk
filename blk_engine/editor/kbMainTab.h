/// kbMainTab.h
///
/// 2016 blk
#pragma once

#pragma warning(push)
#pragma warning(disable:4312)
#include <FL/Fl_Tabs.h>
#pragma warning(pop)

/// kbMainTab
class kbMainTab : public EditorPanel, public Fl_Tabs {
	friend class kbEditor;

public:
	kbMainTab(int x, int y, int w, int h);

	const kbEditorWindow* GetEditorWindow() const { return m_pEditorWindow; }
	kbEditorWindow* GetGameWindow() const { return m_pGameWindow; }

	virtual void update(const f32 dt);
	virtual void render_sync() override;

	virtual void EventCB(const widgetCBObject* const widgetCBObject);

	kbCamera* GetEditorWindowCamera() const { return &m_pEditorWindow->GetCamera(); }

	void SetCameraSpeedMultiplier(const float newMultiplier) { m_CameraMoveSpeedMultiplier = max(min(newMultiplier, 100.0f), 0.1f); }

private:
	void InputCB(const widgetCBObject* const widgetCBObject);
	void CameraMoveCB(const widgetCBInputObject* const widgetCBObject);
	void EntityTransformedCB(const widgetCBObject* const widgetCBObject);

	// Currently unreachable: render_sync()'s entire body (the only caller of
	// this, and of the click-to-select GetEntityIdAtScreenPosition logic
	// alongside it) is commented out. Phase 3, Milestone 2 added ImGui input
	// capture on the same viewport HWND -- when this is revived, gate its
	// entry point on !ImGui::GetIO().WantCaptureMouse, mirroring the guard
	// added to kbEditor::handle(), so a click on an ImGui panel can't also
	// grab the gizmo or pick scene geometry underneath it.
	void ManipulatorEvent(const bool bClicked, const Vec2i& mouseXY);

	kbManipulator& GetManipulator() { return m_Manipulator; }

	kbEditorWindow* GetCurrentWindow();

	kbEditorWindow* m_pEditorWindow;
	kbEditorWindow* m_modelViewerWindow;
	kbEditorWindow* m_pGameWindow;

	std::vector<Fl_Group*> m_Groups;

	//
	const kbModel* m_pCurrentlySelectedResource;

	kbManipulator m_Manipulator;

	float m_CameraMoveSpeedMultiplier;
};
