/// viewport_panel.h
///
/// 2016 blk
#pragma once

#include "render_defs.h"
#include "editor_panel.h"
#include "camera.h"
#include "kbManipulator.h"

class kbEditorEntity;

/// ViewportPanel
///
/// The editor's 3D view: owns a camera, the T/R/S gizmo and click-to-select.
/// Was `kbMainTab` until Phase 3 -- an Fl_Tabs hosting one real viewport plus
/// two that were never rendered into (Milestone 7 collapsed those), which then
/// stopped being a window of its own when kbEditorWindow was deleted
/// (Milestone 8, and what makes kbEditor's HWND the one ImGui initializes
/// against). The name is all that was still describing the old shape.
///
/// ## On more than one viewport
///
/// Nothing here assumes it is the only instance -- the camera and the
/// projection parameters are per-instance, and every screen-space calculation
/// goes through viewport_rect() rather than reading io.DisplaySize. That is
/// deliberate groundwork: a second viewport (a 4-up level view, or an
/// asset-preview panel) needs no changes to the gizmo or the picking maths.
///
/// What still blocks it is entirely below this layer, and none of it is
/// cheap:
///   - Renderer_Dx12's render targets are indexed by frame only
///     (m_render_targets[ERenderTarget::Count][max_frames()]), so a second
///     view running the gbuffer pass overwrites the first's.
///   - Renderer::render() builds exactly one camera from the global
///     set_camera_transform(); the render GRAPH already iterates N
///     ViewContexts (RenderPassDecl::per_view), so the topology is ready even
///     though the entry point is not.
///   - There is no render-to-texture path, so a second viewport has nowhere
///     to draw: this one works only because the scene goes straight to the
///     backbuffer and shows through the dockspace's transparent central node.
///   - An asset-preview viewport (Unreal's PhAT / Static Mesh Editor) needs a
///     separate scene to render, not just a second camera.
class ViewportPanel : public EditorPanel {
	friend class kbEditor;

public:
	ViewportPanel(int x, int y, int w, int h);

	virtual void update(const f32 dt);
	virtual void draw_imgui() override;

	virtual void EventCB(const widgetCBObject* const widgetCBObject);

	// const_cast because callers mutate the camera through this from const
	// methods (UpdateFreeDrag/UpdateAxisDrag). The old kbEditorWindow* member
	// indirection did the same thing implicitly -- constness simply never
	// propagated through the pointer.
	kbCamera* GetEditorWindowCamera() const { return const_cast<kbCamera*>(&m_Camera); }

	void SetCameraSpeedMultiplier(const float newMultiplier) { m_CameraMoveSpeedMultiplier = max(min(newMultiplier, 100.0f), 0.1f); }

	// This viewport's area in ImGui's logical/hit-test space (the same space
	// io.MousePos uses). Today it is the whole display, because the scene is
	// drawn straight to the backbuffer behind a passthrough dockspace -- but
	// every gizmo and picking calculation asks for it here rather than reading
	// io.DisplaySize directly, so a viewport that occupies part of the window
	// needs no changes to any of that maths. Returns false when there is no
	// sensible rect yet (zero display size), which callers treat as "skip this
	// frame".
	//
	// Vec2 rather than ImVec2 on purpose: this header is ImGui-free and stays
	// that way, so including it doesn't drag ImGui into every translation unit
	// (same reason ImGuiDescriptorHeapAllocator is kept ImGui-header-free).
	bool viewport_rect(Vec2& out_pos, Vec2& out_size) const;

	// Per-viewport projection. Mirrors what Renderer::render() feeds
	// make_render_camera() today; held per instance rather than as file-scope
	// constants so a second viewport can differ (a top/front/side view would
	// want an orthographic projection here).
	f32 fov_radians() const { return m_FovRadians; }
	f32 near_clip() const { return m_NearClip; }
	f32 far_clip() const { return m_FarClip; }

private:
	void InputCB(const widgetCBObject* const widgetCBObject);
	void CameraMoveCB(const widgetCBInputObject* const widgetCBObject);

	// Builds the RenderCamera the gizmo projects through, from this viewport's
	// own camera and projection parameters.
	//
	// The aspect comes from the RENDERER (Renderer::render_aspect_ratio()), not
	// from viewport_rect(). That looks wrong and isn't: the scene is rendered
	// at the swapchain's fixed size and then stretched to the client area, so
	// an overlay has to project exactly as render() did and let the same
	// stretch carry it -- projecting with the viewport's own aspect would slide
	// the gizmo off the geometry it is supposed to sit on. When a viewport
	// renders into its own correctly-sized target, this becomes that target's
	// aspect and the distinction disappears.
	RenderCamera make_viewport_camera() const;

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

	// Per-viewport projection -- see fov_radians()/near_clip()/far_clip().
	// These were file-scope constants in viewport_panel.cpp duplicated from
	// Renderer::render()'s g_fov/g_near_clip_plane/g_far_clip_plane, which was
	// flagged there as needing a shared home; per-instance members are that
	// home, and they keep a second viewport free to project differently.
	f32 m_FovRadians = 0.0f;
	f32 m_NearClip = 1.0f;
	f32 m_FarClip = 20000.0f;

	//
	const kbModel* m_pCurrentlySelectedResource;

	// Reduced to a T/R/S mode holder: every other member of kbManipulator is
	// reachable only from dead code now (its models are never loaded in the
	// D3D12 build) and the ImGui gizmo below replaced its mouse grab. Worth
	// collapsing to a plain enum once the toolbar's axis-nudge buttons -- the
	// last non-gizmo consumer -- are removed.
	kbManipulator m_Manipulator;

	float m_CameraMoveSpeedMultiplier;

	// True between issuing a pick request and consuming its result, so a click
	// can't queue a second pick while one is outstanding. Also carries whether
	// Ctrl was held at *click* time -- the result arrives a frame or more
	// later, by which point the key may have been released.
	bool m_bPickPending = false;
	bool m_bPickAppendToSelection = false;

	// This frame's viewport rect, cached by DrawGizmo() so the handle-drawing
	// and hit-testing helpers can project through it without threading it
	// through seven signatures. Per-instance, so each viewport would cache its
	// own.
	Vec2 m_ViewportPos;
	Vec2 m_ViewportSize;

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
