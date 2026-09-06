/// workbench_panel.cpp
///
/// 2026 blk

#include "blk_core.h"
#include "workbench_panel.h"
#include "kbEditor.h"
#include "kbEditorEntity.h"
#include "resources_panel.h"
#include "type_info.h"
#include "imgui.h"

/// InputTextCallback_StdString
///
/// Dear ImGui's InputText() takes a fixed C buffer; the official std::string
/// helper (misc/cpp/imgui_stdlib.h/.cpp) isn't vendored in External/imgui, so
/// this reimplements its resize-callback trick locally rather than pulling in
/// a fixed-size buffer with an arbitrary length cap.
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
/// Appends one log entry to a clipboard buffer, newline-terminated exactly
/// once. kbEditor::OutputCB stores whatever string the caller passed, and most
/// blk::log()/blk::warn() call sites already end theirs with '\n' -- appending
/// unconditionally double-spaces the copied text for those.
static void AppendLogLine(std::string& out, const std::string& text) {
	out += text;
	if (text.empty() || text.back() != '\n') {
		out += '\n';
	}
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

	// Phase 3, Milestone 4: everything in this menu bar that can trigger a
	// resource load (level load/save eagerly loads textures, component
	// construction could too) or otherwise touch the renderer must be
	// deferred via DeferAction() -- see its declaration in kbEditor.h. This
	// menu bar draws mid-frame, inside the D3D12 command list's recording,
	// where Renderer_Dx12::load_texture()'s command-allocator Reset() is
	// illegal. FLTK's menu bar never had this problem since its callbacks
	// fired from Windows' message dispatch, strictly before render().
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
			const std::map<std::string, const kbTypeInfoClass*>& componentMap = g_NameToTypeInfoMap->GetClassMap();
			for (auto iter = componentMap.begin(); iter != componentMap.end(); ++iter) {
				const kbTypeInfoClass* const typeInfo = iter->second;
				if (ImGui::MenuItem(typeInfo->GetClassNameA().c_str())) {
					g_Editor->DeferAction([typeInfo]() { kbEditor::add_component(typeInfo); });
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
	// Sit exactly on the menu bar's bottom edge. Not kbEditor::MenuBarHeight():
	// that is a legacy layout constant of 20, while the real main menu bar is
	// GetFrameHeight() tall (BeginMainMenuBar uses precisely that), which is 19
	// with the current font and frame padding. The 1px disagreement left a
	// hairline of the 3D scene showing between the two bars, and would drift
	// again on any font or DPI change.
	ImGui::SetNextWindowPos(ImVec2(0.0f, ImGui::GetFrameHeight()), ImGuiCond_Always);
	ImGui::SetNextWindowSize(ImVec2(ImGui::GetIO().DisplaySize.x, (float)kbEditor::ToolbarHeight()), ImGuiCond_Always);

	// NoDocking: this is fixed chrome, positioned against io.DisplaySize rather
	// than living in kbEditor::DrawDockSpace()'s dockspace. Without the flag it
	// is still a valid docking *target*, so a panel dragged near it docks into
	// it and lands outside the dockspace entirely.
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
			const bool isSelected = (i == g_Editor->m_CamSpeedIdx);
			if (ImGui::Selectable(kbEditor::CamSpeedBindingName(i), isSelected)) {
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
	static const char* const viewModeNames[] = { "Shaded", "Wireframe", "Color", "Normals", "Specular", "Depth" };
	ImGui::Combo("##ViewMode", &g_Editor->m_ViewModeIdx, viewModeNames, 6);

	ImGui::End();
}

/// WorkbenchPanel::DrawOutputLog
void WorkbenchPanel::DrawOutputLog() {
	extern std::vector<LogEntry> g_OutputLog;

	// An ordinary dockable window now, tabbed alongside Resources in the bottom
	// dock by kbEditor::DrawDockSpace()'s default layout -- the pairing Unreal
	// uses for its Content Browser and Output Log. It was pinned chrome
	// (absolute position against io.DisplaySize, NoMove/NoDocking) up until
	// there was a dockspace able to hold it. The title is visible rather than
	// "##"-hidden because it is now a dock tab label.
	ImGui::SetNextWindowPos(ImVec2(20.0f, 620.0f), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSize(ImVec2(900.0f, 200.0f), ImGuiCond_FirstUseEver);
	ImGui::Begin("Output Log");

	if (ImGui::Button("Copy Selected")) {
		CopyOutputLogSelection();
	}

	ImGui::SameLine();
	if (ImGui::Button("Copy All")) {
		RebuildOutputLogCache();
		ImGui::SetClipboardText(m_OutputLogCache.c_str());
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

	// g_OutputLog can be cleared from outside this panel, which would leave the
	// stored indices dangling past the end.
	const int log_count = (int)g_OutputLog.size();
	if (m_LogSelAnchor >= log_count || m_LogSelEnd >= log_count) {
		m_LogSelAnchor = -1;
		m_LogSelEnd = -1;
	}

	// A child region so the auto-scroll below measures the log's own scrolling
	// area rather than the whole window, whose scroll position the button row
	// above would otherwise skew.
	ImGui::BeginChild("##OutputLogScroll", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar);

	const bool has_selection = (m_LogSelAnchor >= 0 && m_LogSelEnd >= 0);
	const int sel_first = (!has_selection) ? -1 : ((m_LogSelAnchor < m_LogSelEnd) ? m_LogSelAnchor : m_LogSelEnd);
	const int sel_last = (!has_selection) ? -1 : ((m_LogSelAnchor < m_LogSelEnd) ? m_LogSelEnd : m_LogSelAnchor);

	for (int i = 0; i < log_count; i++) {
		const LogEntry& entry = g_OutputLog[i];
		const bool is_error = (entry.type != kbOutputMessageType_t::Message_Normal);

		// Coloring the Selectable's own text rather than drawing separate
		// TextColored keeps the error highlight that the pre-selection log had.
		if (is_error) {
			ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
		}

		// ImGui derives an item ID from its label, and log lines repeat freely
		// -- the same collision that made duplicate entity names ambiguous in
		// outliner_panel. PushID(i) keeps every row distinct.
		ImGui::PushID(i);

		// Span at least the visible width so short lines are still easy to
		// click, and the full text width when longer so the highlight covers
		// what the horizontal scrollbar reveals.
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

	// Auto-scroll to the newest entry, but only if already at the bottom --
	// otherwise new log spam would yank the view away from whatever the user
	// scrolled up to read.
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

	// Replaces RightClickOnOutputWindow()/ClearOutputBuffer().
	if (ImGui::BeginPopupContextWindow()) {
		if (ImGui::MenuItem("Copy Selected", "Ctrl+C", false, has_selection)) {
			CopyOutputLogSelection();
		}
		if (ImGui::MenuItem("Copy All")) {
			RebuildOutputLogCache();
			ImGui::SetClipboardText(m_OutputLogCache.c_str());
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
/// Copies the selected rows to the clipboard, or the whole log when nothing is
/// selected -- so Ctrl+C is never a no-op. Text is copied verbatim, with no
/// severity prefix, so what lands on the clipboard is exactly what was logged.
void WorkbenchPanel::CopyOutputLogSelection() {
	extern std::vector<LogEntry> g_OutputLog;

	if (m_LogSelAnchor < 0 || m_LogSelEnd < 0) {
		RebuildOutputLogCache();
		ImGui::SetClipboardText(m_OutputLogCache.c_str());
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

/// WorkbenchPanel::RebuildOutputLogCache
///
/// Flattens g_OutputLog into m_OutputLogCache for the selectable text box and
/// the clipboard. Keyed on entry count: the log is append-only apart from
/// Clear, and both directions of a size change invalidate the cache, so this
/// rebuilds exactly when it has to instead of once per frame.
void WorkbenchPanel::RebuildOutputLogCache() {
	extern std::vector<LogEntry> g_OutputLog;

	if (m_OutputLogCachedCount == g_OutputLog.size() && !m_OutputLogCache.empty()) {
		return;
	}

	m_OutputLogCachedCount = g_OutputLog.size();

	size_t total = 0;
	for (const LogEntry& entry : g_OutputLog) {
		total += entry.text.size() + 1;
	}

	m_OutputLogCache.clear();
	m_OutputLogCache.reserve(total);
	for (const LogEntry& entry : g_OutputLog) {
		AppendLogLine(m_OutputLogCache, entry.text);
	}
}

/// WorkbenchPanel::DrawAddPrefabPopup
///
/// Phase 3, Milestone 4 Step 3: replaces kbDialogBox's blocking Fl::wait()
/// loop. kbEditor::AddEntityAsPrefab() (its only call site is
/// RightClickOnViewport's still-FLTK context menu, which stays as-is -- out
/// of scope for this milestone) can't call ImGui::OpenPopup() itself -- it
/// fires during Windows' message pump, outside any active ImGui frame, and
/// OpenPopup() needs a valid window/ID-stack context. It sets
/// m_bWantOpenAddPrefabPopup instead; this method (running inside
/// draw_imgui(), where that context exists) opens the popup on its behalf.
/// Fixes the pre-existing bug where Cancel still created the prefab:
/// AddEntityAsPrefab_Internal is now only reachable from the Save button
/// below, never on Cancel/close.
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
		std::string packageName = s_Package;
		if (GetFileExtension(packageName) != "kbPkg") {
			packageName += ".kbPkg";
		}

		const std::string folderName = s_Folder;
		const std::string prefabName = s_Name;
		g_Editor->DeferAction([packageName, folderName, prefabName]() {
			g_Editor->AddEntityAsPrefab_Internal(packageName, folderName, prefabName);
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
/// Phase 3, Milestone 8: replaces kbEditor::RightClickOnViewport()'s
/// Fl_Menu_Item[]::popup() -- the viewport's Duplicate / Create New Prefab /
/// Replace Prefab / Place Prefab menu, and the last FLTK widget in the editor.
/// kbEditor raises m_bWantOpenViewportContextMenu from the WndProc, outside
/// any active ImGui frame where OpenPopup() can't be called; this opens and
/// draws it. Labels are rebuilt every frame rather than snapshot at open time,
/// so they track the live selection and prefab. Every action is deferred: this
/// runs mid-frame inside the D3D12 command list's recording, where anything
/// that loads a resource is illegal -- see kbEditor::DeferAction(). The FLTK
/// original's enable/disable rules are kept verbatim, including "Replace
/// Prefab" gating on selection count only (the callback null-checks the
/// prefab itself) and the run-together label text.
void WorkbenchPanel::DrawViewportContextMenu() {
	if (g_Editor->m_bWantOpenViewportContextMenu) {
		g_Editor->m_bWantOpenViewportContextMenu = false;
		ImGui::OpenPopup("##ViewportContextMenu");
	}

	if (!ImGui::BeginPopup("##ViewportContextMenu")) {
		return;
	}

	const kbPrefab* const prefab = g_Editor->GetCurrentlySelectedPrefab();
	const std::vector<kbEditorEntity*>& selectedObjects = g_Editor->GetSelectedObjects();
	const bool bSingleSelection = (selectedObjects.size() == 1);

	std::string DuplicateMessage = "Duplicate Entity";
	if (selectedObjects.size() > 0) {
		DuplicateMessage += selectedObjects[0]->GetGameEntity()->name().stl_str();
	}

	std::string ReplacePrefabMessage = "Replace Prefab";
	std::string PlacePrefabMessage = "Place Prefab";
	if (!prefab) {
		PlacePrefabMessage += "into scene";
	} else {
		PlacePrefabMessage += "[" + prefab->GetPrefabName() + "] into scene.";
		ReplacePrefabMessage += "[" + prefab->GetPrefabName() + "]";
	}

	if (ImGui::MenuItem(DuplicateMessage.c_str(), nullptr, false, bSingleSelection)) {
		g_Editor->DeferAction([]() { kbEditor::DuplicateEntity(); });
	}
	if (ImGui::MenuItem("Create New Prefab", nullptr, false, bSingleSelection)) {
		g_Editor->DeferAction([]() { kbEditor::AddEntityAsPrefab(); });
	}
	if (ImGui::MenuItem(ReplacePrefabMessage.c_str(), nullptr, false, bSingleSelection)) {
		g_Editor->DeferAction([]() { kbEditor::ReplaceCurrentlySelectedPrefab(); });
	}
	if (ImGui::MenuItem(PlacePrefabMessage.c_str(), nullptr, false, prefab != nullptr)) {
		g_Editor->DeferAction([]() { kbEditor::InsertSelectedPrefabIntoScene(); });
	}

	ImGui::EndPopup();
}
