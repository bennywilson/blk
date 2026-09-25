/// editor_platform.h
///
/// 2026 blk

#pragma once

#include <functional>
#include <string>
#include <vector>

/// editor_platform
///
/// What the editor needs from its host that is not ImGui: message boxes, file
/// dialogs and raw key state. Win32 implements them with the system dialogs
/// (editor_platform_win32.cpp); the web build implements them as ImGui modals
/// over the virtual filesystem.
///
/// Every question is asked with a callback rather than a return value, because
/// a web modal cannot block: it resolves on a later frame. The Win32 version
/// blocks and calls the callback before it returns, so existing behaviour is
/// unchanged. Callers must therefore not assume the callback has run when the
/// call returns, and must re-check any pointer they captured when it does -
/// the selection or the level may have changed in between.
namespace editor_platform {

	enum class MessageKind {
		Info,
		Warning,
		Error,
	};

	/// A message with an OK button and nothing to decide.
	void notify(MessageKind kind, const std::string& title, const std::string& message);

	/// Yes/No. `on_yes` runs if the user accepts; nothing runs otherwise.
	void confirm(const std::string& title, const std::string& message, std::function<void()> on_yes);

	struct FileFilter {
		std::string description;
		// Without the dot: { "blklevel", "kblevel" }.
		std::vector<std::string> extensions;
	};

	/// `on_picked` gets the chosen path; it does not run if the user cancels.
	/// The paths are as the platform gives them - absolute on Win32, virtual
	/// filesystem paths on the web.
	void pick_open_file(const std::string& title, const FileFilter& filter, const std::string& initial_dir, std::function<void(const std::string&)> on_picked);
	void pick_save_file(const std::string& title, const FileFilter& filter, const std::string& initial_dir, std::function<void(const std::string&)> on_picked);

	enum class Key {
		W,
		A,
		S,
		D,
		V,
		Backspace,
		LeftCtrl,
		LeftShift,
	};

	/// True while `key` is physically held. Not gated on focus: callers check
	/// Editor::owns_keyboard() first, as they did with GetAsyncKeyState.
	bool key_down(Key key);

}
