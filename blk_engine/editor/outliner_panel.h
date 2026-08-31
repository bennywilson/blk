/// outliner_panel.h
///
/// 2026 blk

#pragma once

#include "editor_panel.h"

/// OutlinerPanel
///
/// Phase 3, Milestone 2: first ImGui panel bridged into the live FLTK
/// editor. Immediate-mode reader/writer over kbEditor's existing entity
/// list and selection state -- no new engine-side data structures.
class OutlinerPanel : public EditorPanel {
public:
	OutlinerPanel(const int x, const int y, const int w, const int h) : EditorPanel(x, y, w, h) { }

	virtual void draw_imgui() override;
};
