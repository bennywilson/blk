/// kbMainTab.cpp
///
/// 2016 blk

#include "blk_core.h"
#include "Matrix.h"
#include "Quaternion.h"
#include "editor_panel.h"
#include "editor_window.h"
#include "kbEditor.h"
#include "model.h"
#include "entity_header.h"
#include "kbEditorEntity.h"
#include "kbManipulator.h"
#include "renderer.h"
#include "imgui.h"

#include "kbMainTab.h"
#pragma warning(push)
#pragma warning(disable:4312)
#include <fl/fl_button.h>
#pragma warning(pop)

kbModel* model = nullptr;
const f32 Base_Cam_Speed = 100.f;

/// WorldToScreen
///
/// Projects a world position through view_projection into the same logical
/// pixel space ImGui's own io.MousePos/io.DisplaySize use (the current
/// viewport HWND's client area -- see kbEditor::handle()'s coordinate-space
/// comment). Returns false for a point behind the camera.
static bool WorldToScreen(const Vec3& world_pos, const Mat4& view_projection, const ImVec2& display_size, ImVec2& out_screen) {
	const Vec4 clip = Vec4(world_pos, 1.0f).transform_point(view_projection, false);
	if (clip.w < 0.0001f) {
		return false;
	}

	const f32 ndc_x = clip.x / clip.w;
	const f32 ndc_y = clip.y / clip.w;
	out_screen.x = (ndc_x * 0.5f + 0.5f) * display_size.x;
	out_screen.y = (1.0f - (ndc_y * 0.5f + 0.5f)) * display_size.y;
	return true;
}

