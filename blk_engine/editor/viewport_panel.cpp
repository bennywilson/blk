/// viewport_panel.cpp
///
/// 2016 blk

#include "blk_core.h"
#include "Matrix.h"
#include "Quaternion.h"
#include "editor_panel.h"
#include "kbEditor.h"
#include "model.h"
#include "entity_header.h"
#include "kbEditorEntity.h"
#include "kbManipulator.h"
#include "renderer.h"
#include "imgui.h"

#include "viewport_panel.h"

kbModel* model = nullptr;
const f32 Base_Cam_Speed = 100.f;

/// WorldToScreen
///
/// Projects a world position through view_projection into the viewport rect,
/// returning a point in the same window-relative logical pixel space
/// io.MousePos uses -- so every hit-test below can compare against the mouse
/// directly. Takes the viewport's rect rather than io.DisplaySize so a
/// viewport occupying only part of the window projects correctly; with a
/// full-window viewport, viewport_pos is (0,0) and this is what it always was.
/// Returns false for a point behind the camera.
static bool WorldToScreen(const Vec3& world_pos, const Mat4& view_projection, const Vec2& viewport_pos, const Vec2& viewport_size, ImVec2& out_screen) {
	const Vec4 clip = Vec4(world_pos, 1.0f).transform_point(view_projection, false);
	if (clip.w < 0.0001f) {
		return false;
	}

	const f32 ndc_x = clip.x / clip.w;
	const f32 ndc_y = clip.y / clip.w;
	out_screen.x = viewport_pos.x + (ndc_x * 0.5f + 0.5f) * viewport_size.x;
	out_screen.y = viewport_pos.y + (1.0f - (ndc_y * 0.5f + 0.5f)) * viewport_size.y;
	return true;
}

/// ScreenToRay
///
/// Unprojects a screen point into a world-space ray via
/// RenderCamera::inv_view_projection_matrix. screen_pos and viewport_size are
/// both in the viewport's own space, so this works for a viewport that is not
/// the whole window.
static void ScreenToRay(const ImVec2& screen_pos, const RenderCamera& camera, const Vec2& viewport_pos, const Vec2& viewport_size, Vec3& out_origin, Vec3& out_dir) {
	// screen_pos is window-relative (io.MousePos); shift it into the viewport
	// before normalising -- the inverse of what WorldToScreen above does.
	const Vec4 ndc_far(
		(((screen_pos.x - viewport_pos.x) / viewport_size.x) * 2.0f) - 1.0f,
		1.0f - (((screen_pos.y - viewport_pos.y) / viewport_size.y) * 2.0f),
		1.0f,
		1.0f
	);
	const Vec4 world_far = ndc_far.transform_point(camera.inv_view_projection_matrix, true);

	out_origin = camera.view_position;
	out_dir = world_far.ToVec3() - camera.view_position;
}

/// DistancePointToSegment
static f32 DistancePointToSegment(const ImVec2& p, const ImVec2& a, const ImVec2& b) {
	const ImVec2 ab(b.x - a.x, b.y - a.y);
	const f32 len_sqr = ab.x * ab.x + ab.y * ab.y;

	f32 t = 0.0f;
	if (len_sqr > 0.0001f) {
		t = ((p.x - a.x) * ab.x + (p.y - a.y) * ab.y) / len_sqr;
		t = max(0.0f, min(1.0f, t));
	}

	const ImVec2 closest(a.x + ab.x * t, a.y + ab.y * t);
	const f32 dx = p.x - closest.x;
	const f32 dy = p.y - closest.y;
	return sqrtf(dx * dx + dy * dy);
}

/// RayPlaneIntersect
static bool RayPlaneIntersect(const Vec3& ray_origin, const Vec3& ray_dir, const Vec3& plane_point, const Vec3& plane_normal, Vec3& out_point) {
	const f32 denom = ray_dir.dot(plane_normal);
	if (fabsf(denom) < 0.0001f) {
		return false;
	}

	const f32 t = (plane_point - ray_origin).dot(plane_normal) / denom;
	out_point = ray_origin + ray_dir * t;
	return true;
}

/// QuatsEqual
///
/// Quat4 has no compare()/operator== of its own the way Vec3 does. This only
/// has to answer "did the drag actually rotate anything", so a component-wise
/// epsilon test against the same 0.0001f Vec3::compare() uses is enough -- no
/// need to treat q and -q as equal, since a drag that rotated nothing leaves
/// the exact components it started with.
static bool QuatsEqual(const Quat4& a, const Quat4& b) {
	const f32 epsilon = 0.0001f;
	return fabsf(a.x - b.x) < epsilon && fabsf(a.y - b.y) < epsilon && fabsf(a.z - b.z) < epsilon && fabsf(a.w - b.w) < epsilon;
}

// World-space gizmo axes/colors -- X=red, Y=green, Z=blue, matching the
// usual translate/scale/rotate handle convention. Yellow marks whichever
// handle is currently hovered or being dragged.
static const Vec3 g_GizmoAxisDirs[3] = { Vec3(1.0f, 0.0f, 0.0f), Vec3(0.0f, 1.0f, 0.0f), Vec3(0.0f, 0.0f, 1.0f) };
static const ImU32 g_GizmoAxisColors[3] = { IM_COL32(220, 40, 40, 255), IM_COL32(40, 200, 40, 255), IM_COL32(60, 120, 240, 255) };
static const ImU32 g_GizmoHighlightColor = IM_COL32(255, 255, 0, 255);
static const ImU32 g_GizmoCenterColor = IM_COL32(230, 230, 230, 255);
static const int kGizmoCenterAxisIndex = 3;

