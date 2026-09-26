/// editor_platform_web.cpp
///
/// 2026 blk

#include <algorithm>
#include <emscripten/emscripten.h>
#include <filesystem>
#include <map>
#include "blk_core.h"
#include "editor.h"
#include "editor_platform.h"
#include "imgui.h"

/// The web half of editor_platform: ImGui modals in place of the system's
/// dialogs, and a file browser over Emscripten's virtual filesystem.
///
/// A modal cannot block, so it is queued and drawn on the frames that follow.
/// The answer is handed to Editor::DeferAction rather than run on the spot:
/// draw_modals() runs inside the ImGui frame, where a structural edit to the
/// level would pull entities out from under the panels still being drawn.
namespace editor_platform {

	namespace {
		struct Message {
			bool is_confirm = false;
			std::string title;
			std::string text;
			std::function<void()> on_yes;
		};

		struct FileBrowser {
			bool active = false;
			bool opened_popup = false;
			bool is_save = false;
			std::string title;
			FileFilter filter;
			std::filesystem::path directory;
			char file_name[256] = {};
			std::function<void(const std::string&)> on_picked;
		};

		std::vector<Message> g_messages;
		FileBrowser g_browser;
		std::map<Key, bool> g_keys;

		/// Runs `action` after the frame, or right away if there is no editor to ask.
		void run_after_frame(std::function<void()> action) {
			if (g_Editor != nullptr) {
				g_Editor->DeferAction(std::move(action));
			} else {
				action();
			}
		}

		std::string lower(std::string text) {
			std::transform(text.begin(), text.end(), text.begin(), [](const unsigned char c) { return (char)std::tolower(c); });
			return text;
		}

		bool matches_filter(const std::filesystem::path& path, const FileFilter& filter) {
			const std::string extension = lower(path.extension().string());
			for (const std::string& allowed : filter.extensions) {
				if (extension == "." + lower(allowed)) {
					return true;
				}
			}
			return false;
		}

