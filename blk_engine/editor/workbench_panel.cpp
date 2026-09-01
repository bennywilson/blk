/// workbench_panel.cpp
///
/// 2026 blk

#include "blk_core.h"
#include "workbench_panel.h"
#include "kbEditor.h"
#include "type_info.h"
#include "imgui.h"

/// WorkbenchPanel::draw_imgui
void WorkbenchPanel::draw_imgui() {
	DrawMainMenuBar();
	DrawToolbar();
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
