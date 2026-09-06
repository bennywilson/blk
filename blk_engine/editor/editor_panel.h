/// editor_panel.h
///
/// 2026 blk

#pragma once

#include "kbWidgetCBObjects.h"

///  EditorPanel
class EditorPanel {
public:
	EditorPanel(const int x, const int y, const int width, const int height);

	virtual void update(const f32 dt) { }
	virtual void render_sync() { }
	virtual void EventCB(const widgetCBObject* const widgetCBObject) { }
	virtual void draw_imgui() { }

	bool IsPointWithinBounds(const int x, const int y) const;

protected:
	int	DisplayWidth() const;
	unsigned int FontSize()	const { return 14; }
	unsigned int LineSpacing() const { return FontSize() + 6; }

private:
	int m_X;
	int m_Y;
	int m_Width;
	int m_Height;
};
