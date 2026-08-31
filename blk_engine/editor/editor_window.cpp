/// editor_window.cpp
///
/// 2026 blk

#include "blk_core.h"
#include "editor_window.h"

/// kbEditorWindow
kbEditorWindow::kbEditorWindow(const int x, const int y, const int w, const int h, const char* const title) :
	EditorPanel(x, y, w, h),
	Fl_Window(x, y, w, h, title) {
}

/// kbEditorWindow::update
void kbEditorWindow::update(const f32 dt) {

}

/// kbEditorWindow::EventCB
void kbEditorWindow::EventCB(const widgetCBType_t cbType) {

}
