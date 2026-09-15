/// workbench_panel.h
///
/// 2026 blk

#pragma once

#include "editor_panel.h"

/// WorkbenchPanel
///
/// Draws the main menu bar, toolbar, output log, and the add-prefab and viewport popups.
/// Editor friends it so it can call Editor's private commands without widening the public API.
class WorkbenchPanel : public EditorPanel {
public:
	WorkbenchPanel() {}

	virtual void draw_imgui() override;

private:
	void DrawMainMenuBar();
	void DrawToolbar();
	void DrawOutputLog();
	void DrawAddPrefabPopup();
	void DrawViewportContextMenu();
	void CopyOutputLogSelection();

	// Selects whole log rows via Selectable, since a read-only InputTextMultiline would lose per-entry color and scroll-to-newest.
	// Indices into g_OutputLog, -1 for none. The anchor exceeds the end when a range extends upward.
	int m_LogSelAnchor = -1;
	int m_LogSelEnd = -1;
};
