/// kbMainTab.h
///
/// 2016 blk
#pragma once

#include "render_defs.h"
#include "editor_panel.h"
#include "camera.h"
#include "kbManipulator.h"

class kbEditorEntity;

/// kbMainTab
///
/// Phase 3, Milestone 7: no longer an Fl_Tabs. The three tabs it used to host
/// were one real viewport plus two that were never rendered into -- the model
/// viewer's update() branch held nothing but commented-out DrawLine calls
/// behind six placeholder buttons, and the game window appeared only inside
/// commented-out code (the running game renders into the editor window, via
/// HackEditorInit). The swapchain is created once against this one viewport's
/// HWND, so tab switching only ever desynced input mapping from rendering.
/// Collapsed to that single viewport; a tab bar can come back when there is a
/// second view worth switching to.
///
/// Phase 3, Milestone 8: and the viewport is no longer a window of its own
/// either. kbEditorWindow was a 23-line Fl_Window with an empty update() and
/// EventCB() whose only real content was the editor camera below, so with the
/// viewport already filling kbEditor's whole client area it was deleted and
/// the camera moved here. That collapse is what makes kbEditor's HWND the one
/// ImGui is initialized against.
class kbMainTab : public EditorPanel {
	friend class kbEditor;

public:
	kbMainTab(int x, int y, int w, int h);

	virtual void update(const f32 dt);
	virtual void render_sync() override;
	virtual void draw_imgui() override;

	virtual void EventCB(const widgetCBObject* const widgetCBObject);

	// const_cast because callers mutate the camera through this from const
	// methods (UpdateFreeDrag/UpdateAxisDrag). The old kbEditorWindow* member
	// indirection did the same thing implicitly -- constness simply never
	// propagated through the pointer.
	kbCamera* GetEditorWindowCamera() const { return const_cast<kbCamera*>(&m_Camera); }

	void SetCameraSpeedMultiplier(const float newMultiplier) { m_CameraMoveSpeedMultiplier = max(min(newMultiplier, 100.0f), 0.1f); }

private:
	void InputCB(const widgetCBObject* const widgetCBObject);
	void CameraMoveCB(const widgetCBInputObject* const widgetCBObject);
	void EntityTransformedCB(const widgetCBObject* const widgetCBObject);

	// Currently unreachable: render_sync()'s entire body, its only caller, is
	// commented out. The click-to-select that used to sit beside it there has
	// since been rebuilt as UpdateViewportPicking() below; this manipulator
	// grab has not, and the ImGui-era rules that would apply to reviving it
	// are the ones UpdateViewportPicking()/DrawGizmo() already follow -- run
	// inside draw_imgui()'s frame and gate on !io.WantCaptureMouse, so a click
	// on a panel can't also grab scene geometry underneath it.
	void ManipulatorEvent(const bool bClicked, const Vec2i& mouseXY);

	// Draggable world-space T/R/S gizmo (3 axes each) for the current
	// selection, drawn/hit-tested directly in screen space via ImGui's
	// foreground draw list. Deliberately does not go through
	// kbManipulator::AttemptMouseGrab()/UpdateMouseDrag() -- those hit-test
	// against m_models[mode], which is always null in the D3D12 build (the
	// D3D11-era model-loading code in kbManipulator::render_sync() is
	// entirely commented out and never runs), so calling them would
	// dereference a null pointer. World-space axes only (not entity-local).
	// Each completed drag pushes one kbUndoTransformEntities (see
	// BeginGizmoDrag/EndGizmoDrag); the toolbar axis-nudge buttons still
	// don't, which is pre-existing.
	void DrawGizmo();
	void DrawTranslateAxis(const int axis_index, const std::vector<kbEditorEntity*>& selected, const Vec3& origin, const RenderCamera& render_camera);
	void DrawScaleAxis(const int axis_index, const std::vector<kbEditorEntity*>& selected, const Vec3& origin, const RenderCamera& render_camera);
	void DrawRotateRing(const int axis_index, const std::vector<kbEditorEntity*>& selected, const Vec3& origin, const RenderCamera& render_camera);

	// Center handle: free (unconstrained) move in Translate, uniform scale
	// in Scale. Uses the sentinel axis index kGizmoCenterAxisIndex (3) in
	// m_GizmoDragAxis so it shares the same drag-state fields as the 3 axis
	// handles instead of needing its own.
	void DrawTranslateCenter(const std::vector<kbEditorEntity*>& selected, const Vec3& origin, const RenderCamera& render_camera);
	void DrawScaleCenter(const std::vector<kbEditorEntity*>& selected, const Vec3& origin, const RenderCamera& render_camera);

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

	// Shared drag-start for all five handles: latches the drag state and
	// snapshots which entities are about to move plus their full pre-drag
	// transforms. Every handle records position, rotation AND scale even
	// though its own mode only reads one of them -- that is what lets
	// EndGizmoDrag() build an undo action without caring which handle ran.
	void BeginGizmoDrag(const int axis_index, const kbManipulator::manipulatorMode_t mode, const std::vector<kbEditorEntity*>& selected, const Vec3& origin);

	// Viewport click-to-select. Issues a GPU entity-id pick for the clicked
	// pixel and, on a later frame, turns the id the renderer read back into a
	// selection. Replaces the D3D11-era pick that used to live in
	// render_sync() -- see that function's comment.
	void UpdateViewportPicking();

	// Shared drag-end. Pushes exactly ONE kbUndoTransformEntities for the
	// whole drag -- the drag itself mutates transforms every frame, so the
	// push belongs here and nowhere near the per-frame set_position() calls.
	// A grab that never actually moved anything (a click on a handle with no
	// drag) pushes nothing, so it can't evict a real action from the 15-deep
	// stack.
	void EndGizmoDrag();

	kbManipulator& GetManipulator() { return m_Manipulator; }

	kbCamera m_Camera;

	//
	const kbModel* m_pCurrentlySelectedResource;

	kbManipulator m_Manipulator;

	float m_CameraMoveSpeedMultiplier;

	// True between issuing a pick request and consuming its result, so a click
	// can't queue a second pick while one is outstanding. Also carries whether
	// Ctrl was held at *click* time -- the result arrives a frame or more
	// later, by which point the key may have been released.
	bool m_bPickPending = false;
	bool m_bPickAppendToSelection = false;

	bool m_bGizmoDragging = false;
	int m_GizmoDragAxis = -1;
	kbManipulator::manipulatorMode_t m_GizmoDragMode = kbManipulator::Translate;

	Vec3 m_GizmoGrabWorldPoint;   // translate/scale: camera-plane reference point
	Vec3 m_GizmoGrabAngleVec;     // rotate: initial origin->hit vector, for signed-angle delta
	f32 m_GizmoGrabCenterDist = 0.0f; // scale center handle: reference distance at grab time

	// Pre-drag snapshot, captured by BeginGizmoDrag() and read back by
	// EndGizmoDrag() as the "before" half of the undo action. m_GizmoGrabEntities
	// is what binds the snapshot to specific entities: the drag applies to the
	// entities that were selected when it *started*, so drag-end must not go
	// re-read a selection that may have changed underneath it.
	std::vector<kbEditorEntity*> m_GizmoGrabEntities;
	std::vector<Vec3> m_GizmoGrabPositions;
	std::vector<Quat4> m_GizmoGrabRotations;
	std::vector<Vec3> m_GizmoGrabScales;
};
