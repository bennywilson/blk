/// viewport_panel.cpp
///
/// 2016 blk

#include "blk_core.h"
#include "editor.h"
#include "editor_entity.h"
#include "renderer.h"
#include "imgui.h"

#include "viewport_panel.h"
#include "editor_platform.h"

Model* model = nullptr;
const f32 Base_Cam_Speed = 100.f;

extern bool g_bEditorIsUndoingAnAction;
extern bool g_bBillboardsEnabled;

/// WorldToScreen
///
/// Projects world_pos into viewport-relative logical pixels, the space io.MousePos uses.
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
/// Unprojects a viewport point into a world-space ray.
static void ScreenToRay(const ImVec2& screen_pos, const RenderCamera& camera, const Vec2& viewport_pos, const Vec2& viewport_size, Vec3& out_origin, Vec3& out_dir) {
	// Shifts screen_pos from window space into the viewport before normalizing, the inverse of WorldToScreen().
	const Vec4 ndc_far(
		(((screen_pos.x - viewport_pos.x) / viewport_size.x) * 2.0f) - 1.0f,
		1.0f - (((screen_pos.y - viewport_pos.y) / viewport_size.y) * 2.0f),
		1.0f,
		1.0f);
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
/// Compares component-wise within Vec3::compare()'s epsilon. Doesn't treat q and -q as equal,
/// since a drag that rotated nothing leaves the exact starting components.
static bool QuatsEqual(const Quat4& a, const Quat4& b) {
	const f32 epsilon = 0.0001f;
	return fabsf(a.x - b.x) < epsilon && fabsf(a.y - b.y) < epsilon && fabsf(a.z - b.z) < epsilon && fabsf(a.w - b.w) < epsilon;
}

// World-space gizmo axes and colors (X red, Y green, Z blue). Yellow marks the hovered or dragged handle.
static const Vec3 g_GizmoAxisDirs[3] = { Vec3(1.0f, 0.0f, 0.0f), Vec3(0.0f, 1.0f, 0.0f), Vec3(0.0f, 0.0f, 1.0f) };
static const ImU32 g_GizmoAxisColors[3] = { IM_COL32(220, 40, 40, 255), IM_COL32(40, 200, 40, 255), IM_COL32(60, 120, 240, 255) };
static const ImU32 g_GizmoHighlightColor = IM_COL32(255, 255, 0, 255);
static const ImU32 g_GizmoCenterColor = IM_COL32(230, 230, 230, 255);
static const int kGizmoCenterAxisIndex = 3;

// Toggle Icons colors: a faint ring per entity, purple rays for lights.
static const ImU32 g_EntityIconColor = IM_COL32(255, 255, 255, 140);
static const ImU32 g_LightIconColor = IM_COL32(110, 51, 110, 255);

/// ViewportPanel::ViewportPanel
ViewportPanel::ViewportPanel() {
	g_Editor->RegisterUpdate(this);
	g_Editor->RegisterEvent(this, WidgetCB_Input);
	g_Editor->RegisterEvent(this, WidgetCB_TranslationButtonPressed);
	g_Editor->RegisterEvent(this, WidgetCB_RotationButtonPressed);
	g_Editor->RegisterEvent(this, WidgetCB_ScaleButtonPressed);
	g_Editor->RegisterEvent(this, WidgetCB_EntitySelected);

	m_CameraMoveSpeedMultiplier = 1.0f;
	m_pCurrentlySelectedResource = nullptr;
	m_FovRadians = blk::to_radians(80.0f);
}

/// ViewportPanel::viewport_rect
bool ViewportPanel::viewport_rect(Vec2& out_pos, Vec2& out_size) const {
	const ImGuiIO& io = ImGui::GetIO();
	if (io.DisplaySize.x <= 0.0f || io.DisplaySize.y <= 0.0f) {
		return false;
	}

	// Returns the whole display, since the scene draws straight to the backbuffer behind the passthrough dockspace.
	out_pos.set(0.0f, 0.0f);
	out_size.set(io.DisplaySize.x, io.DisplaySize.y);
	return true;
}

/// ViewportPanel::make_viewport_camera
RenderCamera ViewportPanel::make_viewport_camera() const {
	// See the declaration for why the aspect is the renderer's.
	const f32 aspect = g_renderer ? g_renderer->render_aspect_ratio() : (1920.0f / 1080.0f);
	return make_render_camera(m_Camera.m_position, m_Camera.m_rotation, m_FovRadians, aspect, m_NearClip, m_FarClip);
}

/// ViewportPanel::update
void ViewportPanel::update(const f32 dt) {
	if (g_Editor->IsRunningGame()) {
		return;
	}

	if (!g_Editor->hwnd()) {
		return;
	}

	const Camera& camera = m_Camera;

	// Cycles camera speed on 'V' only while the editor owns the keyboard. The binding table stays on Editor,
	// since the index persists in editorSettings.txt.
	{
		static bool bSpeedKeyWasDown = false;
		const bool bSpeedKeyDown = g_Editor->owns_keyboard() && editor_platform::key_down(editor_platform::Key::V);
		if (bSpeedKeyDown && !bSpeedKeyWasDown) {
			g_Editor->SetCamSpeedIndex((g_Editor->cam_speed_index() + 1) % Editor::NumCamSpeedBindings());
		}
		bSpeedKeyWasDown = bSpeedKeyDown;
	}

	// TODO: Single-viewport chokepoint. Renderer::render() builds one camera from this global, so with several viewports the last update() wins.
	if (g_renderer) {
		g_renderer->set_camera_transform(camera.m_position, camera.m_rotation);
	}

	m_Camera.Update();
}

/// ViewportPanel::draw_imgui
void ViewportPanel::draw_imgui() {
	// Draws icons before the gizmo so its handles land on top in the shared background draw list.
	DrawEntityIcons();
	DrawGizmo();

	// Runs after DrawGizmo(), which claims handle clicks by setting m_bGizmoDragging before picking checks it.
	UpdateViewportPicking();
}

/// ViewportPanel::DrawEntityIcons
///
/// Marks every visible entity and draws a 3x3 bundle of rays along each light, scaled by camera distance to hold a steady screen size.
void ViewportPanel::DrawEntityIcons() {
	if (!g_bBillboardsEnabled || g_Editor->IsRunningGame()) {
		return;
	}

	Vec2 viewport_pos, viewport_size;
	if (!viewport_rect(viewport_pos, viewport_size)) {
		return;
	}

	const RenderCamera render_camera = make_viewport_camera();
	ImDrawList* const draw_list = ImGui::GetBackgroundDrawList();

	for (EditorEntity* const entity : g_Editor->GetGameEntities()) {
		if (entity->IsHidden()) {
			continue;
		}

		const Vec3 position = entity->position();
		ImVec2 screen_pos;
		if (!WorldToScreen(position, render_camera.view_projection_matrix, viewport_pos, viewport_size, screen_pos)) {
			continue;
		}

		const GameEntity* const game_entity = entity->GetGameEntity();
		bool is_light = false;
		for (int i = 0; i < game_entity->num_components(); i++) {
			const Component* const component = game_entity->component(i);
			if (component->IsA(DirectionalLightComponent::GetType()) || component->IsA(LightShaftsComponent::GetType())) {
				is_light = true;
				break;
			}
		}

		if (!is_light) {
			draw_list->AddCircle(screen_pos, 4.0f, g_EntityIconColor, 0, 1.5f);
			continue;
		}

		const f32 scale = max((position - m_Camera.m_position).length() * 0.02f, 1.0f);
		const Mat4 rotation = game_entity->rotation().to_mat4();
		const Vec3 light_dir = Vec3(0.0f, 0.0f, 1.0f) * rotation;
		for (f32 x = -1.0f; x <= 1.0f; x += 1.0f) {
			for (f32 y = -1.0f; y <= 1.0f; y += 1.0f) {
				const Vec3 ray_start = position + (Vec3(x, y, 0.0f) * rotation) * scale;
				const Vec3 ray_end = ray_start + light_dir * (3.0f * scale);
				ImVec2 start_screen, end_screen;
				if (WorldToScreen(ray_start, render_camera.view_projection_matrix, viewport_pos, viewport_size, start_screen) &&
					WorldToScreen(ray_end, render_camera.view_projection_matrix, viewport_pos, viewport_size, end_screen)) {
					draw_list->AddLine(start_screen, end_screen, g_LightIconColor, 1.5f);
				}
			}
		}
		draw_list->AddCircleFilled(screen_pos, 5.0f, g_LightIconColor);
	}
}

/// ViewportPanel::UpdateViewportPicking
///
/// Reads back the entity id the gbuffer pass wrote under the clicked pixel and selects that entity.
void ViewportPanel::UpdateViewportPicking() {
	if (!g_renderer) {
		return;
	}

	// Consumes a landed result before issuing a new pick, keeping at most one in flight.
	u32 picked_entity_id = Renderer::invalid_entity_id();
	if (g_renderer->try_take_entity_id_pick(picked_entity_id)) {
		m_bPickPending = false;

		std::vector<EditorEntity*> newly_selected;
		if (picked_entity_id != Renderer::invalid_entity_id()) {
			for (EditorEntity* const entity : g_Editor->GetGameEntities()) {
				const GameEntity* const game_entity = entity->GetGameEntity();
				if (game_entity && game_entity->GetEntityId() == picked_entity_id) {
					newly_selected.push_back(entity);
					break;
				}
			}
		}

		if (newly_selected.empty()) {
			// Clicking empty space clears the selection unless Ctrl-clicking to extend it.
			if (!m_bPickAppendToSelection) {
				g_Editor->DeselectEntities();
			}
		} else {
			// SelectEntities() pushes its own UndoSelectActor, so picking is undoable.
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

	// Converts to viewport-local coordinates so a viewport hosted inside a panel works unchanged.
	const f32 local_x = io.MousePos.x - viewport_pos.x;
	const f32 local_y = io.MousePos.y - viewport_pos.y;
	if (local_x < 0.0f || local_y < 0.0f || local_x >= viewport_size.x || local_y >= viewport_size.y) {
		return;
	}

	// Scales to backbuffer pixels by DisplayFramebufferScale, which render_ui_overlay() sets to m_frame_width / DisplaySize each frame.
	const f32 backbuffer_x = local_x * io.DisplayFramebufferScale.x;
	const f32 backbuffer_y = local_y * io.DisplayFramebufferScale.y;

	m_bPickPending = true;
	m_bPickAppendToSelection = io.KeyCtrl;
	g_renderer->request_entity_id_pick((u32)backbuffer_x, (u32)backbuffer_y);
}

/// ViewportPanel::DrawGizmo
void ViewportPanel::DrawGizmo() {
	// Filters the selection against the live entity list, since it can briefly hold a deleted entity.
	const std::vector<EditorEntity*>& live_entities = g_Editor->GetGameEntities();
	std::vector<EditorEntity*> selected;
	for (EditorEntity* const entity : g_Editor->GetSelectedObjects()) {
		if (std::find(live_entities.begin(), live_entities.end(), entity) != live_entities.end()) {
			selected.push_back(entity);
		}
	}

	if (selected.empty()) {
		// Ends a drag whose selection emptied so its movement stays undoable. EndGizmoDrag() pushes nothing if the entities are gone.
		EndGizmoDrag();
		return;
	}

	// Caches the rect for this frame's handle helpers.
	if (!viewport_rect(m_ViewportPos, m_ViewportSize)) {
		return;
	}

	const RenderCamera render_camera = make_viewport_camera();

	Vec3 origin(0.0f, 0.0f, 0.0f);
	for (const EditorEntity* const entity : selected) {
		origin += entity->position();
	}
	origin /= (f32)selected.size();

	const Manipulator::manipulatorMode_t mode = m_Manipulator.GetMode();

	// Ends a drag started under another T/R/S mode instead of applying its deltas through the wrong handles.
	if (m_bGizmoDragging && mode != m_GizmoDragMode) {
		EndGizmoDrag();
	}

	// Draws the center handle first, since the axis lines start at origin and would otherwise claim its clicks.
	if (mode == Manipulator::Translate) {
		DrawTranslateCenter(selected, origin, render_camera);
	} else if (mode == Manipulator::Scale) {
		DrawScaleCenter(selected, origin, render_camera);
	}

	for (int axis = 0; axis < 3; axis++) {
		if (mode == Manipulator::Translate) {
			DrawTranslateAxis(axis, selected, origin, render_camera);
		} else if (mode == Manipulator::Scale) {
			DrawScaleAxis(axis, selected, origin, render_camera);
		} else if (mode == Manipulator::Rotate) {
			DrawRotateRing(axis, selected, origin, render_camera);
		}
	}
}

/// ViewportPanel::UpdateFreeDrag
bool ViewportPanel::UpdateFreeDrag(const RenderCamera& render_camera, Vec3& out_delta) const {
	const ImGuiIO& io = ImGui::GetIO();

	Vec3 ray_origin, ray_dir;
	ScreenToRay(io.MousePos, render_camera, m_ViewportPos, m_ViewportSize, ray_origin, ray_dir);

	// Intersects the mouse ray with the camera-facing plane through the grab point.
	const Camera& camera = *GetEditorWindowCamera();
	const Vec3 camera_forward = camera.m_rotation.to_mat4()[2].ToVec3();

	Vec3 plane_hit;
	if (!RayPlaneIntersect(ray_origin, ray_dir, m_GizmoGrabWorldPoint, camera_forward, plane_hit)) {
		return false;
	}

	out_delta = plane_hit - m_GizmoGrabWorldPoint;
	return true;
}

/// ViewportPanel::BeginGizmoDrag
void ViewportPanel::BeginGizmoDrag(const int axis_index, const Manipulator::manipulatorMode_t mode, const std::vector<EditorEntity*>& selected, const Vec3& origin) {
	m_bGizmoDragging = true;
	m_GizmoDragAxis = axis_index;
	m_GizmoDragMode = mode;

	// Sets the grab point for every mode to keep one drag-start path. Rotate reads m_GizmoGrabAngleVec instead.
	m_GizmoGrabWorldPoint = origin;

	m_GizmoGrabEntities.clear();
	m_GizmoGrabPositions.clear();
	m_GizmoGrabRotations.clear();
	m_GizmoGrabScales.clear();
	for (EditorEntity* const entity : selected) {
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

	// Reads after-transforms only from entities still in the editor, since one can be deleted mid-drag.
	const std::vector<EditorEntity*>& live_entities = g_Editor->GetGameEntities();

	std::vector<EditorEntity*> moved_entities;
	std::vector<UndoTransformEntities::EntityTransform_t> before_transforms;
	std::vector<UndoTransformEntities::EntityTransform_t> after_transforms;

	for (size_t i = 0; i < m_GizmoGrabEntities.size(); i++) {
		EditorEntity* const entity = m_GizmoGrabEntities[i];
		if (std::find(live_entities.begin(), live_entities.end(), entity) == live_entities.end()) {
			continue;
		}

		UndoTransformEntities::EntityTransform_t before;
		before.m_position = m_GizmoGrabPositions[i];
		before.m_rotation = m_GizmoGrabRotations[i];
		before.m_scale = m_GizmoGrabScales[i];

		UndoTransformEntities::EntityTransform_t after;
		after.m_position = entity->position();
		after.m_rotation = entity->rotation();
		after.m_scale = entity->scale();

		// Skips entities a no-drag click left unchanged, so no-op clicks can't evict real undo actions.
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

	g_Editor->PushUndoAction(new UndoTransformEntities(moved_entities, before_transforms, after_transforms));
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
/// Draws every handle into the background draw list so it sits over the scene but under the editor panels.
/// Hit-testing gates on !io.WantCaptureMouse to match.
void ViewportPanel::DrawTranslateAxis(const int axis_index, const std::vector<EditorEntity*>& selected, const Vec3& origin, const RenderCamera& render_camera) {
	const Camera& camera = *GetEditorWindowCamera();
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
			BeginGizmoDrag(axis_index, Manipulator::Translate, selected, origin);
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
void ViewportPanel::DrawScaleAxis(const int axis_index, const std::vector<EditorEntity*>& selected, const Vec3& origin, const RenderCamera& render_camera) {
	const Camera& camera = *GetEditorWindowCamera();
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
			BeginGizmoDrag(axis_index, Manipulator::Scale, selected, origin);
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
		// Scales only the dragged component. DrawScaleCenter() handles uniform scale.
		const f32 scale_factor = max(0.01f, 1.0f + delta / handle_length);
		for (size_t i = 0; i < selected.size(); i++) {
			Vec3 new_scale = m_GizmoGrabScales[i];
			new_scale[axis_index] *= scale_factor;
			selected[i]->set_scale(new_scale);
		}
	}
}

/// ViewportPanel::DrawTranslateCenter
void ViewportPanel::DrawTranslateCenter(const std::vector<EditorEntity*>& selected, const Vec3& origin, const RenderCamera& render_camera) {
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
			BeginGizmoDrag(kGizmoCenterAxisIndex, Manipulator::Translate, selected, origin);
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
void ViewportPanel::DrawScaleCenter(const std::vector<EditorEntity*>& selected, const Vec3& origin, const RenderCamera& render_camera) {
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
			const Camera& camera = *GetEditorWindowCamera();
			const Vec3 camera_forward = camera.m_rotation.to_mat4()[2].ToVec3();

			Vec3 plane_hit;
			if (RayPlaneIntersect(ray_origin, ray_dir, origin, camera_forward, plane_hit)) {
				BeginGizmoDrag(kGizmoCenterAxisIndex, Manipulator::Scale, selected, origin);
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
void ViewportPanel::DrawRotateRing(const int axis_index, const std::vector<EditorEntity*>& selected, const Vec3& origin, const RenderCamera& render_camera) {
	const Camera& camera = *GetEditorWindowCamera();
	const ImGuiIO& io = ImGui::GetIO();

	const Vec3& axis_dir = g_GizmoAxisDirs[axis_index];
	const f32 dist_to_camera = (origin - camera.m_position).length();
	const f32 radius = max(dist_to_camera * 0.15f, 1.0f);

	// Basis spanning the plane perpendicular to axis_dir, for tracing the ring around origin.
	const Vec3 arbitrary = (fabsf(axis_dir.x) < 0.9f) ? Vec3(1.0f, 0.0f, 0.0f) : Vec3(0.0f, 1.0f, 0.0f);
	const Vec3 u = axis_dir.cross(arbitrary).normalize_safe();
	const Vec3 v = axis_dir.cross(u).normalize_safe();

	const int Num_Segments = 32;
	ImVec2 points[Num_Segments];
	for (int i = 0; i < Num_Segments; i++) {
		const f32 theta = (2.0f * blk::PI * (f32)i) / (f32)Num_Segments;
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
				BeginGizmoDrag(axis_index, Manipulator::Rotate, selected, origin);
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

	// Signed angle from the grab vector to the current vector, in the plane perpendicular to axis_dir.
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
void ViewportPanel::EventCB(const widgetCBObject* const widget_cb_object) {
	if (!widget_cb_object) {
		blk::error("Error: ViewportPanel::EventCB() - NULL widgetCBObject");
	}

	switch (widget_cb_object->widgetType) {
		// Recenters the manipulator when an undo reselects entities.
		case WidgetCB_EntitySelected:
			if (!g_Editor->GetSelectedObjects().empty() && g_bEditorIsUndoingAnAction) {
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
			InputCB(widget_cb_object);
			break;

		case WidgetCB_TranslationButtonPressed:
			m_Manipulator.SetMode(Manipulator::Translate);
			break;

		case WidgetCB_RotationButtonPressed:
			m_Manipulator.SetMode(Manipulator::Rotate);
			break;

		case WidgetCB_ScaleButtonPressed:
			m_Manipulator.SetMode(Manipulator::Scale);
			break;
	}
}

/// ViewportPanel::InputCB
void ViewportPanel::InputCB(const widgetCBObject* const widget_cb_object) {
	const widgetCBInputObject* const inputObject = static_cast<const widgetCBInputObject*>(widget_cb_object);

	if (inputObject->rightMouseButtonDown) {
		CameraMoveCB(inputObject);
	}
}

/// ViewportPanel::CameraMoveCB
void ViewportPanel::CameraMoveCB(const widgetCBInputObject* const inputObject) {
	Camera& camera = m_Camera;
	const float dt = inputObject->dt;

	// Guards the spring maths against zero or negative dt.
	if (dt <= 0.0f) {
		return;
	}

	Quat4 totalRotation = Quat4::identity;

	if (inputObject->rightMouseButtonDown && (inputObject->mouseDeltaX != 0 || inputObject->mouseDeltaY != 0)) {
		const Mat4 camMat = camera.m_rotation_target.to_mat4();
		const Vec3 rightVec = camMat[0].ToVec3();

		// Scales per pixel, not per second, since mouse deltas are already per-frame displacement.
		const f32 rot_mag = 0.005f;

		Quat4 xRot;
		xRot.from_axis_angle(Vec3::up, inputObject->mouseDeltaX * -rot_mag);
		Quat4 yRot;
		yRot.from_axis_angle(rightVec, inputObject->mouseDeltaY * -rot_mag);

		totalRotation = yRot * xRot;
	}

	// Snaps the target to raw input. The current rotation springs toward it below.
	if (!totalRotation.is_identity()) {
		camera.m_rotation_target = camera.m_rotation_target * totalRotation;
		camera.m_rotation_target.normalize_self();
	}

	const float springStrength = 1.f; // Higher = snappier
	const float damping = 0.3f; // Set lower for more "wobble", 1 is critically damped (no overshoot)

	// Converts spring strength into this frame's blend toward the target.
	const float springAcc = springStrength * dt;
	const float dampingAcc = damping * std::sqrt(springStrength) * dt;

	const float lerpFactor = 1.0f - std::exp(-springAcc);
	camera.m_rotation_current = Quat4::nlerp(camera.m_rotation_current, camera.m_rotation_target, lerpFactor);

	Vec3 moveDir(Vec3::zero);
	float moveSpeed = m_CameraMoveSpeedMultiplier * Base_Cam_Speed * dt;

	const Mat4 currentCamMat = camera.m_rotation_target.to_mat4();
	const Vec3 right = currentCamMat[0].ToVec3();
	const Vec3 fwd = currentCamMat[2].ToVec3();

	// Keys come from Editor::Update()'s GetAsyncKeyState polling.
	for (const auto key : inputObject->keys) {
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

	if (moveDir.length_sqr() > 0.0001f) {
		moveDir.normalize_self();
		camera.m_position += moveDir * moveSpeed;
	}
}