/// ViewportPanel::ViewportPanel
ViewportPanel::ViewportPanel(int x, int y, int w, int h) :
	EditorPanel(x, y, w, h) {

	// Phase 3, Milestone 8: no viewport window is created here any more. The
	// viewport is kbEditor's own window (it already filled the whole client
	// area after Milestone 7), so all that remains of kbEditorWindow is the
	// m_Camera member.

	// register this widget with the editor
	g_Editor->RegisterUpdate(this);
	g_Editor->RegisterEvent(this, WidgetCB_Input);
	g_Editor->RegisterEvent(this, WidgetCB_TranslationButtonPressed);
	g_Editor->RegisterEvent(this, WidgetCB_RotationButtonPressed);
	g_Editor->RegisterEvent(this, WidgetCB_ScaleButtonPressed);
	g_Editor->RegisterEvent(this, WidgetCB_EntitySelected);

	m_CameraMoveSpeedMultiplier = 1.0f;
	m_pCurrentlySelectedResource = nullptr;
	m_FovRadians = kbToRadians(80.0f);
}

/// ViewportPanel::viewport_rect
bool ViewportPanel::viewport_rect(Vec2& out_pos, Vec2& out_size) const {
	const ImGuiIO& io = ImGui::GetIO();
	if (io.DisplaySize.x <= 0.0f || io.DisplaySize.y <= 0.0f) {
		return false;
	}

	// Whole display today: the 3D scene is drawn straight to the backbuffer and
	// shows through the dockspace's PassthruCentralNode, so this viewport has
	// no window of its own to measure. A viewport hosted inside an ImGui window
	// would return its content region here instead, and nothing else in this
	// file would have to change.
	out_pos.set(0.0f, 0.0f);
	out_size.set(io.DisplaySize.x, io.DisplaySize.y);
	return true;
}

/// ViewportPanel::make_viewport_camera
RenderCamera ViewportPanel::make_viewport_camera() const {
	// See the declaration for why the aspect is the renderer's and not this
	// viewport's. Replaces the 1920/1080 that used to be hardcoded here
	// alongside copies of g_fov/g_near_clip_plane/g_far_clip_plane, with the
	// note that they wanted a shared accessor -- Renderer now has one.
	const f32 aspect = g_renderer ? g_renderer->render_aspect_ratio() : (1920.0f / 1080.0f);
	return make_render_camera(m_Camera.m_position, m_Camera.m_rotation, m_FovRadians, aspect, m_NearClip, m_FarClip);
}

/// ViewportPanel::update
void ViewportPanel::update(const f32 dt) {
	if (g_Editor->IsRunningGame()) {
		return;
	}

	if (!g_Editor->main_viewport_hwnd()) {
		return;
	}

	const kbCamera& pCamera = m_Camera;

	// 'V' cycles the camera speed. Viewport input, so it lives here rather than
	// in kbEditor::Update() -- though the binding table and the selected index
	// stay on kbEditor, being editor state persisted into the level.
	{
		static bool bSpeedKeyWasDown = false;
		const bool bSpeedKeyDown = (GetAsyncKeyState('V') & 0x8000) != 0;
		if (bSpeedKeyDown && !bSpeedKeyWasDown) {
			g_Editor->SetCamSpeedIndex((g_Editor->cam_speed_index() + 1) % kbEditor::NumCamSpeedBindings());
		}
		bSpeedKeyWasDown = bSpeedKeyDown;
	}

	// THE single-viewport chokepoint. Renderer::render() builds one camera from
	// this global transform, so with more than one ViewportPanel the last one
	// to update() would silently win. When multi-viewport arrives this becomes
	// "publish my ViewContext" and the renderer takes a list -- the render
	// graph already iterates N of them (RenderPassDecl::per_view).
	if (g_renderer) {
		g_renderer->set_camera_transform(pCamera.m_position, pCamera.m_rotation);
	}

	{
		for (int i = 0; i < g_Editor->GetGameEntities().size(); i++) {
			const kbEditorEntity* const pCurrentEntity = g_Editor->GetGameEntities()[i];
			const GameEntity* const pGameEntity = pCurrentEntity->GetGameEntity();

			int iconIdx = 1;

			for (int j = 0; j < pGameEntity->num_components(); j++) {

				const kbComponent* const pCurrentComponent = pGameEntity->component(j);

				extern bool g_bBillboardsEnabled;
				if (g_bBillboardsEnabled && (pCurrentComponent->IsA(kbDirectionalLightComponent::GetType()) || pCurrentComponent->IsA(kbLightShaftsComponent::GetType()))) {

					const Mat4 rotationMatrix = pGameEntity->rotation().to_mat4();
					const Vec3 lightDirection = Vec3(0, 0, 1.0f) * rotationMatrix;

					for (float x = -1.0f; x <= 1.0f; x += 1.0f) {
						for (float y = -1.0f; y <= 1.0f; y += 1.0f) {
							const Vec3 lightPosition = Vec3(x, y, 0.0f) * rotationMatrix;
						//	g_pRenderer->DrawLine(pGameEntity->position() + lightPosition, pGameEntity->position() + lightPosition + lightDirection * 3.0f, kbColor(0.43f, 0.2f, 0.43f, 1.0f));
						}
					}

					iconIdx = 2;
					break;
				}
			}

		//	g_pRenderer->DrawBillboard(pCurrentEntity->position(), Vec2(1.0f, 1.0f), iconIdx, nullptr, pCurrentEntity->GetGameEntity()->GetEntityId());

			/*if (pCurrentEntity->IsSelected() && g_pRenderer->DebugBillboardsEnabled()) {
				g_pRenderer->DrawBox(pCurrentEntity->GetWorldBounds(), kbColor::yellow);

				m_Manipulator.Update();
			}*/
		}
	}

	m_Camera.Update();

}

