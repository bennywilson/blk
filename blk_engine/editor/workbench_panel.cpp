/// workbench_panel.cpp
///
/// 2026 blk

#include "blk_core.h"
#include "workbench_panel.h"
#include "kbEditor.h"
#include "kbEditorEntity.h"
#include "imgui.h"

extern std::vector<LogEntry> g_OutputLog;

/// InputTextCallback_StdString
///
/// Grows the std::string on ImGui's resize callback, since misc/cpp/imgui_stdlib isn't vendored.
static int InputTextCallback_StdString(ImGuiInputTextCallbackData* const data) {
	if (data->EventFlag == ImGuiInputTextFlags_CallbackResize) {
		std::string* const str = (std::string*)data->UserData;
		str->resize(data->BufTextLen);
		data->Buf = str->data();
	}
	return 0;
}

/// InputTextStdString
static bool InputTextStdString(const char* const label, std::string* const str) {
	return ImGui::InputText(label, str->data(), str->capacity() + 1, ImGuiInputTextFlags_CallbackResize, InputTextCallback_StdString, str);
}

/// AppendLogLine
///
/// Appends text newline-terminated exactly once, since most blk::log() call sites already end with '\n'.
static void AppendLogLine(std::string& out, const std::string& text) {
	out += text;
	if (text.empty() || text.back() != '\n') {
		out += '\n';
	}
}

/// FlattenOutputLog
static std::string FlattenOutputLog() {
	size_t total = 0;
	for (const LogEntry& entry : g_OutputLog) {
		total += entry.text.size() + 1;
	}

	std::string flattened;
	flattened.reserve(total);
	for (const LogEntry& entry : g_OutputLog) {
		AppendLogLine(flattened, entry.text);
	}
	return flattened;
}

/// WorkbenchPanel::draw_imgui
void WorkbenchPanel::draw_imgui() {
	DrawMainMenuBar();
	DrawToolbar();
	DrawOutputLog();
	DrawAddPrefabPopup();
	DrawViewportContextMenu();
}

/// WorkbenchPanel::DrawMainMenuBar
void WorkbenchPanel::DrawMainMenuBar() {
	if (!ImGui::BeginMainMenuBar()) {
		return;
	}

	// Defers every action, since this draws mid-frame while the D3D12 command list records and a resource load would Reset() its allocator.
	if (ImGui::BeginMenu("File")) {
		if (ImGui::MenuItem("New Level", "Ctrl+N")) {
			g_Editor->DeferAction([]() { kbEditor::NewLevel(); });
		}
		if (ImGui::MenuItem("Open Level", "Ctrl+O")) {
			g_Editor->DeferAction([]() { kbEditor::OpenLevel(); });
		}
		if (ImGui::MenuItem("Save Level As")) {
			g_Editor->DeferAction([]() { kbEditor::SaveLevelAs(); });
		}
		if (ImGui::MenuItem("Save", "Ctrl+S")) {
			g_Editor->DeferAction([]() { kbEditor::SaveLevel(); });
		}
		ImGui::Separator();
		if (ImGui::MenuItem("Quit")) {
			g_Editor->DeferAction([]() { kbEditor::Close(); });
		}
		ImGui::EndMenu();
	}

	if (ImGui::BeginMenu("Edit")) {
		if (ImGui::MenuItem("Undo", "Ctrl+Z")) {
			g_Editor->DeferAction([]() { kbEditor::Undo(); });
		}
		if (ImGui::MenuItem("Redo", "Ctrl+Y")) {
			g_Editor->DeferAction([]() { kbEditor::Redo(); });
		}
		if (ImGui::MenuItem("Delete", "Del")) {
			g_Editor->DeferAction([]() { kbEditor::DeleteEntitiesCB(); });
		}
		ImGui::EndMenu();
	}

	if (ImGui::BeginMenu("Add")) {
		if (ImGui::MenuItem("Entity")) {
			g_Editor->DeferAction([]() { kbEditor::CreateGameEntity(); });
		}

		if (ImGui::BeginMenu("Component")) {
			const std::map<std::string, const kbTypeInfoClass*>& component_map = g_NameToTypeInfoMap->GetClassMap();
			for (auto iter = component_map.begin(); iter != component_map.end(); ++iter) {
				const kbTypeInfoClass* const type_info = iter->second;
				if (ImGui::MenuItem(type_info->GetClassNameA().c_str())) {
					g_Editor->DeferAction([type_info]() { kbEditor::add_component(type_info); });
				}
			}
			ImGui::EndMenu();
		}
		ImGui::EndMenu();
	}

	if (ImGui::BeginMenu("Play")) {
		if (ImGui::MenuItem("Play Game From Here", "Ctrl+P")) {
			g_Editor->DeferAction([]() { kbEditor::PlayGameFromHere(); });
		}
		if (ImGui::MenuItem("Stop Game", "Ctrl+Q")) {
			g_Editor->DeferAction([]() { kbEditor::StopGame(); });
		}
		ImGui::EndMenu();
	}

	ImGui::EndMainMenuBar();
}

