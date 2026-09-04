/// outliner_panel.cpp
///
/// 2026 blk

#include "blk_core.h"
#include "outliner_panel.h"
#include "kbEditor.h"
#include "kbEditorEntity.h"
#include "entity_header.h"
#include "imgui.h"

/// OutlinerPanel::draw_imgui
void OutlinerPanel::draw_imgui() {
	ImGui::SetNextWindowPos(ImVec2(20, 20), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSize(ImVec2(260, 400), ImGuiCond_FirstUseEver);
	ImGui::Begin("Outliner");

	const std::vector<kbEditorEntity*>& entities = g_Editor->GetGameEntities();
	const std::vector<kbEditorEntity*>& selected = g_Editor->GetSelectedObjects();

	for (kbEditorEntity* const entity : entities) {
		if (entity->IsHidden()) {
			continue;
		}

		// Entity names are not unique -- the test level ships two entities both
		// named "Crate_2" -- and ImGui derives an item's ID from its label, so
		// duplicates collide: ImGui raises "2 visible items with conflicting
		// ID!" and clicking either row is ambiguous. Key off the pointer
		// instead, the same way ResourcesPanel's entity list already does.
		ImGui::PushID(entity);

		const bool is_selected = std::find(selected.begin(), selected.end(), entity) != selected.end();
		const char* const entity_name = entity->GetGameEntity()->name().c_str();
		if (ImGui::Selectable(entity_name, is_selected)) {
			std::vector<kbEditorEntity*> pick{ entity };
			g_Editor->SelectEntities(pick, false);
		}

		ImGui::PopID();
	}

	ImGui::End();
}