/// ViewportPanel::draw_imgui
void ViewportPanel::draw_imgui() {
	DrawGizmo();

	// After DrawGizmo(), never before: a click that lands on a gizmo handle
	// belongs to the drag, and DrawGizmo() is what decides that by setting
	// m_bGizmoDragging. Picking reads that flag to stay out of the way.
	UpdateViewportPicking();
}

/// ViewportPanel::UpdateViewportPicking
///
/// The click-to-select the deleted D3D11-era render_sync() used to hold,
/// rebuilt for D3D12. The renderer writes each pixel's owning entity id into
/// ERenderTarget::EntityId during the gbuffer pass; this asks it to read one
/// pixel back and selects whatever entity that names.
void ViewportPanel::UpdateViewportPicking() {
	if (!g_renderer) {
		return;
	}

	// Results first: a pick issued on an earlier frame may have landed, and
	// consuming it before issuing a new one keeps at most one in flight.
	u32 picked_entity_id = Renderer::invalid_entity_id();
	if (g_renderer->try_take_entity_id_pick(picked_entity_id)) {
		m_bPickPending = false;

		std::vector<kbEditorEntity*> newly_selected;
		if (picked_entity_id != Renderer::invalid_entity_id()) {
			for (kbEditorEntity* const entity : g_Editor->GetGameEntities()) {
				const GameEntity* const game_entity = entity->GetGameEntity();
				if (game_entity && game_entity->GetEntityId() == picked_entity_id) {
					newly_selected.push_back(entity);
					break;
				}
			}
		}

		if (newly_selected.empty()) {
			// Clicking empty space clears the selection, except while
			// Ctrl-clicking to extend one.
			if (!m_bPickAppendToSelection) {
				g_Editor->DeselectEntities();
			}
		} else {
			// SelectEntities() pushes its own kbUndoSelectActor, so picking is
			// undoable without anything extra here.
			g_Editor->SelectEntities(newly_selected, m_bPickAppendToSelection);
		}
	}

	if (m_bPickPending || m_bGizmoDragging) {
		return;
	}

	const ImGuiIO& io = ImGui::GetIO();
	if (io.WantCaptureMouse || !ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
		return;
	}

	Vec2 viewport_pos, viewport_size;
	if (!viewport_rect(viewport_pos, viewport_size)) {
		return;
	}

	// Mouse relative to this viewport, not to the window. Identical while the
	// viewport fills the display; the subtraction is what makes a viewport
	// hosted inside a panel work without touching anything else here.
	const f32 local_x = io.MousePos.x - viewport_pos.x;
	const f32 local_y = io.MousePos.y - viewport_pos.y;
	if (local_x < 0.0f || local_y < 0.0f || local_x >= viewport_size.x || local_y >= viewport_size.y) {
		return;
	}

	// Viewport-local logical pixels -> the EntityId target's backbuffer pixels.
	// DisplayFramebufferScale is exactly that ratio for a full-window viewport:
	// Renderer_Dx12::render_ui_overlay() sets it to m_frame_width/DisplaySize
	// each frame for this same reason. A partial-window viewport rendering into
	// its own target would scale by that target's size instead.
	const f32 backbuffer_x = local_x * io.DisplayFramebufferScale.x;
	const f32 backbuffer_y = local_y * io.DisplayFramebufferScale.y;

	m_bPickPending = true;
	m_bPickAppendToSelection = io.KeyCtrl;
	g_renderer->request_entity_id_pick((u32)backbuffer_x, (u32)backbuffer_y);
}