/// WorkbenchPanel::DrawToolbar
void WorkbenchPanel::DrawToolbar() {
	// Positions at GetFrameHeight(), the exact height BeginMainMenuBar uses, since a fixed constant leaves a seam.
	ImGui::SetNextWindowPos(ImVec2(0.0f, ImGui::GetFrameHeight()), ImGuiCond_Always);
	ImGui::SetNextWindowSize(ImVec2(ImGui::GetIO().DisplaySize.x, (float)kbEditor::ToolbarHeight()), ImGuiCond_Always);

	// NoDocking: fixed chrome outside the dockspace is still a docking target, and a panel docked into it lands outside the dockspace.
	constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
		ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar |
		ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoDocking;

	ImGui::Begin("##Toolbar", nullptr, flags);

	if (ImGui::Button("T")) {
		kbEditor::TranslationButtonCB();
	}
	ImGui::SameLine();
	if (ImGui::Button("R")) {
		kbEditor::RotationButtonCB();
	}
	ImGui::SameLine();
	if (ImGui::Button("S")) {
		kbEditor::ScaleButtonCB();
	}

	ImGui::SameLine();
	ImGui::SetNextItemWidth(60.0f);
	ImGui::InputFloat("##XFormAmount", &g_Editor->m_XFormAmount, 0.0f, 0.0f, "%.2f");

	ImGui::SameLine();
	if (ImGui::Button("X+")) {
		kbEditor::XPlusAdjustButtonCB();
	}
	ImGui::SameLine();
	if (ImGui::Button("X-")) {
		kbEditor::XNegAdjustButtonCB();
	}
	ImGui::SameLine();
	if (ImGui::Button("Y+")) {
		kbEditor::YPlusAdjustButtonCB();
	}
	ImGui::SameLine();
	if (ImGui::Button("Y-")) {
		kbEditor::YNegAdjustButtonCB();
	}
	ImGui::SameLine();
	if (ImGui::Button("Z+")) {
		kbEditor::ZPlusAdjustButtonCB();
	}
	ImGui::SameLine();
	if (ImGui::Button("Z-")) {
		kbEditor::ZNegAdjustButtonCB();
	}

	ImGui::SameLine();
	ImGui::SetNextItemWidth(100.0f);
	if (ImGui::BeginCombo("Cam Speed", kbEditor::CamSpeedBindingName(g_Editor->m_CamSpeedIdx))) {
		for (int i = 0; i < kbEditor::NumCamSpeedBindings(); i++) {
			const bool is_selected = (i == g_Editor->m_CamSpeedIdx);
			if (ImGui::Selectable(kbEditor::CamSpeedBindingName(i), is_selected)) {
				g_Editor->SetCamSpeedIndex(i);
			}
		}
		ImGui::EndCombo();
	}

	ImGui::SameLine();
	if (ImGui::Button("Toggle Icons")) {
		kbEditor::ToggleIconsCB();
	}

	ImGui::SameLine();
	ImGui::SetNextItemWidth(100.0f);
	static const char* const view_mode_names[] = { "Shaded", "Wireframe", "Color", "Normals", "Specular", "Depth" };
	ImGui::Combo("##ViewMode", &g_Editor->m_ViewModeIdx, view_mode_names, IM_ARRAYSIZE(view_mode_names));

	ImGui::End();
}