		void draw_message() {
			Message& message = g_messages.front();
			const std::string popup_id = message.title + "##editor_message";

			if (!ImGui::IsPopupOpen(popup_id.c_str())) {
				ImGui::OpenPopup(popup_id.c_str());
			}

			ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
			if (!ImGui::BeginPopupModal(popup_id.c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
				return;
			}

			ImGui::TextUnformatted(message.text.c_str());
			ImGui::Separator();

			bool answered = false;
			bool yes = false;
			if (message.is_confirm) {
				if (ImGui::Button("Yes")) {
					answered = true;
					yes = true;
				}
				ImGui::SameLine();
				if (ImGui::Button("No")) {
					answered = true;
				}
			} else if (ImGui::Button("OK")) {
				answered = true;
			}

			if (answered) {
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndPopup();

			if (answered) {
				std::function<void()> on_yes = std::move(message.on_yes);
				g_messages.erase(g_messages.begin());
				if (yes && on_yes) {
					run_after_frame(std::move(on_yes));
				}
			}
		}

		void draw_file_browser() {
			FileBrowser& browser = g_browser;
			const std::string popup_id = browser.title + "##editor_file_browser";

			if (!browser.opened_popup) {
				ImGui::OpenPopup(popup_id.c_str());
				browser.opened_popup = true;
			}

			ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
			ImGui::SetNextWindowSize(ImVec2(520, 420), ImGuiCond_Appearing);
			if (!ImGui::BeginPopupModal(popup_id.c_str(), nullptr, ImGuiWindowFlags_NoSavedSettings)) {
				return;
			}

			ImGui::TextUnformatted(browser.directory.string().c_str());
			ImGui::Separator();

			std::vector<std::filesystem::path> folders;
			std::vector<std::filesystem::path> files;
			std::error_code directory_error;
			for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(browser.directory, directory_error)) {
				if (entry.is_directory(directory_error)) {
					folders.push_back(entry.path().filename());
				} else if (matches_filter(entry.path(), browser.filter)) {
					files.push_back(entry.path().filename());
				}
			}
			std::sort(folders.begin(), folders.end());
			std::sort(files.begin(), files.end());

			const float footer_height = ImGui::GetFrameHeightWithSpacing() * (browser.is_save ? 2.0f : 1.0f) + ImGui::GetStyle().ItemSpacing.y;
			if (ImGui::BeginChild("##listing", ImVec2(0, -footer_height), ImGuiChildFlags_Borders)) {
				if (browser.directory.has_parent_path() && browser.directory != browser.directory.root_path() && ImGui::Selectable("[..]")) {
					browser.directory = browser.directory.parent_path();
				}
				for (const std::filesystem::path& folder : folders) {
					const std::string label = "[" + folder.string() + "]";
					if (ImGui::Selectable(label.c_str())) {
						browser.directory /= folder;
						browser.file_name[0] = '\0';
					}
				}
				for (const std::filesystem::path& file : files) {
					const bool selected = file.string() == browser.file_name;
					if (ImGui::Selectable(file.string().c_str(), selected, ImGuiSelectableFlags_AllowDoubleClick)) {
						strncpy(browser.file_name, file.string().c_str(), sizeof(browser.file_name) - 1);
						browser.file_name[sizeof(browser.file_name) - 1] = '\0';
					}
				}
			}
			ImGui::EndChild();

			if (browser.is_save) {
				ImGui::SetNextItemWidth(-1.0f);
				ImGui::InputText("##file_name", browser.file_name, sizeof(browser.file_name));
			}

			bool accepted = false;
			if (ImGui::Button(browser.is_save ? "Save" : "Open") && browser.file_name[0] != '\0') {
				accepted = true;
			}
			ImGui::SameLine();
			const bool cancelled = ImGui::Button("Cancel");

			if (accepted || cancelled) {
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndPopup();

			if (accepted || cancelled) {
				const std::string picked = (browser.directory / browser.file_name).generic_string();
				std::function<void(const std::string&)> on_picked = std::move(browser.on_picked);
				browser = FileBrowser();
				if (accepted && on_picked) {
					run_after_frame([on_picked, picked]() { on_picked(picked); });
				}
			}
		}

		void open_browser(const bool is_save, const std::string& title, const FileFilter& filter, const std::string& initial_dir, std::function<void(const std::string&)> on_picked) {
			if (g_browser.active) {
				return;
			}

			g_browser = FileBrowser();
			g_browser.active = true;
			g_browser.is_save = is_save;
			g_browser.title = title;
			g_browser.filter = filter;
			g_browser.on_picked = std::move(on_picked);

			// The staged filesystem is lowercased end to end, and case-sensitive.
			std::error_code directory_error;
			std::filesystem::path directory = std::filesystem::absolute(lower(initial_dir), directory_error);
			directory = directory.lexically_normal();
			if (!std::filesystem::is_directory(directory, directory_error)) {
				directory = std::filesystem::current_path();
			}
			g_browser.directory = directory;
		}

		Key to_key(const std::string& code, bool& known) {
			known = true;
			if (code == "KeyW") { return Key::W; }
			if (code == "KeyA") { return Key::A; }
			if (code == "KeyS") { return Key::S; }
			if (code == "KeyD") { return Key::D; }
			if (code == "KeyV") { return Key::V; }
			if (code == "Backspace") { return Key::Backspace; }
			if (code == "ControlLeft") { return Key::LeftCtrl; }
			if (code == "ShiftLeft") { return Key::LeftShift; }
			known = false;
			return Key::W;
		}
	}

	/// notify
	void notify(const MessageKind, const std::string& title, const std::string& message) {
		g_messages.push_back({ false, title, message, nullptr });
	}

	/// confirm
	void confirm(const std::string& title, const std::string& message, std::function<void()> on_yes) {
		g_messages.push_back({ true, title, message, std::move(on_yes) });
	}

	/// pick_open_file
	void pick_open_file(const std::string& title, const FileFilter& filter, const std::string& initial_dir, std::function<void(const std::string&)> on_picked) {
		open_browser(false, title, filter, initial_dir, std::move(on_picked));
	}

	/// pick_save_file
	void pick_save_file(const std::string& title, const FileFilter& filter, const std::string& initial_dir, std::function<void(const std::string&)> on_picked) {
		open_browser(true, title, filter, initial_dir, std::move(on_picked));
	}

	/// set_window_title
	void set_window_title(const std::string& title) {
		EM_ASM({ document.title = UTF8ToString($0); }, title.c_str());
	}

	/// window_has_focus
	bool window_has_focus() {
		return EM_ASM_INT({ return document.hasFocus() ? 1 : 0; }) != 0;
	}

	/// show_cursor
	void show_cursor(const bool) {
	}

	/// draw_modals
	void draw_modals() {
		if (!g_messages.empty()) {
			draw_message();
		} else if (g_browser.active) {
			draw_file_browser();
		}
	}

	/// web_key_event
	void web_key_event(const char* const code, const bool down) {
		bool known = false;
		const Key key = to_key(code, known);
		if (known) {
			g_keys[key] = down;
		}
	}

	/// key_down
	bool key_down(const Key key) {
		const auto found = g_keys.find(key);
		return found != g_keys.end() && found->second;
	}

}