/// ViewportPanel::DrawGizmo
void ViewportPanel::DrawGizmo() {
	// GetSelectedObjects() can hold a dangling kbEditorEntity* in the window
	// between a level unload/entity delete and the selection list itself
	// being cleared -- same class of bug PropertiesPanel::draw_imgui() had
	// to guard against (see its "dangling pointer, not nullptr" comment).
	// Filter against the live entity list before dereferencing anything.
	const std::vector<kbEditorEntity*>& live_entities = g_Editor->GetGameEntities();
	std::vector<kbEditorEntity*> selected;
	for (kbEditorEntity* const entity : g_Editor->GetSelectedObjects()) {
		if (std::find(live_entities.begin(), live_entities.end(), entity) != live_entities.end()) {
			selected.push_back(entity);
		}
	}

	if (selected.empty()) {
		// A drag in progress when the selection empties out (deleted, or
		// deselected mid-drag) has still moved something -- end it properly so
		// that movement is undoable, rather than dropping the snapshot. If the
		// entities are actually gone, EndGizmoDrag()'s liveness filter finds
		// nothing changed and pushes nothing.
		EndGizmoDrag();
		return;
	}

	// Cached for this frame so the Draw*/UpdateDrag helpers below can project
	// through the viewport without each taking it as a parameter.
	if (!viewport_rect(m_ViewportPos, m_ViewportSize)) {
		return;
	}

	const RenderCamera render_camera = make_viewport_camera();

	Vec3 origin(0.0f, 0.0f, 0.0f);
	for (const kbEditorEntity* const entity : selected) {
		origin += entity->position();
	}
	origin /= (f32)selected.size();

	const kbManipulator::manipulatorMode_t mode = m_Manipulator.GetMode();

	// A drag started under a different T/R/S mode (e.g. the mode button was
	// clicked while a drag was somehow still active) would keep applying its
	// deltas through the new mode's handles -- stop rather than apply garbage.
	// Whatever it moved before the mode changed stays undoable.
	if (m_bGizmoDragging && mode != m_GizmoDragMode) {
		EndGizmoDrag();
	}

	// Center handle checked/claimed before the axis handles: all 3 axis
	// lines start exactly at origin, so near the center an axis line's own
	// hit-test also matches within its threshold. Processing the center
	// handle first means it sets m_bGizmoDragging on a claiming click before
	// the axis loop below runs, so the axis handles see a drag already in
	// progress (for a different axis index) and skip starting their own.
	if (mode == kbManipulator::Translate) {
		DrawTranslateCenter(selected, origin, render_camera);
	} else if (mode == kbManipulator::Scale) {
		DrawScaleCenter(selected, origin, render_camera);
	}

	for (int axis = 0; axis < 3; axis++) {
		if (mode == kbManipulator::Translate) {
			DrawTranslateAxis(axis, selected, origin, render_camera);
		} else if (mode == kbManipulator::Scale) {
			DrawScaleAxis(axis, selected, origin, render_camera);
		} else if (mode == kbManipulator::Rotate) {
			DrawRotateRing(axis, selected, origin, render_camera);
		}
	}
}

/// ViewportPanel::UpdateFreeDrag
bool ViewportPanel::UpdateFreeDrag(const RenderCamera& render_camera, Vec3& out_delta) const {
	const ImGuiIO& io = ImGui::GetIO();

	Vec3 ray_origin, ray_dir;
	ScreenToRay(io.MousePos, render_camera, m_ViewportPos, m_ViewportSize, ray_origin, ray_dir);

	// Intersect with the camera-facing plane through the grab point -- same
	// technique kbManipulator::UpdateMouseDrag() uses for its axis handles,
	// just without the projection onto a single axis.
	const kbCamera& camera = *GetEditorWindowCamera();
	const Vec3 camera_forward = camera.m_rotation.to_mat4()[2].ToVec3();

	Vec3 plane_hit;
	if (!RayPlaneIntersect(ray_origin, ray_dir, m_GizmoGrabWorldPoint, camera_forward, plane_hit)) {
		return false;
	}

	out_delta = plane_hit - m_GizmoGrabWorldPoint;
	return true;
}

/// ViewportPanel::BeginGizmoDrag
void ViewportPanel::BeginGizmoDrag(const int axis_index, const kbManipulator::manipulatorMode_t mode, const std::vector<kbEditorEntity*>& selected, const Vec3& origin) {
	m_bGizmoDragging = true;
	m_GizmoDragAxis = axis_index;
	m_GizmoDragMode = mode;

	// Rotate doesn't use this (its reference is m_GizmoGrabAngleVec, set by
	// the caller once its ray/plane hit succeeds), but setting it uniformly
	// keeps one drag-start path instead of one per mode.
	m_GizmoGrabWorldPoint = origin;

	m_GizmoGrabEntities.clear();
	m_GizmoGrabPositions.clear();
	m_GizmoGrabRotations.clear();
	m_GizmoGrabScales.clear();
	for (kbEditorEntity* const entity : selected) {
		m_GizmoGrabEntities.push_back(entity);
		m_GizmoGrabPositions.push_back(entity->position());
		m_GizmoGrabRotations.push_back(entity->rotation());
		m_GizmoGrabScales.push_back(entity->scale());
	}
}