/// WorkbenchPanel::DrawOutputLog
void WorkbenchPanel::DrawOutputLog() {
	// Uses a visible title because it's a dock tab beside Resources.
	ImGui::SetNextWindowPos(ImVec2(20.0f, 620.0f), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSize(ImVec2(900.0f, 200.0f), ImGuiCond_FirstUseEver);
	ImGui::Begin("Output Log");

	if (ImGui::Button("Copy Selected")) {
		CopyOutputLogSelection();
	}

	ImGui::SameLine();
	if (ImGui::Button("Copy All")) {
		ImGui::SetClipboardText(FlattenOutputLog().c_str());
	}

	ImGui::SameLine();
	if (ImGui::Button("Clear")) {
		g_OutputLog.clear();
		m_LogSelAnchor = -1;
		m_LogSelEnd = -1;
	}

	ImGui::SameLine();
	ImGui::TextDisabled("click to select, shift-click to extend, Ctrl+A / Ctrl+C");

	ImGui::Separator();

	// Resets indices left past the end when g_OutputLog is cleared elsewhere.
	const int log_count = (int)g_OutputLog.size();
	if (m_LogSelAnchor >= log_count || m_LogSelEnd >= log_count) {
		m_LogSelAnchor = -1;
		m_LogSelEnd = -1;
	}

	// Scrolls a child region so auto-scroll measures the log alone, not the button row.
	ImGui::BeginChild("##OutputLogScroll", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar);

	const bool has_selection = (m_LogSelAnchor >= 0 && m_LogSelEnd >= 0);
	const int sel_first = (!has_selection) ? -1 : ((m_LogSelAnchor < m_LogSelEnd) ? m_LogSelAnchor : m_LogSelEnd);
	const int sel_last = (!has_selection) ? -1 : ((m_LogSelAnchor < m_LogSelEnd) ? m_LogSelEnd : m_LogSelAnchor);

	for (int i = 0; i < log_count; i++) {
		const LogEntry& entry = g_OutputLog[i];
		const bool is_error = (entry.type != kbOutputMessageType_t::Message_Normal);

		// Colors the Selectable's own text to keep error highlighting.
		if (is_error) {
			ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
		}

		// Pushes the row index as ID, since log lines repeat and ImGui IDs derive from labels.
		ImGui::PushID(i);

		// Spans the wider of the visible width and the text, so short rows stay clickable and long ones highlight fully.
		float row_width = ImGui::CalcTextSize(entry.text.c_str()).x;
		const float avail_width = ImGui::GetContentRegionAvail().x;
		if (row_width < avail_width) {
			row_width = avail_width;
		}

		const bool selected = (has_selection && i >= sel_first && i <= sel_last);
		if (ImGui::Selectable(entry.text.c_str(), selected, 0, ImVec2(row_width, 0.0f))) {
			if (ImGui::GetIO().KeyShift && m_LogSelAnchor >= 0) {
				m_LogSelEnd = i;
			} else {
				m_LogSelAnchor = i;
				m_LogSelEnd = i;
			}
		}

		ImGui::PopID();

		if (is_error) {
			ImGui::PopStyleColor();
		}
	}

	// Auto-scrolls only when already at the bottom, so new entries don't yank the view while reading.
	if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 1.0f) {
		ImGui::SetScrollHereY(1.0f);
	}

	if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) {
		const ImGuiIO& io = ImGui::GetIO();
		if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_A) && log_count > 0) {
			m_LogSelAnchor = 0;
			m_LogSelEnd = log_count - 1;
		}
		if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_C)) {
			CopyOutputLogSelection();
		}
	}

	ImGui::EndChild();

	if (ImGui::BeginPopupContextWindow()) {
		if (ImGui::MenuItem("Copy Selected", "Ctrl+C", false, has_selection)) {
			CopyOutputLogSelection();
		}
		if (ImGui::MenuItem("Copy All")) {
			ImGui::SetClipboardText(FlattenOutputLog().c_str());
		}
		if (ImGui::MenuItem("Clear Output")) {
			g_OutputLog.clear();
			m_LogSelAnchor = -1;
			m_LogSelEnd = -1;
		}
		ImGui::EndPopup();
	}

	ImGui::End();
}

/// WorkbenchPanel::CopyOutputLogSelection
///
/// Copies the selected rows verbatim, or the whole log when nothing is selected, so Ctrl+C is never a no-op.
void WorkbenchPanel::CopyOutputLogSelection() {
	if (m_LogSelAnchor < 0 || m_LogSelEnd < 0) {
		ImGui::SetClipboardText(FlattenOutputLog().c_str());
		return;
	}

	const int first = (m_LogSelAnchor < m_LogSelEnd) ? m_LogSelAnchor : m_LogSelEnd;
	const int last = (m_LogSelAnchor < m_LogSelEnd) ? m_LogSelEnd : m_LogSelAnchor;

	std::string selection;
	for (int i = first; i <= last && i < (int)g_OutputLog.size(); i++) {
		AppendLogLine(selection, g_OutputLog[i].text);
	}

	ImGui::SetClipboardText(selection.c_str());
}

