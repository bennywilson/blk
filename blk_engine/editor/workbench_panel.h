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

	// Output Log selection. ImGui::TextColored/TextUnformatted draw glyphs with
	// no selection model at all, so pulling a line out of the log (a D3D12
	// validation message, say) meant retyping it.
	//
	// The obvious fix -- a read-only InputTextMultiline -- buys character-level
	// selection but costs both per-entry coloring (ImGui text widgets have no
	// rich-text mode) and scroll-to-newest, since ImGui owns that widget's
	// scrolling and will not follow appended text. Neither is worth losing on a
	// live log, so rows are ImGui::Selectable instead: click to select, shift-
	// click to extend, Ctrl+A / Ctrl+C. Selection is line-granular rather than
	// character-granular, which is the one thing this trades away.
	//
	// Indices into g_OutputLog; -1 means nothing selected. Anchor may be
	// greater than end when the range was extended upwards.
	int m_LogSelAnchor = -1;
	int m_LogSelEnd = -1;

	// Flattened log backing Copy All, rebuilt only when the entry count changes.
	std::string m_OutputLogCache;
	size_t m_OutputLogCachedCount = 0;

	void RebuildOutputLogCache();
	void CopyOutputLogSelection();
};