/// ViewportPanel::EndGizmoDrag
void ViewportPanel::EndGizmoDrag() {
	const bool was_dragging = m_bGizmoDragging;
	m_bGizmoDragging = false;

	if (!was_dragging) {
		return;
	}

	// Same dangling-pointer guard DrawGizmo() applies to the raw selection:
	// an entity grabbed at drag-start can have been deleted before this runs
	// (the selection-emptied path above is reached exactly that way), so read
	// the "after" transforms only from entities still in the editor's list.
	const std::vector<kbEditorEntity*>& live_entities = g_Editor->GetGameEntities();

	std::vector<kbEditorEntity*> moved_entities;
	std::vector<kbUndoTransformEntities::EntityTransform_t> before_transforms;
	std::vector<kbUndoTransformEntities::EntityTransform_t> after_transforms;

	for (size_t i = 0; i < m_GizmoGrabEntities.size(); i++) {
		kbEditorEntity* const entity = m_GizmoGrabEntities[i];
		if (std::find(live_entities.begin(), live_entities.end(), entity) == live_entities.end()) {
			continue;
		}

		kbUndoTransformEntities::EntityTransform_t before;
		before.m_position = m_GizmoGrabPositions[i];
		before.m_rotation = m_GizmoGrabRotations[i];
		before.m_scale = m_GizmoGrabScales[i];

		kbUndoTransformEntities::EntityTransform_t after;
		after.m_position = entity->position();
		after.m_rotation = entity->rotation();
		after.m_scale = entity->scale();

		// A click that grabbed a handle without dragging leaves the transform
		// bit-identical. Skipping those keeps no-op clicks from evicting real
		// actions out of the 15-deep undo stack.
		if (before.m_position.compare(after.m_position) && QuatsEqual(before.m_rotation, after.m_rotation) && before.m_scale.compare(after.m_scale)) {
			continue;
		}

		moved_entities.push_back(entity);
		before_transforms.push_back(before);
		after_transforms.push_back(after);
	}

	m_GizmoGrabEntities.clear();
	m_GizmoGrabPositions.clear();
	m_GizmoGrabRotations.clear();
	m_GizmoGrabScales.clear();

	if (moved_entities.empty()) {
		return;
	}

	g_Editor->PushUndoAction(new kbUndoTransformEntities(moved_entities, before_transforms, after_transforms));
}

/// ViewportPanel::UpdateAxisDrag
bool ViewportPanel::UpdateAxisDrag(const int axis_index, const RenderCamera& render_camera, f32& out_delta) const {
	Vec3 free_delta;
	if (!UpdateFreeDrag(render_camera, free_delta)) {
		return false;
	}

	out_delta = free_delta.dot(g_GizmoAxisDirs[axis_index]);
	return true;
}

/// ViewportPanel::DrawTranslateAxis
///
/// Every gizmo handle draws into ImGui's *background* draw list, not the
/// foreground one: the handles belong over the 3D scene but under the editor's
/// panels. The foreground list draws on top of every window, so the gizmo bled
/// over the Outliner/Properties/Resources panels whenever the selected entity
/// projected behind one. Hit-testing is already gated on !io.WantCaptureMouse,
/// so the two agree about which clicks belong to the gizmo.
void ViewportPanel::DrawTranslateAxis(const int axis_index, const std::vector<kbEditorEntity*>& selected, const Vec3& origin, const RenderCamera& render_camera) {
	const kbCamera& camera = *GetEditorWindowCamera();
	const ImGuiIO& io = ImGui::GetIO();

	const Vec3& axis_dir = g_GizmoAxisDirs[axis_index];
	const f32 dist_to_camera = (origin - camera.m_position).length();
	const f32 handle_length = max(dist_to_camera * 0.15f, 1.0f);
	const Vec3 axis_tip = origin + axis_dir * handle_length;

	ImVec2 origin_screen, tip_screen;
	if (!WorldToScreen(origin, render_camera.view_projection_matrix, m_ViewportPos, m_ViewportSize, origin_screen)) {
		return;
	}
	if (!WorldToScreen(axis_tip, render_camera.view_projection_matrix, m_ViewportPos, m_ViewportSize, tip_screen)) {
		return;
	}

	ImDrawList* const draw_list = ImGui::GetBackgroundDrawList();
	const bool this_axis_dragging = m_bGizmoDragging && m_GizmoDragAxis == axis_index;
	const bool hovered = !m_bGizmoDragging && !io.WantCaptureMouse && DistancePointToSegment(io.MousePos, origin_screen, tip_screen) < 8.0f;
	const ImU32 color = (this_axis_dragging || hovered) ? g_GizmoHighlightColor : g_GizmoAxisColors[axis_index];

	draw_list->AddLine(origin_screen, tip_screen, color, this_axis_dragging ? 4.0f : 3.0f);
	draw_list->AddCircleFilled(tip_screen, 5.0f, color);

	if (!m_bGizmoDragging) {
		if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
			BeginGizmoDrag(axis_index, kbManipulator::Translate, selected, origin);
		}
		return;
	}

	if (!this_axis_dragging) {
		return;
	}

	if (!ImGui::IsMouseDown(ImGuiMouseButton_Left) || m_GizmoGrabPositions.size() != selected.size()) {
		EndGizmoDrag();
		return;
	}

	f32 delta = 0.0f;
	if (UpdateAxisDrag(axis_index, render_camera, delta)) {
		for (size_t i = 0; i < selected.size(); i++) {
			selected[i]->set_position(m_GizmoGrabPositions[i] + axis_dir * delta);
		}
	}
}

