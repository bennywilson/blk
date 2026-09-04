/// workbench_panel.h
///
/// 2026 blk

#pragma once

#include "editor_panel.h"

/// WorkbenchPanel
///
/// Phase 3, Milestone 4: replaces the FLTK menu bar, toolbar row, and output
/// log with ImGui equivalents -- full behavioral parity, not additive
/// alongside an FLTK original like OutlinerPanel/PropertiesPanel were.
/// Milestone 8 added the viewport's right-click menu, the last FLTK widget.
/// Friended by kbEditor (kbEditor.h) so it can call kbEditor's existing
/// private command methods and reach its now-plain toolbar state directly,
/// instead of growing kbEditor's public API for this one internal consumer.
class WorkbenchPanel : public EditorPanel {
public:
	WorkbenchPanel(const int x, const int y, const int w, const int h) : EditorPanel(x, y, w, h) { }

	virtual void draw_imgui() override;

private:
	void DrawMainMenuBar();
	void DrawToolbar();
	void DrawOutputLog();
	void DrawAddPrefabPopup();
	void DrawViewportContextMenu();
};
