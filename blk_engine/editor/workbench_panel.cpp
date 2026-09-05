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

// Dear ImGui's InputText() takes a fixed C buffer; the official std::string
// helper (misc/cpp/imgui_stdlib.h/.cpp) isn't vendored in External/imgui, so
// this reimplements its resize-callback trick locally rather than pulling in
// a fixed-size buffer with an arbitrary length cap.
static int InputTextCallback_StdString(ImGuiInputTextCallbackData* const data) {
	if (data->EventFlag == ImGuiInputTextFlags_CallbackResize) {
		std::string* const str = (std::string*)data->UserData;
		str->resize(data->BufTextLen);
		data->Buf = str->data();
	}
	return 0;
}

static bool InputTextStdString(const char* const label, std::string* const str) {
	return ImGui::InputText(label, str->data(), str->capacity() + 1, ImGuiInputTextFlags_CallbackResize, InputTextCallback_StdString, str);
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
	if (ImGui::BeginMainMenuBar() == false) {
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

	for (const LogEntry& entry : g_OutputLog) {
		if (entry.type == kbOutputMessageType_t::Message_Normal) {
			ImGui::TextUnformatted(entry.text.c_str());
		} else {
			ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "%s", entry.text.c_str());
		}
	}

	// Auto-scroll to the newest entry, but only if already at the bottom --
	// otherwise new log spam would yank the view away from whatever the user
	// scrolled up to read.
	if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 1.0f) {
		ImGui::SetScrollHereY(1.0f);
	}

	// Replaces RightClickOnOutputWindow()/ClearOutputBuffer().
	if (ImGui::BeginPopupContextWindow()) {
		if (ImGui::MenuItem("Clear Output")) {
			g_OutputLog.clear();
		}
		ImGui::EndPopup();
	}

	ImGui::End();
}

/// WorkbenchPanel::DrawAddPrefabPopup
///
/// Phase 3, Milestone 4 Step 3: replaces kbDialogBox's blocking Fl::wait()
/// loop. kbEditor::AddEntityAsPrefab() (its only call site is
/// RightClickOnMainTab's still-FLTK context menu, which stays as-is -- out
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
/// Phase 3, Milestone 8: replaces kbEditor::RightClickOnMainTab()'s
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

	if (ImGui::BeginPopup("##ViewportContextMenu") == false) {
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
	if (prefab == nullptr) {
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