/// ViewportPanel::DrawScaleAxis
void ViewportPanel::DrawScaleAxis(const int axis_index, const std::vector<kbEditorEntity*>& selected, const Vec3& origin, const RenderCamera& render_camera) {
	const kbCamera& camera = *GetEditorWindowCamera();
	const ImGuiIO& io = ImGui::GetIO();

	const Vec3& axis_dir = g_GizmoAxisDirs[axis_index];
	const f32 dist_to_camera = (origin - camera.m_position).length();
	const f32 handle_length = max(dist_to_camera * 0.15f, 1.0f);
	const Vec3 axis_tip = origin + axis_dir * handle_length;

	ImVec2 origin_screen, tip_screen;
	if (!WorldToScreen(origin, render_camera.view_projection_matrix, m_ViewportPos, m_ViewportSize, origin_screen)) {
		return;
	}
	if (!WorldToScreen(axis_tip, render_camera.view_projection_matrix, m_ViewportPos, m_ViewportSize, tip_screen)) {
		return;
	}

	ImDrawList* const draw_list = ImGui::GetBackgroundDrawList();
	const bool this_axis_dragging = m_bGizmoDragging && m_GizmoDragAxis == axis_index;
	const bool hovered = !m_bGizmoDragging && !io.WantCaptureMouse && DistancePointToSegment(io.MousePos, origin_screen, tip_screen) < 8.0f;
	const ImU32 color = (this_axis_dragging || hovered) ? g_GizmoHighlightColor : g_GizmoAxisColors[axis_index];

	draw_list->AddLine(origin_screen, tip_screen, color, this_axis_dragging ? 4.0f : 3.0f);
	draw_list->AddRectFilled(ImVec2(tip_screen.x - 4.0f, tip_screen.y - 4.0f), ImVec2(tip_screen.x + 4.0f, tip_screen.y + 4.0f), color);

	if (!m_bGizmoDragging) {
		if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
			BeginGizmoDrag(axis_index, kbManipulator::Scale, selected, origin);
		}
		return;
	}

	if (!this_axis_dragging) {
		return;
	}

	if (!ImGui::IsMouseDown(ImGuiMouseButton_Left) || m_GizmoGrabScales.size() != selected.size()) {
		EndGizmoDrag();
		return;
	}

	f32 delta = 0.0f;
	if (UpdateAxisDrag(axis_index, render_camera, delta)) {
		// Per-axis (non-uniform) scale -- only the dragged component
		// changes. Uniform scale is the separate center handle
		// (DrawScaleCenter), not this axis handle.
		const f32 scale_factor = max(0.01f, 1.0f + delta / handle_length);
		for (size_t i = 0; i < selected.size(); i++) {
			Vec3 new_scale = m_GizmoGrabScales[i];
			new_scale[axis_index] *= scale_factor;
			selected[i]->set_scale(new_scale);
		}
	}
}

/// ViewportPanel::DrawTranslateCenter
void ViewportPanel::DrawTranslateCenter(const std::vector<kbEditorEntity*>& selected, const Vec3& origin, const RenderCamera& render_camera) {
	const ImGuiIO& io = ImGui::GetIO();

	ImVec2 origin_screen;
	if (!WorldToScreen(origin, render_camera.view_projection_matrix, m_ViewportPos, m_ViewportSize, origin_screen)) {
		return;
	}

	ImDrawList* const draw_list = ImGui::GetBackgroundDrawList();
	const bool this_dragging = m_bGizmoDragging && m_GizmoDragAxis == kGizmoCenterAxisIndex;
	const f32 dx = io.MousePos.x - origin_screen.x;
	const f32 dy = io.MousePos.y - origin_screen.y;
	const bool hovered = !m_bGizmoDragging && !io.WantCaptureMouse && sqrtf(dx * dx + dy * dy) < 8.0f;
	const ImU32 color = (this_dragging || hovered) ? g_GizmoHighlightColor : g_GizmoCenterColor;

	draw_list->AddCircleFilled(origin_screen, 6.0f, color);

	if (!m_bGizmoDragging) {
		if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
			BeginGizmoDrag(kGizmoCenterAxisIndex, kbManipulator::Translate, selected, origin);
		}
		return;
	}

	if (!this_dragging) {
		return;
	}

	if (!ImGui::IsMouseDown(ImGuiMouseButton_Left) || m_GizmoGrabPositions.size() != selected.size()) {
		EndGizmoDrag();
		return;
	}

	Vec3 delta;
	if (UpdateFreeDrag(render_camera, delta)) {
		for (size_t i = 0; i < selected.size(); i++) {
			selected[i]->set_position(m_GizmoGrabPositions[i] + delta);
		}
	}
}

/// ViewportPanel::DrawScaleCenter
void ViewportPanel::DrawScaleCenter(const std::vector<kbEditorEntity*>& selected, const Vec3& origin, const RenderCamera& render_camera) {
	const ImGuiIO& io = ImGui::GetIO();

	ImVec2 origin_screen;
	if (!WorldToScreen(origin, render_camera.view_projection_matrix, m_ViewportPos, m_ViewportSize, origin_screen)) {
		return;
	}

	ImDrawList* const draw_list = ImGui::GetBackgroundDrawList();
	const bool this_dragging = m_bGizmoDragging && m_GizmoDragAxis == kGizmoCenterAxisIndex;
	const f32 dx = io.MousePos.x - origin_screen.x;
	const f32 dy = io.MousePos.y - origin_screen.y;
	const bool hovered = !m_bGizmoDragging && !io.WantCaptureMouse && sqrtf(dx * dx + dy * dy) < 8.0f;
	const ImU32 color = (this_dragging || hovered) ? g_GizmoHighlightColor : g_GizmoCenterColor;

	draw_list->AddRectFilled(ImVec2(origin_screen.x - 5.0f, origin_screen.y - 5.0f), ImVec2(origin_screen.x + 5.0f, origin_screen.y + 5.0f), color);

	if (!m_bGizmoDragging) {
		if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
			Vec3 ray_origin, ray_dir;
			ScreenToRay(io.MousePos, render_camera, m_ViewportPos, m_ViewportSize, ray_origin, ray_dir);
			const kbCamera& camera = *GetEditorWindowCamera();
			const Vec3 camera_forward = camera.m_rotation.to_mat4()[2].ToVec3();

			Vec3 plane_hit;
			if (RayPlaneIntersect(ray_origin, ray_dir, origin, camera_forward, plane_hit)) {
				BeginGizmoDrag(kGizmoCenterAxisIndex, kbManipulator::Scale, selected, origin);
				m_GizmoGrabCenterDist = max((plane_hit - origin).length(), 0.0001f);
			}
		}
		return;
	}

	if (!this_dragging) {
		return;
	}

	if (!ImGui::IsMouseDown(ImGuiMouseButton_Left) || m_GizmoGrabScales.size() != selected.size()) {
		EndGizmoDrag();
		return;
	}

	Vec3 delta;
	if (UpdateFreeDrag(render_camera, delta)) {
		const f32 cur_dist = (m_GizmoGrabWorldPoint + delta - origin).length();
		const f32 scale_factor = max(0.01f, cur_dist / m_GizmoGrabCenterDist);
		for (size_t i = 0; i < selected.size(); i++) {
			selected[i]->set_scale(m_GizmoGrabScales[i] * scale_factor);
		}
	}
}

