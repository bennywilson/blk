/// editor_platform_win32.cpp
///
/// 2026 blk

#include "blk_core.h"
#include <commdlg.h>
#include "editor.h"
#include "editor_platform.h"

namespace editor_platform {

	namespace {
		// Calls the A-suffixed functions explicitly: blk_engine builds MultiByte, and
		// the unsuffixed macros would flip to wchar_t if CharacterSet ever changed.
		HWND owner_window() {
			return g_Editor ? g_Editor->hwnd() : nullptr;
		}

		/// "Level Files (*.blklevel;*.kblevel)\0*.blklevel;*.kblevel\0" - the
		/// double-NUL-terminated list the common dialogs want.
		std::string make_filter_string(const FileFilter& filter) {
			std::string patterns;
			for (size_t i = 0; i < filter.extensions.size(); i++) {
				if (i > 0) {
					patterns += ";";
				}
				patterns += "*." + filter.extensions[i];
			}

			std::string out = filter.description + " (" + patterns + ")";
			out.push_back('\0');
			out += patterns;
			out.push_back('\0');
			return out;
		}
	}

	/// notify
	void notify(const MessageKind kind, const std::string& title, const std::string& message) {
		const UINT icon = (kind == MessageKind::Error) ? MB_ICONERROR : (kind == MessageKind::Warning) ? MB_ICONWARNING : MB_ICONINFORMATION;
		MessageBoxA(owner_window(), message.c_str(), title.c_str(), MB_OK | icon);
	}

	/// confirm
	void confirm(const std::string& title, const std::string& message, std::function<void()> on_yes) {
		if (MessageBoxA(owner_window(), message.c_str(), title.c_str(), MB_YESNO | MB_ICONQUESTION) == IDYES) {
			on_yes();
		}
	}

	/// pick_open_file
	void pick_open_file(const std::string& title, const FileFilter& filter, const std::string& initial_dir, std::function<void(const std::string&)> on_picked) {
		char file_name[MAX_PATH] = {};
		const std::string filter_string = make_filter_string(filter);

		OPENFILENAMEA ofn = {};
		ofn.lStructSize = sizeof(ofn);
		ofn.hwndOwner = owner_window();
		ofn.lpstrFile = file_name;
		ofn.nMaxFile = MAX_PATH;
		ofn.lpstrFilter = filter_string.c_str();
		ofn.lpstrInitialDir = initial_dir.c_str();
		ofn.lpstrTitle = title.c_str();
		// OFN_NOCHANGEDIR: every relative path (assets, level saves, imgui.ini) resolves against the CWD,
		// which the dialog would otherwise move to the last-browsed folder.
		ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;

		if (!GetOpenFileNameA(&ofn)) {
			return;
		}
		on_picked(file_name);
	}

	/// pick_save_file
	void pick_save_file(const std::string& title, const FileFilter& filter, const std::string& initial_dir, std::function<void(const std::string&)> on_picked) {
		char file_name[MAX_PATH] = {};
		const std::string filter_string = make_filter_string(filter);

		OPENFILENAMEA ofn = {};
		ofn.lStructSize = sizeof(ofn);
		ofn.hwndOwner = owner_window();
		ofn.lpstrFile = file_name;
		ofn.nMaxFile = MAX_PATH;
		ofn.lpstrFilter = filter_string.c_str();
		ofn.lpstrInitialDir = initial_dir.c_str();
		ofn.lpstrTitle = title.c_str();
		ofn.Flags = OFN_NOCHANGEDIR;

		if (!GetSaveFileNameA(&ofn)) {
			return;
		}
		on_picked(file_name);
	}

	/// key_down
	bool key_down(const Key key) {
		int virtual_key = 0;
		switch (key) {
			case Key::W: virtual_key = 'W'; break;
			case Key::A: virtual_key = 'A'; break;
			case Key::S: virtual_key = 'S'; break;
			case Key::D: virtual_key = 'D'; break;
			case Key::V: virtual_key = 'V'; break;
			case Key::Backspace: virtual_key = VK_BACK; break;
			case Key::LeftCtrl: virtual_key = VK_LCONTROL; break;
			case Key::LeftShift: virtual_key = VK_LSHIFT; break;
		}
		return (GetAsyncKeyState(virtual_key) & 0x8000) != 0;
	}

}
