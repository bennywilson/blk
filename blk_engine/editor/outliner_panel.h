/// outliner_panel.h
///
/// 2026 blk

#pragma once

#include "editor_panel.h"

class EditorEntity;

/// OutlinerPanel
///
/// Lists the editor's live entities and drives selection, holding no copy of either.
class OutlinerPanel : public EditorPanel {
public:
	OutlinerPanel() {}

	virtual void draw_imgui() override;

private:
	// Identity token for last frame's selection. Never dereferenced, since the selection list can briefly hold a deleted entity.
	const EditorEntity* m_LastSelectedEntity = nullptr;

	// Set for one frame when the selection changes outside this panel, so the matching row scrolls into view.
	bool m_bScrollToSelection = false;
};
