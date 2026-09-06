/// editor_panel.cpp
///
/// 2026 blk

#include "blk_core.h"
#include "kbEditor.h"
#include "editor_panel.h"

/// EditorPanel::EditorPanel
EditorPanel::EditorPanel(const int x, const int y, const int w, const int h) :
	m_X(x),
	m_Y(y),
	m_Width(w),
	m_Height(h) {
}

/// EditorPanel::IsPointWithinBounds
bool EditorPanel::IsPointWithinBounds(const int x, const int y) const {
	return (x >= m_X && x < m_X + m_Width && y >= m_Y && y < m_Y + m_Height);
}

///  *  EditorPanel::DisplayWidth
int EditorPanel::DisplayWidth() const {
	return m_Width - 2 * kbEditor::PanelBorderSize();
}
