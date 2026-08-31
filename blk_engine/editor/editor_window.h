/// editor_window.h
///
/// 2026 blk

#pragma once

#include "editor_panel.h"
#include "camera.h"

#pragma warning(push)
#pragma warning(disable:4312)
#include <FL/FL_Window.h>
#include <FL/x.H>
#pragma warning(pop)

/// kbEditorWindow
class kbEditorWindow : public Fl_Window, public EditorPanel {
public:
	kbEditorWindow(const int x, const int y, const int w, const int h, const char* const title = nullptr);

	virtual void update(const f32 dt) override;
	virtual void EventCB(const widgetCBType_t);	// TODO this function differs from EditorPanel's.  WHY?

	HWND GetWindowHandle() const { return fl_xid(this); }

	kbCamera& GetCamera() { return m_Camera; }

private:
	kbCamera m_Camera;
	Vec3 m_TargetPos;
	Quat4 m_TargetRotation;
};