/// ScreenToRay
///
/// Same screen->clip->world unprojection as kbMainTab::ManipulatorEvent(),
/// but via RenderCamera::inv_view_projection_matrix instead of a second,
/// separately-built/-inverted projection matrix.
static void ScreenToRay(const ImVec2& screen_pos, const RenderCamera& camera, const ImVec2& display_size, Vec3& out_origin, Vec3& out_dir) {
	const Vec4 ndc_far(
		(screen_pos.x / display_size.x) * 2.0f - 1.0f,
		1.0f - (screen_pos.y / display_size.y) * 2.0f,
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

// World-space gizmo axes/colors -- X=red, Y=green, Z=blue, matching the
// usual translate/scale/rotate handle convention. Yellow marks whichever
// handle is currently hovered or being dragged.
static const Vec3 g_GizmoAxisDirs[3] = { Vec3(1.0f, 0.0f, 0.0f), Vec3(0.0f, 1.0f, 0.0f), Vec3(0.0f, 0.0f, 1.0f) };
static const ImU32 g_GizmoAxisColors[3] = { IM_COL32(220, 40, 40, 255), IM_COL32(40, 200, 40, 255), IM_COL32(60, 120, 240, 255) };
static const ImU32 g_GizmoHighlightColor = IM_COL32(255, 255, 0, 255);
static const ImU32 g_GizmoCenterColor = IM_COL32(230, 230, 230, 255);
static const int kGizmoCenterAxisIndex = 3;

/// kbEditorMainTab::kbEditorMainTab
kbMainTab::kbMainTab(int x, int y, int w, int h) :
	EditorPanel(x, y, w, h) {

	// The one real viewport, filling this panel's bounds. With the Fl_Tabs base
	// gone it parents directly into kbEditor's window instead of an Fl_Group
	// inside the tab strip, and there's no tab strip to inset for -- one fewer
	// level of FLTK nesting for Milestone 8 to unpick.
	m_pEditorWindow = new kbEditorWindow(x, y, w, h);
	m_pEditorWindow->end();

	// register this widget with the editor
	g_Editor->RegisterUpdate(this);
	g_Editor->RegisterEvent(this, WidgetCB_Input);
	g_Editor->RegisterEvent(this, WidgetCB_TranslationButtonPressed);
	g_Editor->RegisterEvent(this, WidgetCB_RotationButtonPressed);
	g_Editor->RegisterEvent(this, WidgetCB_ScaleButtonPressed);
	g_Editor->RegisterEvent(this, WidgetCB_EntityTransformed);
	g_Editor->RegisterEvent(this, WidgetCB_EntitySelected);

	m_CameraMoveSpeedMultiplier = 1.0f;
	m_pCurrentlySelectedResource = nullptr;
}

/// kbMainTab::update
void kbMainTab::update(const f32 dt) {
	if (g_Editor->IsRunningGame()) {
		return;
	}

	kbEditorWindow* const pCurrentWindow = m_pEditorWindow;

	if (pCurrentWindow == nullptr || pCurrentWindow->GetWindowHandle() == nullptr) {
		return;
	}

	const kbCamera& pCamera = pCurrentWindow->GetCamera();

	//g_pRenderer->SetRenderViewTransform(pCurrentWindow->GetWindowHandle(), pCamera.m_position, pCamera.m_rotation);
	//g_pRenderer->SetRenderWindow(pCurrentWindow->GetWindowHandle());

	if (g_renderer != nullptr) {
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

	pCurrentWindow->GetCamera().Update();

}

/// kbMainTab::RenderSync
void kbMainTab::render_sync() {
	/*EditorPanel::render_sync();

	m_Manipulator.render_sync();

	const widgetCBInputObject& inputState = g_Editor->get_input();

	// Convert mouse coordinates from window space to screen space
	kbEditorWindow* const pCurrentWindow = GetCurrentWindow();
	if (pCurrentWindow == nullptr) {
		return;
	}

	RECT windowRect;
	GetWindowRect(pCurrentWindow->GetWindowHandle(), &windowRect);

	const float windowWidth = (float)windowRect.right - windowRect.left;
	const float windowHeight = (float)windowRect.bottom - windowRect.top;
	Vec2i mouseXY(inputState.mouseX, inputState.mouseY);
	mouseXY.x -= windowRect.left;
	mouseXY.y -= y() + kbEditor::TabHeight();

	Vec2i mouseRenderBufferPos;
	mouseRenderBufferPos.x = (int)(mouseXY.x * g_pRenderer->GetBackBufferWidth() / windowWidth);
	mouseRenderBufferPos.y = (int)(mouseXY.y * g_pRenderer->GetBackBufferHeight() / windowHeight);

	if (m_Manipulator.IsGrabbed()) {
		if (inputState.leftMouseButtonDown == false) {
			m_Manipulator.ReleaseFromMouseGrab();
		} else {
			// Dragging
			ManipulatorEvent(false, mouseXY);

			std::vector<kbEditorEntity*>& entityList = g_Editor->GetGameEntities();

			for (int i = 0; i < entityList.size(); i++) {
				if (entityList[i]->IsSelected()) {
					if (m_Manipulator.GetMode() == kbManipulator::manipulatorMode_t::Translate) {
						entityList[i]->set_position(m_Manipulator.position());
					} else if (m_Manipulator.GetMode() == kbManipulator::manipulatorMode_t::Rotate) {
						entityList[i]->set_rotation(m_Manipulator.rotation());
					} else if (m_Manipulator.GetMode() == kbManipulator::manipulatorMode_t::Scale) {
						entityList[i]->set_scale(m_Manipulator.scale());
					}
				}
			}

		}
	}

	if (m_Manipulator.IsGrabbed() == true || inputState.leftMouseButtonPressed == false) {
		return;
	}

	if (mouseXY.x < 0 || mouseXY.y < 0 || mouseXY.x >= windowWidth || mouseXY.y >= windowHeight) {
		return;
	}

	const Vec2i hitEntityId = g_pRenderer->GetEntityIdAtScreenPosition(mouseRenderBufferPos.x, mouseRenderBufferPos.y);
	if (hitEntityId.x == UINT16_MAX) {
		ManipulatorEvent(true, mouseXY);
	} else {
		std::vector<kbEditorEntity*>& entityList = g_Editor->GetGameEntities();

		const bool bCtrlIsDown = GetAsyncKeyState(VK_LCONTROL) || GetAsyncKeyState(VK_RCONTROL);

		kbEditorEntity* pSelectedEntity = nullptr;
		for (int i = 0; i < entityList.size(); i++) {
			if (entityList[i]->GetGameEntity()->GetEntityId() == hitEntityId.x) {
				pSelectedEntity = entityList[i];
				std::vector<kbEditorEntity*> selectedEntities;
				selectedEntities.push_back(entityList[i]);
				g_Editor->SelectEntities(selectedEntities, bCtrlIsDown);
				break;
			}
		}

		if (pSelectedEntity == nullptr) {
			g_Editor->DeselectEntities();
			return;
		}

		Vec3 manipulatorPos(0.0f, 0.0f, 0.0f);
		for (int i = 0; i < g_Editor->GetSelectedObjects().size(); i++) {
			manipulatorPos += g_Editor->GetSelectedObjects()[i]->position();
		}
		manipulatorPos /= (float)g_Editor->GetSelectedObjects().size();

		// check if mouse grabbed the manipulator
		m_Manipulator.set_position(manipulatorPos);
		m_Manipulator.set_rotation(pSelectedEntity->rotation());
		m_Manipulator.set_scale(pSelectedEntity->scale());
	}
	*/
}

/// kbMainTab::draw_imgui
void kbMainTab::draw_imgui() {
	DrawGizmo();
}

/// kbMainTab::DrawGizmo
void kbMainTab::DrawGizmo() {
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
		m_bGizmoDragging = false;
		return;
	}

	const ImGuiIO& io = ImGui::GetIO();
	if (io.DisplaySize.x <= 0.0f || io.DisplaySize.y <= 0.0f) {
		return;
	}

	const kbCamera& camera = *GetEditorWindowCamera();

	// Mirrors Renderer::render()'s make_render_camera() call (renderer.cpp):
	// g_fov/g_near_clip_plane/g_far_clip_plane and g_screen_width/
	// g_screen_height. Those constants are declared extern only in the
	// D3D12-specific renderer_dx12.h, and editor code shouldn't take on a
	// backend-specific include just for this -- duplicated here for the
	// prototype. Hoist into a shared, backend-agnostic accessor if this
	// grows past proof-of-concept.
	const f32 fov = kbToRadians(80.0f);
	const f32 near_z = 1.0f;
	const f32 far_z = 20000.0f;
	const f32 aspect = 1920.0f / 1080.0f;

	const RenderCamera render_camera = make_render_camera(camera.m_position, camera.m_rotation, fov, aspect, near_z, far_z);

	Vec3 origin(0.0f, 0.0f, 0.0f);
	for (const kbEditorEntity* const entity : selected) {
		origin += entity->position();
	}
	origin /= (f32)selected.size();

	const kbManipulator::manipulatorMode_t mode = m_Manipulator.GetMode();

	// A drag started under a different T/R/S mode (e.g. the mode button was
	// clicked while a drag was somehow still active) reads a grab-snapshot
	// vector that was never populated for the new mode -- stop rather than
	// apply garbage.
	if (m_bGizmoDragging && mode != m_GizmoDragMode) {
		m_bGizmoDragging = false;
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

/// kbMainTab::UpdateFreeDrag
bool kbMainTab::UpdateFreeDrag(const RenderCamera& render_camera, Vec3& out_delta) const {
	const ImGuiIO& io = ImGui::GetIO();

	Vec3 ray_origin, ray_dir;
	ScreenToRay(io.MousePos, render_camera, io.DisplaySize, ray_origin, ray_dir);

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

/// kbMainTab::UpdateAxisDrag
bool kbMainTab::UpdateAxisDrag(const int axis_index, const RenderCamera& render_camera, f32& out_delta) const {
	Vec3 free_delta;
	if (!UpdateFreeDrag(render_camera, free_delta)) {
		return false;
	}

	out_delta = free_delta.dot(g_GizmoAxisDirs[axis_index]);
	return true;
}

/// kbMainTab::DrawTranslateAxis
///
/// Every gizmo handle draws into ImGui's *background* draw list, not the
/// foreground one: the handles belong over the 3D scene but under the editor's
/// panels. The foreground list draws on top of every window, so the gizmo bled
/// over the Outliner/Properties/Resources panels whenever the selected entity
/// projected behind one. Hit-testing is already gated on !io.WantCaptureMouse,
/// so the two agree about which clicks belong to the gizmo.
void kbMainTab::DrawTranslateAxis(const int axis_index, const std::vector<kbEditorEntity*>& selected, const Vec3& origin, const RenderCamera& render_camera) {
	const kbCamera& camera = *GetEditorWindowCamera();
	const ImGuiIO& io = ImGui::GetIO();

	const Vec3& axis_dir = g_GizmoAxisDirs[axis_index];
	const f32 dist_to_camera = (origin - camera.m_position).length();
	const f32 handle_length = max(dist_to_camera * 0.15f, 1.0f);
	const Vec3 axis_tip = origin + axis_dir * handle_length;

	ImVec2 origin_screen, tip_screen;
	if (!WorldToScreen(origin, render_camera.view_projection_matrix, io.DisplaySize, origin_screen)) {
		return;
	}
	if (!WorldToScreen(axis_tip, render_camera.view_projection_matrix, io.DisplaySize, tip_screen)) {
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
			m_bGizmoDragging = true;
			m_GizmoDragAxis = axis_index;
			m_GizmoDragMode = kbManipulator::Translate;
			m_GizmoGrabWorldPoint = origin;
			m_GizmoGrabPositions.clear();
			for (const kbEditorEntity* const entity : selected) {
				m_GizmoGrabPositions.push_back(entity->position());
			}
		}
		return;
	}

	if (!this_axis_dragging) {
		return;
	}

	if (!ImGui::IsMouseDown(ImGuiMouseButton_Left) || m_GizmoGrabPositions.size() != selected.size()) {
		m_bGizmoDragging = false;
		return;
	}

	f32 delta = 0.0f;
	if (UpdateAxisDrag(axis_index, render_camera, delta)) {
		for (size_t i = 0; i < selected.size(); i++) {
			selected[i]->set_position(m_GizmoGrabPositions[i] + axis_dir * delta);
		}
	}
}

/// kbMainTab::DrawScaleAxis
void kbMainTab::DrawScaleAxis(const int axis_index, const std::vector<kbEditorEntity*>& selected, const Vec3& origin, const RenderCamera& render_camera) {
	const kbCamera& camera = *GetEditorWindowCamera();
	const ImGuiIO& io = ImGui::GetIO();

	const Vec3& axis_dir = g_GizmoAxisDirs[axis_index];
	const f32 dist_to_camera = (origin - camera.m_position).length();
	const f32 handle_length = max(dist_to_camera * 0.15f, 1.0f);
	const Vec3 axis_tip = origin + axis_dir * handle_length;

	ImVec2 origin_screen, tip_screen;
	if (!WorldToScreen(origin, render_camera.view_projection_matrix, io.DisplaySize, origin_screen)) {
		return;
	}
	if (!WorldToScreen(axis_tip, render_camera.view_projection_matrix, io.DisplaySize, tip_screen)) {
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
			m_bGizmoDragging = true;
			m_GizmoDragAxis = axis_index;
			m_GizmoDragMode = kbManipulator::Scale;
			m_GizmoGrabWorldPoint = origin;
			m_GizmoGrabScales.clear();
			for (const kbEditorEntity* const entity : selected) {
				m_GizmoGrabScales.push_back(entity->scale());
			}
		}
		return;
	}

	if (!this_axis_dragging) {
		return;
	}

	if (!ImGui::IsMouseDown(ImGuiMouseButton_Left) || m_GizmoGrabScales.size() != selected.size()) {
		m_bGizmoDragging = false;
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

/// kbMainTab::DrawTranslateCenter
void kbMainTab::DrawTranslateCenter(const std::vector<kbEditorEntity*>& selected, const Vec3& origin, const RenderCamera& render_camera) {
	const ImGuiIO& io = ImGui::GetIO();

	ImVec2 origin_screen;
	if (!WorldToScreen(origin, render_camera.view_projection_matrix, io.DisplaySize, origin_screen)) {
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
			m_bGizmoDragging = true;
			m_GizmoDragAxis = kGizmoCenterAxisIndex;
			m_GizmoDragMode = kbManipulator::Translate;
			m_GizmoGrabWorldPoint = origin;
			m_GizmoGrabPositions.clear();
			for (const kbEditorEntity* const entity : selected) {
				m_GizmoGrabPositions.push_back(entity->position());
			}
		}
		return;
	}

	if (!this_dragging) {
		return;
	}

	if (!ImGui::IsMouseDown(ImGuiMouseButton_Left) || m_GizmoGrabPositions.size() != selected.size()) {
		m_bGizmoDragging = false;
		return;
	}

	Vec3 delta;
	if (UpdateFreeDrag(render_camera, delta)) {
		for (size_t i = 0; i < selected.size(); i++) {
			selected[i]->set_position(m_GizmoGrabPositions[i] + delta);
		}
	}
}

/// kbMainTab::DrawScaleCenter
void kbMainTab::DrawScaleCenter(const std::vector<kbEditorEntity*>& selected, const Vec3& origin, const RenderCamera& render_camera) {
	const ImGuiIO& io = ImGui::GetIO();

	ImVec2 origin_screen;
	if (!WorldToScreen(origin, render_camera.view_projection_matrix, io.DisplaySize, origin_screen)) {
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
			ScreenToRay(io.MousePos, render_camera, io.DisplaySize, ray_origin, ray_dir);
			const kbCamera& camera = *GetEditorWindowCamera();
			const Vec3 camera_forward = camera.m_rotation.to_mat4()[2].ToVec3();

			Vec3 plane_hit;
			if (RayPlaneIntersect(ray_origin, ray_dir, origin, camera_forward, plane_hit)) {
				m_bGizmoDragging = true;
				m_GizmoDragAxis = kGizmoCenterAxisIndex;
				m_GizmoDragMode = kbManipulator::Scale;
				m_GizmoGrabWorldPoint = origin;
				m_GizmoGrabCenterDist = max((plane_hit - origin).length(), 0.0001f);
				m_GizmoGrabScales.clear();
				for (const kbEditorEntity* const entity : selected) {
					m_GizmoGrabScales.push_back(entity->scale());
				}
			}
		}
		return;
	}

	if (!this_dragging) {
		return;
	}

	if (!ImGui::IsMouseDown(ImGuiMouseButton_Left) || m_GizmoGrabScales.size() != selected.size()) {
		m_bGizmoDragging = false;
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

/// kbMainTab::DrawRotateRing
void kbMainTab::DrawRotateRing(const int axis_index, const std::vector<kbEditorEntity*>& selected, const Vec3& origin, const RenderCamera& render_camera) {
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
		if (!WorldToScreen(world_point, render_camera.view_projection_matrix, io.DisplaySize, points[i])) {
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
			ScreenToRay(io.MousePos, render_camera, io.DisplaySize, ray_origin, ray_dir);

			Vec3 hit_point;
			if (RayPlaneIntersect(ray_origin, ray_dir, origin, axis_dir, hit_point)) {
				m_bGizmoDragging = true;
				m_GizmoDragAxis = axis_index;
				m_GizmoDragMode = kbManipulator::Rotate;
				m_GizmoGrabAngleVec = (hit_point - origin).normalize_safe();
				m_GizmoGrabRotations.clear();
				for (const kbEditorEntity* const entity : selected) {
					m_GizmoGrabRotations.push_back(entity->rotation());
				}
			}
		}
		return;
	}

	if (!this_axis_dragging) {
		return;
	}

	if (!ImGui::IsMouseDown(ImGuiMouseButton_Left) || m_GizmoGrabRotations.size() != selected.size()) {
		m_bGizmoDragging = false;
		return;
	}

	Vec3 ray_origin, ray_dir;
	ScreenToRay(io.MousePos, render_camera, io.DisplaySize, ray_origin, ray_dir);

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

/// kbMainTab::EventCB
void kbMainTab::EventCB(const widgetCBObject* widgetCBObject) {
	if (widgetCBObject == NULL) {
		blk::error("Error: kbMainTab::EventCB() - NULL widgetCBObject");
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

		case WidgetCB_EntityTransformed:
			EntityTransformedCB(widgetCBObject);
			break;

		// WidgetCB_GameStarted/GameStopped used to swap which Fl_Group was
		// visible. With one viewport there is nothing to swap -- the running
		// game already renders into this same window -- so those events are no
		// longer subscribed to.
	}
}

/// kbMainTab::InputCB
void kbMainTab::InputCB(const widgetCBObject* const widgetCBObj) {

	const widgetCBInputObject* const inputObject = static_cast<const widgetCBInputObject*>(widgetCBObj);

	if (inputObject->rightMouseButtonDown) {
		CameraMoveCB(inputObject);
	}
}

void kbMainTab::CameraMoveCB(const widgetCBInputObject* const inputObject) {
	kbEditorWindow* pCurrentWindow = m_pEditorWindow;
	if (!pCurrentWindow) return;

	kbCamera& camera = pCurrentWindow->GetCamera();
	const float dt = inputObject->dt;

	// Prevent math explosions if dt is zero or negative
	if (dt <= 0.0f) return;

	// ---------------------------------------------------------
	// 1. Process Mouse Rotation (The "Ghost" Target)
	// ---------------------------------------------------------
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

	// Instantly snap the target rotation to the raw mouse input
	if (!totalRotation.is_identity()) {
		camera.m_rotation_target = camera.m_rotation_target * totalRotation;
		camera.m_rotation_target.normalize_self();
	}

	// ---------------------------------------------------------
	// 2. The Frame-Independent Spring (Visual Smoothing)
	// ---------------------------------------------------------
// 1. Define the spring physics
// 'springStrength' (10-30): Higher = tighter, snappier (like a stiff spring)
// 'damping' (0.5-1.0): 1.0 is critically damped (no overshoot), < 1.0 creates 'sway'
	const float springStrength = 1.f;
	const float damping = 0.3f; // Set lower (e.g., 0.4) for more 'wobble'

	// 2. Calculate the "Spring" Force
	// This converts the angular distance between current and target into an acceleration
	const float springAcc = springStrength * dt;
	const float dampingAcc = damping * std::sqrt(springStrength) * dt;

	// 3. Update the smoothed rotation
	// We apply the 'spring' math to the rotation
	const float lerpFactor = 1.0f - std::exp(-springAcc);
	camera.m_rotation_current = Quat4::nlerp(camera.m_rotation_current, camera.m_rotation_target, lerpFactor);
	// ---------------------------------------------------------
	// 3. Process Keyboard Movement
	// ---------------------------------------------------------
	Vec3 moveDir(Vec3::zero);
	float moveSpeed = m_CameraMoveSpeedMultiplier * Base_Cam_Speed * dt;

	// Extract movement axes from the camera's actual rotation -- kbCamera::
	// Update() sets m_rotation (what set_camera_transform sends to the
	// renderer) straight to m_rotation_target with no smoothing, so movement
	// has to read m_rotation_target too. Reading the separately-smoothed
	// m_rotation_current here meant WASD kept pushing along the
	// pre-rotation direction after a mouse-look turn until that spring
	// caught up -- slowly, since springStrength 1.0 gives it a multi-second
	// half-life.
	const Mat4 currentCamMat = camera.m_rotation_target.to_mat4();
	const Vec3 right = currentCamMat[0].ToVec3();
	const Vec3 fwd = currentCamMat[2].ToVec3();

	// Process the keys sent from the kbEditor::Update loop
	for (auto key : inputObject->keys) {
		if (key == widgetCBInputObject::WidgetInput_Forward) moveDir += fwd;
		else if (key == widgetCBInputObject::WidgetInput_Back) moveDir -= fwd;
		else if (key == widgetCBInputObject::WidgetInput_Left) moveDir -= right;
		else if (key == widgetCBInputObject::WidgetInput_Right) moveDir += right;
		else if (key == widgetCBInputObject::WidgetInput_Shift) moveSpeed *= 2.0f;
	}

	// ---------------------------------------------------------
	// 4. Apply Position Update
	// ---------------------------------------------------------
	if (moveDir.length_sqr() > 0.0001f) {
		blk::log("movedir = %f %f %f", moveDir.x, moveDir.y, moveDir.z);
		moveDir.normalize_self();
		camera.m_position += moveDir * moveSpeed;
	}
}

/// kbMainTab::EntityTransformedCB
void kbMainTab::EntityTransformedCB(const widgetCBObject* const widgetCBObj) {
	const widgetCBEntityTransformed* entityTransformedWidget = static_cast<const widgetCBEntityTransformed*>(widgetCBObj);

	std::vector< class kbEditorEntity* >& gameEntities = g_Editor->GetGameEntities();
	kbEditorEntity* pMovedEntity = entityTransformedWidget->entitiesMoved[0];

	if (std::find(gameEntities.begin(), gameEntities.end(), pMovedEntity) != gameEntities.end()) {
		m_Manipulator.set_position(pMovedEntity->position());
		m_Manipulator.set_rotation(pMovedEntity->rotation());
		m_Manipulator.set_scale(pMovedEntity->scale());
	}
}

/// kbMainTab::ManipulatorEvent
void kbMainTab::ManipulatorEvent(const bool bClicked, const Vec2i& mouseXY) {

	RECT windowRect;

	kbEditorWindow* const pCurrentWindow = m_pEditorWindow;

	if (pCurrentWindow == nullptr) {
		return;
	}

	kbCamera& camera = pCurrentWindow->GetCamera();

	GetWindowRect(pCurrentWindow->GetWindowHandle(), &windowRect);
	const float windowWidth = (float)windowRect.right - windowRect.left;//g_pRenderer->GetBackBufferWidth();
	const float windowHeight = (float)windowRect.bottom - windowRect.top;//->GetBackBufferHeight();

	Vec4 mousePosition((float)mouseXY.x, (float)mouseXY.y, 0.0f, 1.0f);

	// Transform from screeen space to unit clip space
	mousePosition.x = (((2.0f * mousePosition.x) / windowWidth) - 1.0f);
	mousePosition.y = -(((2.0f * (mousePosition.y)) / windowHeight) - 1.0f);
	mousePosition.z = 1.0f;

	// Persepctive mat
	Mat4 perspectiveMat;
	perspectiveMat.create_perspective_matrix(kbToRadians(75.0f), windowWidth / windowHeight, 0.25f, 1000.0f);	// TODO - NEAR/FAR PLANE 
	perspectiveMat.inverse_projection();

	// View mat
	const Mat4 modelViewMatrix(camera.m_rotation, camera.m_position);
	const Mat4 unitCubeToWorldMatrix = perspectiveMat * modelViewMatrix;
	const Vec4 ray = (mousePosition.transform_point(unitCubeToWorldMatrix, true) - camera.m_position);

	if (bClicked) {
		if (m_Manipulator.AttemptMouseGrab(camera.m_position, ray.ToVec3(), camera.m_rotation) == false) {
			std::vector<kbEditorEntity*> empty;
			g_Editor->SelectEntities(empty, false);
			m_Manipulator.ReleaseFromMouseGrab();
		}
		return;
	}

	m_Manipulator.UpdateMouseDrag(camera.m_position, ray.ToVec3(), camera.m_rotation);
}
