/// editor_panel.h
///
/// 2026 blk

#pragma once

#include "kbWidgetCBObjects.h"

///  EditorPanel
///
/// Pure interface. It used to carry an x/y/w/h rect for FLTK-era hit-testing
/// and layout, but every panel positions itself through ImGui now, so the rect
/// had no readers left -- and the values passed in had already drifted out of
/// sync with what the panels actually drew.
class EditorPanel {
public:
	virtual void update(const f32 dt) { }
	virtual void render_sync() { }
	virtual void EventCB(const widgetCBObject* const widgetCBObject) { }
	virtual void draw_imgui() { }
};
