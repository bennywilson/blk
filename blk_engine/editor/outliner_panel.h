/// outliner_panel.h
///
/// 2026 blk

#pragma once

#include "editor_panel.h"

class kbEditorEntity;

/// OutlinerPanel
///
/// Phase 3, Milestone 2: first ImGui panel bridged into the live FLTK
/// editor. Immediate-mode reader/writer over kbEditor's existing entity
/// list and selection state -- no new engine-side data structures.
class OutlinerPanel : public EditorPanel {
public:
	OutlinerPanel(const int x, const int y, const int w, const int h) : EditorPanel(x, y, w, h) { }

	virtual void draw_imgui() override;

private:
	// Identity of last frame's selection, used only to notice that it changed.
	// NEVER dereferenced: GetSelectedObjects() can hold a dangling
	// kbEditorEntity* between an entity being deleted and the selection list
	// being cleared (the same hazard PropertiesPanel::draw_imgui() and
	// kbMainTab::DrawGizmo() guard against), so this is an identity token and
	// nothing more.
	const kbEditorEntity* m_LastSelectedEntity = nullptr;

	// Set for one frame when the selection changed somewhere other than this
	// panel, so the matching row can scroll itself into view as it is drawn.
	bool m_bScrollToSelection = false;
};