/// WorkbenchPanel::DrawAddPrefabPopup
///
/// Opens the popup when kbEditor raises m_bWantOpenAddPrefabPopup, since OpenPopup() needs an active ImGui frame.
/// Only Save creates the prefab.
void WorkbenchPanel::DrawAddPrefabPopup() {
	if (g_Editor->m_bWantOpenAddPrefabPopup) {
		g_Editor->m_bWantOpenAddPrefabPopup = false;
		ImGui::OpenPopup("Add Prefab To Library Package");
	}

	static std::string s_Package;
	static std::string s_Folder;
	static std::string s_Name;

	if (!ImGui::BeginPopupModal("Add Prefab To Library Package", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
		return;
	}

	InputTextStdString("Package:", &s_Package);
	InputTextStdString("Folder:", &s_Folder);
	InputTextStdString("Name:", &s_Name);

	if (ImGui::Button("Save Prefab")) {
		std::string package_name = s_Package;
		if (GetFileExtension(package_name) != "kbPkg") {
			package_name += ".kbPkg";
		}

		const std::string folder_name = s_Folder;
		const std::string prefab_name = s_Name;
		g_Editor->DeferAction([package_name, folder_name, prefab_name]() {
			g_Editor->AddEntityAsPrefab_Internal(package_name, folder_name, prefab_name);
		});

		s_Package.clear();
		s_Folder.clear();
		s_Name.clear();
		ImGui::CloseCurrentPopup();
	}

	ImGui::SameLine();
	if (ImGui::Button("Cancel")) {
		s_Package.clear();
		s_Folder.clear();
		s_Name.clear();
		ImGui::CloseCurrentPopup();
	}

	ImGui::EndPopup();
}

/// WorkbenchPanel::DrawViewportContextMenu
///
/// Opens when kbEditor raises m_bWantOpenViewportContextMenu, rebuilding labels each frame to track the live selection.
/// Defers every action, since resource loads are illegal mid-frame.
void WorkbenchPanel::DrawViewportContextMenu() {
	if (g_Editor->m_bWantOpenViewportContextMenu) {
		g_Editor->m_bWantOpenViewportContextMenu = false;
		ImGui::OpenPopup("##ViewportContextMenu");
	}

	if (!ImGui::BeginPopup("##ViewportContextMenu")) {
		return;
	}

	const kbPrefab* const prefab = g_Editor->GetCurrentlySelectedPrefab();
	const std::vector<kbEditorEntity*>& selected_objects = g_Editor->GetSelectedObjects();
	const bool is_single_selection = (selected_objects.size() == 1);

	std::string duplicate_label = "Duplicate Entity";
	if (!selected_objects.empty()) {
		duplicate_label += " " + selected_objects[0]->GetGameEntity()->name().stl_str();
	}

	std::string replace_prefab_label = "Replace Prefab";
	std::string place_prefab_label = "Place Prefab";
	if (!prefab) {
		place_prefab_label += " into scene";
	} else {
		place_prefab_label += " [" + prefab->GetPrefabName() + "] into scene";
		replace_prefab_label += " [" + prefab->GetPrefabName() + "]";
	}

	if (ImGui::MenuItem(duplicate_label.c_str(), nullptr, false, is_single_selection)) {
		g_Editor->DeferAction([]() { kbEditor::DuplicateEntity(); });
	}
	if (ImGui::MenuItem("Create New Prefab", nullptr, false, is_single_selection)) {
		g_Editor->DeferAction([]() { kbEditor::AddEntityAsPrefab(); });
	}
	if (ImGui::MenuItem(replace_prefab_label.c_str(), nullptr, false, is_single_selection)) {
		g_Editor->DeferAction([]() { kbEditor::ReplaceCurrentlySelectedPrefab(); });
	}
	if (ImGui::MenuItem(place_prefab_label.c_str(), nullptr, false, prefab != nullptr)) {
		g_Editor->DeferAction([]() { kbEditor::InsertSelectedPrefabIntoScene(); });
	}

	ImGui::EndPopup();
}
