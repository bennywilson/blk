/// workbench_panel.cpp
///
/// 2026 blk

#include "blk_core.h"
#include "workbench_panel.h"
#include "kbEditor.h"
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
			g_Editor->DeferAction([]() { kbEditor::NewLevel(nullptr, nullptr); });
		}
		if (ImGui::MenuItem("Open Level", "Ctrl+O")) {
			g_Editor->DeferAction([]() { kbEditor::OpenLevel(nullptr, nullptr); });
		}
		if (ImGui::MenuItem("Save Level As")) {
			g_Editor->DeferAction([]() { kbEditor::SaveLevelAs(nullptr, nullptr); });
		}
		if (ImGui::MenuItem("Save", "Ctrl+S")) {
			g_Editor->DeferAction([]() { kbEditor::SaveLevel(nullptr, nullptr); });
		}
		ImGui::Separator();
		if (ImGui::MenuItem("Quit")) {
			g_Editor->DeferAction([]() { kbEditor::Close(nullptr, g_Editor); });
		}
		ImGui::EndMenu();
	}

	if (ImGui::BeginMenu("Edit")) {
		if (ImGui::MenuItem("Undo", "Ctrl+Z")) {
			g_Editor->DeferAction([]() { kbEditor::Undo(nullptr, nullptr); });
		}
		if (ImGui::MenuItem("Redo", "Ctrl+Y")) {
			g_Editor->DeferAction([]() { kbEditor::Redo(nullptr, nullptr); });
		}
		if (ImGui::MenuItem("Delete", "Del")) {
			g_Editor->DeferAction([]() { kbEditor::DeleteEntitiesCB(nullptr, nullptr); });
		}
		ImGui::EndMenu();
	}

	if (ImGui::BeginMenu("Add")) {
		if (ImGui::MenuItem("Entity")) {
			g_Editor->DeferAction([]() { kbEditor::CreateGameEntity(nullptr, nullptr); });
		}

		if (ImGui::BeginMenu("Component")) {
			const std::map<std::string, const kbTypeInfoClass*>& componentMap = g_NameToTypeInfoMap->GetClassMap();
			for (auto iter = componentMap.begin(); iter != componentMap.end(); ++iter) {
				const kbTypeInfoClass* const typeInfo = iter->second;
				if (ImGui::MenuItem(typeInfo->GetClassNameA().c_str())) {
					g_Editor->DeferAction([typeInfo]() { kbEditor::add_component(nullptr, (void*)typeInfo); });	// Hack cast - unfortunate, matches the FLTK original
				}
			}
			ImGui::EndMenu();
		}
		ImGui::EndMenu();
	}

	if (ImGui::BeginMenu("Play")) {
		if (ImGui::MenuItem("Play Game From Here", "Ctrl+P")) {
			g_Editor->DeferAction([]() { kbEditor::PlayGameFromHere(nullptr, nullptr); });
		}
		if (ImGui::MenuItem("Stop Game", "Ctrl+Q")) {
			g_Editor->DeferAction([]() { kbEditor::StopGame(nullptr, nullptr); });
		}
		ImGui::EndMenu();
	}

	ImGui::EndMainMenuBar();
}

/// WorkbenchPanel::DrawToolbar
void WorkbenchPanel::DrawToolbar() {
	ImGui::SetNextWindowPos(ImVec2(0.0f, (float)kbEditor::MenuBarHeight()), ImGuiCond_Always);
	ImGui::SetNextWindowSize(ImVec2(ImGui::GetIO().DisplaySize.x, (float)kbEditor::ToolbarHeight()), ImGuiCond_Always);

	constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
		ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar |
		ImGuiWindowFlags_NoBringToFrontOnFocus;

	ImGui::Begin("##Toolbar", nullptr, flags);

	if (ImGui::Button("T")) {
		kbEditor::TranslationButtonCB(nullptr, nullptr);
	}
	ImGui::SameLine();
	if (ImGui::Button("R")) {
		kbEditor::RotationButtonCB(nullptr, nullptr);
	}
	ImGui::SameLine();
	if (ImGui::Button("S")) {
		kbEditor::ScaleButtonCB(nullptr, nullptr);
	}

	ImGui::SameLine();
	ImGui::SetNextItemWidth(60.0f);
	ImGui::InputFloat("##XFormAmount", &g_Editor->m_XFormAmount, 0.0f, 0.0f, "%.2f");

	ImGui::SameLine();
	if (ImGui::Button("X+")) {
		kbEditor::XPlusAdjustButtonCB(nullptr, nullptr);
	}
	ImGui::SameLine();
	if (ImGui::Button("X-")) {
		kbEditor::XNegAdjustButtonCB(nullptr, nullptr);
	}
	ImGui::SameLine();
	if (ImGui::Button("Y+")) {
		kbEditor::YPlusAdjustButtonCB(nullptr, nullptr);
	}
	ImGui::SameLine();
	if (ImGui::Button("Y-")) {
		kbEditor::YNegAdjustButtonCB(nullptr, nullptr);
	}
	ImGui::SameLine();
	if (ImGui::Button("Z+")) {
		kbEditor::ZPlusAdjustButtonCB(nullptr, nullptr);
	}
	ImGui::SameLine();
	if (ImGui::Button("Z-")) {
		kbEditor::ZNegAdjustButtonCB(nullptr, nullptr);
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
		kbEditor::ToggleIconsCB(nullptr, nullptr);
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

	const ImGuiIO& io = ImGui::GetIO();
	ImGui::SetNextWindowPos(ImVec2(0.0f, io.DisplaySize.y - (float)kbEditor::BottomPanelHeight()), ImGuiCond_Always);
	ImGui::SetNextWindowSize(ImVec2(io.DisplaySize.x, (float)kbEditor::BottomPanelHeight()), ImGuiCond_Always);

	constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
		ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoBringToFrontOnFocus;

	ImGui::Begin("##OutputLog", nullptr, flags);

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
