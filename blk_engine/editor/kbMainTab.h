/// kbMainTab.h
///
/// 2016 blk
#pragma once

#pragma warning(push)
#pragma warning(disable:4312)
#include <FL/Fl_Tabs.h>
#pragma warning(pop)

#include "render_defs.h"

/// kbMainTab
class kbMainTab : public EditorPanel, public Fl_Tabs {
	friend class kbEditor;

public:
	kbMainTab(int x, int y, int w, int h);

	const kbEditorWindow* GetEditorWindow() const { return m_pEditorWindow; }
	kbEditorWindow* GetGameWindow() const { return m_pGameWindow; }

	virtual void update(const f32 dt);
	virtual void render_sync() override;
	virtual void draw_imgui() override;

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

	// Draggable world-space T/R/S gizmo (3 axes each) for the current
	// selection, drawn/hit-tested directly in screen space via ImGui's
	// foreground draw list. Deliberately does not go through
	// kbManipulator::AttemptMouseGrab()/UpdateMouseDrag() -- those hit-test
	// against m_models[mode], which is always null in the D3D12 build (the
	// D3D11-era model-loading code in kbManipulator::render_sync() is
	// entirely commented out and never runs), so calling them would
	// dereference a null pointer. World-space axes only (not entity-local),
	// and doesn't push an undo entry (matching the existing toolbar
	// axis-nudge buttons' behavior).
	void DrawGizmo();
	void DrawTranslateAxis(const int axis_index, const Vec3& origin, const RenderCamera& render_camera);
	void DrawScaleAxis(const int axis_index, const Vec3& origin, const RenderCamera& render_camera);
	void DrawRotateRing(const int axis_index, const Vec3& origin, const RenderCamera& render_camera);

	// Center handle: free (unconstrained) move in Translate, uniform scale
	// in Scale. Uses the sentinel axis index kGizmoCenterAxisIndex (3) in
	// m_GizmoDragAxis so it shares the same drag-state fields as the 3 axis
	// handles instead of needing its own.
	void DrawTranslateCenter(const Vec3& origin, const RenderCamera& render_camera);
	void DrawScaleCenter(const Vec3& origin, const RenderCamera& render_camera);

	// Shared drag-update, called from Draw{Translate,Scale}Axis once a drag
	// on axis_index is in progress: intersects the current mouse ray with
	// the camera-facing plane through the grab point (same technique
	// kbManipulator::UpdateMouseDrag() uses) and returns the signed distance
	// travelled along that world axis since the grab.
	bool UpdateAxisDrag(const int axis_index, const RenderCamera& render_camera, f32& out_delta) const;

	// Same camera-facing-plane intersection as UpdateAxisDrag(), but returns
	// the full unconstrained world-space delta instead of projecting it onto
	// one axis -- the center handle's free-move/uniform-scale reference.
	bool UpdateFreeDrag(const RenderCamera& render_camera, Vec3& out_delta) const;

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

	bool m_bGizmoDragging = false;
	int m_GizmoDragAxis = -1;
	kbManipulator::manipulatorMode_t m_GizmoDragMode = kbManipulator::Translate;

	Vec3 m_GizmoGrabWorldPoint;   // translate/scale: camera-plane reference point
	Vec3 m_GizmoGrabAngleVec;     // rotate: initial origin->hit vector, for signed-angle delta
	f32 m_GizmoGrabCenterDist = 0.0f; // scale center handle: reference distance at grab time

	std::vector<Vec3> m_GizmoGrabPositions;
	std::vector<Quat4> m_GizmoGrabRotations;
	std::vector<Vec3> m_GizmoGrabScales;
};