/// ViewportPanel::DrawRotateRing
void ViewportPanel::DrawRotateRing(const int axis_index, const std::vector<kbEditorEntity*>& selected, const Vec3& origin, const RenderCamera& render_camera) {
	const kbCamera& camera = *GetEditorWindowCamera();
	const ImGuiIO& io = ImGui::GetIO();

	const Vec3& axis_dir = g_GizmoAxisDirs[axis_index];
	const f32 dist_to_camera = (origin - camera.m_position).length();
	const f32 radius = max(dist_to_camera * 0.15f, 1.0f);

	// Basis spanning the plane perpendicular to axis_dir, to trace out a
	// world-space ring around origin.
	const Vec3 arbitrary = (fabsf(axis_dir.x) < 0.9f) ? Vec3(1.0f, 0.0f, 0.0f) : Vec3(0.0f, 1.0f, 0.0f);
	const Vec3 u = axis_dir.cross(arbitrary).normalize_safe();
	const Vec3 v = axis_dir.cross(u).normalize_safe();

	const int Num_Segments = 32;
	ImVec2 points[Num_Segments];
	for (int i = 0; i < Num_Segments; i++) {
		const f32 theta = (2.0f * kbPI * (f32)i) / (f32)Num_Segments;
		const Vec3 world_point = origin + (u * cosf(theta) + v * sinf(theta)) * radius;
		if (!WorldToScreen(world_point, render_camera.view_projection_matrix, m_ViewportPos, m_ViewportSize, points[i])) {
			return;
		}
	}

	ImDrawList* const draw_list = ImGui::GetBackgroundDrawList();
	const bool this_axis_dragging = m_bGizmoDragging && m_GizmoDragAxis == axis_index;

	bool hovered = false;
	if (!m_bGizmoDragging && !io.WantCaptureMouse) {
		for (int i = 0; i < Num_Segments; i++) {
			if (DistancePointToSegment(io.MousePos, points[i], points[(i + 1) % Num_Segments]) < 8.0f) {
				hovered = true;
				break;
			}
		}
	}

	const ImU32 color = (this_axis_dragging || hovered) ? g_GizmoHighlightColor : g_GizmoAxisColors[axis_index];
	draw_list->AddPolyline(points, Num_Segments, color, this_axis_dragging ? 3.0f : 2.0f, ImDrawFlags_Closed);

	if (!m_bGizmoDragging) {
		if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
			Vec3 ray_origin, ray_dir;
			ScreenToRay(io.MousePos, render_camera, m_ViewportPos, m_ViewportSize, ray_origin, ray_dir);

			Vec3 hit_point;
			if (RayPlaneIntersect(ray_origin, ray_dir, origin, axis_dir, hit_point)) {
				BeginGizmoDrag(axis_index, kbManipulator::Rotate, selected, origin);
				m_GizmoGrabAngleVec = (hit_point - origin).normalize_safe();
			}
		}
		return;
	}

	if (!this_axis_dragging) {
		return;
	}

	if (!ImGui::IsMouseDown(ImGuiMouseButton_Left) || m_GizmoGrabRotations.size() != selected.size()) {
		EndGizmoDrag();
		return;
	}

	Vec3 ray_origin, ray_dir;
	ScreenToRay(io.MousePos, render_camera, m_ViewportPos, m_ViewportSize, ray_origin, ray_dir);

	Vec3 hit_point;
	if (!RayPlaneIntersect(ray_origin, ray_dir, origin, axis_dir, hit_point)) {
		return;
	}

	// Signed angle between the grab vector and the current vector, both
	// measured from origin in the plane perpendicular to axis_dir.
	const Vec3 cur_vec = (hit_point - origin).normalize_safe();
	const f32 cos_angle = max(-1.0f, min(1.0f, m_GizmoGrabAngleVec.dot(cur_vec)));
	f32 angle = acosf(cos_angle);
	if (axis_dir.dot(m_GizmoGrabAngleVec.cross(cur_vec)) < 0.0f) {
		angle = -angle;
	}

	const Quat4 delta_rot(axis_dir, angle);
	for (size_t i = 0; i < selected.size(); i++) {
		selected[i]->set_rotation((delta_rot * m_GizmoGrabRotations[i]).normalize_safe());
	}
}

