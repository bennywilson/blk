/// viewport_panel.h
///
/// 2016 blk

#pragma once

#include "editor_panel.h"
#include "kbManipulator.h"

class kbEditorEntity;

/// ViewportPanel
///
/// Owns the editor camera, the T/R/S gizmo, and click-to-select. Routes all screen math through viewport_rect(),
/// so a second instance needs no gizmo or picking changes.
/// TODO: More viewports need per-view render targets, a multi-camera Renderer::render(), and a render-to-texture path.
class ViewportPanel : public EditorPanel {
	friend class kbEditor;

public:
	ViewportPanel();

	virtual void update(const f32 dt) override;
	virtual void draw_imgui() override;

	virtual void EventCB(const widgetCBObject* const widget_cb_object) override;

	// Const so const methods like UpdateFreeDrag() can read the camera. Non-const result so kbEditor's forwarders can move it.
	kbCamera* GetEditorWindowCamera() const { return const_cast<kbCamera*>(&m_Camera); }

	void SetCameraSpeedMultiplier(const float newMultiplier) { m_CameraMoveSpeedMultiplier = max(min(newMultiplier, 100.0f), 0.1f); }

	// Returns this viewport's rect in io.MousePos space, currently the whole display. Returns false before the display has a size.
	// Takes Vec2, not ImVec2, so this header stays ImGui-free.
	bool viewport_rect(Vec2& out_pos, Vec2& out_size) const;

	// Per-instance projection. Must match Renderer::render()'s, or the gizmo slides off the geometry.
	f32 fov_radians() const { return m_FovRadians; }
	f32 near_clip() const { return m_NearClip; }
	f32 far_clip() const { return m_FarClip; }

private:
	void InputCB(const widgetCBObject* const widget_cb_object);
	void CameraMoveCB(const widgetCBInputObject* const inputObject);

	// Uses the renderer's aspect, not viewport_rect()'s, because the scene renders at the swapchain's fixed size and stretches to the client area.
	// The gizmo must project the same way to stay on the geometry.
	RenderCamera make_viewport_camera() const;

	// Marks every visible entity and each light while Toggle Icons (g_bBillboardsEnabled) is on.
	void DrawEntityIcons();

	// Draws and hit-tests the world-space gizmo in screen space instead of kbManipulator::AttemptMouseGrab(), whose models are never loaded.
	// TODO: The toolbar axis-nudge buttons push no undo action, unlike gizmo drags.
	void DrawGizmo();
	void DrawTranslateAxis(const int axis_index, const std::vector<kbEditorEntity*>& selected, const Vec3& origin, const RenderCamera& render_camera);
	void DrawScaleAxis(const int axis_index, const std::vector<kbEditorEntity*>& selected, const Vec3& origin, const RenderCamera& render_camera);
	void DrawRotateRing(const int axis_index, const std::vector<kbEditorEntity*>& selected, const Vec3& origin, const RenderCamera& render_camera);

	// Center handle: free move in Translate, uniform scale in Scale. Shares drag state through the sentinel kGizmoCenterAxisIndex.
	void DrawTranslateCenter(const std::vector<kbEditorEntity*>& selected, const Vec3& origin, const RenderCamera& render_camera);
	void DrawScaleCenter(const std::vector<kbEditorEntity*>& selected, const Vec3& origin, const RenderCamera& render_camera);

	// Returns the signed distance moved along axis_index, from the mouse ray's hit on the camera-facing plane through the grab point.
	bool UpdateAxisDrag(const int axis_index, const RenderCamera& render_camera, f32& out_delta) const;

	// Returns the unconstrained world-space delta on the same plane, for the center handle.
	bool UpdateFreeDrag(const RenderCamera& render_camera, Vec3& out_delta) const;

	// Latches drag state and snapshots every selected entity's full pre-drag T/R/S,
	// so EndGizmoDrag() can build the undo action without knowing which handle ran.
	void BeginGizmoDrag(const int axis_index, const kbManipulator::manipulatorMode_t mode, const std::vector<kbEditorEntity*>& selected, const Vec3& origin);

	// Issues a GPU entity-id pick for the clicked pixel and turns the id read back on a later frame into a selection.
	void UpdateViewportPicking();

	// Pushes one kbUndoTransformEntities per drag, never per frame. A grab that moved nothing pushes nothing,
	// so it can't evict a real action from the 15-deep undo stack.
	void EndGizmoDrag();

	kbManipulator& GetManipulator() { return m_Manipulator; }

	kbCamera m_Camera;

	f32 m_FovRadians = 0.0f;
	f32 m_NearClip = 1.0f;
	f32 m_FarClip = 20000.0f;

	const kbModel* m_pCurrentlySelectedResource;

	// Holds only the T/R/S mode, since the ImGui gizmo replaced its mouse grab.
	// TODO: Collapse to an enum once the toolbar axis-nudge buttons go.
	kbManipulator m_Manipulator;

	float m_CameraMoveSpeedMultiplier;

	// Blocks a second pick while one is in flight, and records Ctrl at click time, since the result arrives frames later.
	bool m_bPickPending = false;
	bool m_bPickAppendToSelection = false;

	// This frame's viewport rect, cached by DrawGizmo() for the handle helpers.
	Vec2 m_ViewportPos;
	Vec2 m_ViewportSize;

	bool m_bGizmoDragging = false;
	int m_GizmoDragAxis = -1;
	kbManipulator::manipulatorMode_t m_GizmoDragMode = kbManipulator::Translate;

	Vec3 m_GizmoGrabWorldPoint;   // translate/scale: camera-plane reference point
	Vec3 m_GizmoGrabAngleVec;     // rotate: initial origin->hit vector, for signed-angle delta
	f32 m_GizmoGrabCenterDist = 0.0f; // scale center handle: reference distance at grab time

	// Pre-drag snapshot from BeginGizmoDrag(). Binds to the entities selected at drag start, since the selection can change mid-drag.
	std::vector<kbEditorEntity*> m_GizmoGrabEntities;
	std::vector<Vec3> m_GizmoGrabPositions;
	std::vector<Quat4> m_GizmoGrabRotations;
	std::vector<Vec3> m_GizmoGrabScales;
};