/// ViewportPanel::EventCB
void ViewportPanel::EventCB(const widgetCBObject* widgetCBObject) {
	if (widgetCBObject == NULL) {
		blk::error("Error: ViewportPanel::EventCB() - NULL widgetCBObject");
	}

	switch (widgetCBObject->widgetType) {

		// Handle when using "undo" selects some entities
		case WidgetCB_EntitySelected:
			extern bool g_bEditorIsUndoingAnAction;

			if (g_Editor->GetSelectedObjects().size() > 0 && g_bEditorIsUndoingAnAction) {

				Vec3 manipulatorPos(0.0f, 0.0f, 0.0f);
				for (int i = 0; i < g_Editor->GetSelectedObjects().size(); i++) {
					manipulatorPos += g_Editor->GetSelectedObjects()[i]->position();
				}
				manipulatorPos /= (float)g_Editor->GetSelectedObjects().size();

				m_Manipulator.set_position(manipulatorPos);
				//m_Manipulator.set_rotation( g_Editor->GetSelectedObjects()[0]->rotation() );
				//m_Manipulator.set_scale( g_Editor->GetSelectedObjects()[0]->scale() );
			}
			break;

		case WidgetCB_Input:
			InputCB(widgetCBObject);
			break;

		case WidgetCB_TranslationButtonPressed:
			m_Manipulator.SetMode(kbManipulator::Translate);
			break;

		case WidgetCB_RotationButtonPressed:
			m_Manipulator.SetMode(kbManipulator::Rotate);
			break;

		case WidgetCB_ScaleButtonPressed:
			m_Manipulator.SetMode(kbManipulator::Scale);
			break;

		// WidgetCB_GameStarted/GameStopped used to swap which Fl_Group was
		// visible. With one viewport there is nothing to swap -- the running
		// game already renders into this same window -- so those events are no
		// longer subscribed to.
	}
}

/// ViewportPanel::InputCB
void ViewportPanel::InputCB(const widgetCBObject* const widgetCBObj) {

	const widgetCBInputObject* const inputObject = static_cast<const widgetCBInputObject*>(widgetCBObj);

	if (inputObject->rightMouseButtonDown) {
		CameraMoveCB(inputObject);
	}
}

/// ViewportPanel::CameraMoveCB
void ViewportPanel::CameraMoveCB(const widgetCBInputObject* const inputObject) {
	kbCamera& camera = m_Camera;
	const float dt = inputObject->dt;

	// Prevent math explosions if dt is zero or negative
	if (dt <= 0.0f) {
		return;
	}

	// Process mouse rotation
	Quat4 totalRotation = Quat4::identity;

	if (inputObject->rightMouseButtonDown && (inputObject->mouseDeltaX != 0 || inputObject->mouseDeltaY != 0)) {
		const Mat4 camMat = camera.m_rotation_target.to_mat4();
		const Vec3 rightVec = camMat[0].ToVec3();

		// Constant mouse sensitivity. Do NOT multiply by dt here!
		const f32 rot_mag = 0.005f;

		Quat4 xRot; xRot.from_axis_angle(Vec3::up, inputObject->mouseDeltaX * -rot_mag);
		Quat4 yRot; yRot.from_axis_angle(rightVec, inputObject->mouseDeltaY * -rot_mag);

		totalRotation = yRot * xRot;
	}

	// Snap the target rotation to the raw mouse input
	if (!totalRotation.is_identity()) {
		camera.m_rotation_target = camera.m_rotation_target * totalRotation;
		camera.m_rotation_target.normalize_self();
	}

	// Calculate spring force
	const float springStrength = 1.f;	// Higher = snappier
	const float damping = 0.3f; // Set lower for more "wobble", 1 is critically damped (no overshoot)

	// Converts the angular distance between current and target into an acceleration
	const float springAcc = springStrength * dt;
	const float dampingAcc = damping * std::sqrt(springStrength) * dt;

	// Update smooth rotation
	const float lerpFactor = 1.0f - std::exp(-springAcc);
	camera.m_rotation_current = Quat4::nlerp(camera.m_rotation_current, camera.m_rotation_target, lerpFactor);

	// Process keyboard movement
	Vec3 moveDir(Vec3::zero);
	float moveSpeed = m_CameraMoveSpeedMultiplier * Base_Cam_Speed * dt;

	const Mat4 currentCamMat = camera.m_rotation_target.to_mat4();
	const Vec3 right = currentCamMat[0].ToVec3();
	const Vec3 fwd = currentCamMat[2].ToVec3();

	// Process the keys sent from the kbEditor::Update loop
	for (auto key : inputObject->keys) {
		if (key == widgetCBInputObject::WidgetInput_Forward) {
			moveDir += fwd;
		} else if (key == widgetCBInputObject::WidgetInput_Back) {
			moveDir -= fwd;
		} else if (key == widgetCBInputObject::WidgetInput_Left) {
			moveDir -= right;
		} else if (key == widgetCBInputObject::WidgetInput_Right) {
			moveDir += right;
		} else if (key == widgetCBInputObject::WidgetInput_Shift) {
			moveSpeed *= 2.0f;
		}
	}

	// Update position
	if (moveDir.length_sqr() > 0.0001f) {
		moveDir.normalize_self();
		camera.m_position += moveDir * moveSpeed;
	}
}
