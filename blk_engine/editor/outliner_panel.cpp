/// outliner_panel.cpp
///
/// 2026 blk

#include "blk_core.h"
#include "outliner_panel.h"
#include "kbEditor.h"
#include "kbEditorEntity.h"
#include "imgui.h"

/// OutlinerPanel::draw_imgui
void OutlinerPanel::draw_imgui() {
	ImGui::SetNextWindowPos(ImVec2(20, 20), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSize(ImVec2(260, 400), ImGuiCond_FirstUseEver);
	ImGui::Begin("Outliner");

	const std::vector<kbEditorEntity*>& entities = g_Editor->GetGameEntities();
	const std::vector<kbEditorEntity*>& selected = g_Editor->GetSelectedObjects();

	// Scrolls once to a selection made elsewhere (viewport pick, undo, prefab drop).
	// Selection is single-entity, so selected[0] is the row to reveal.
	const kbEditorEntity* const primary_selected = selected.empty() ? nullptr : selected[0];
	if (primary_selected != m_LastSelectedEntity) {
		m_LastSelectedEntity = primary_selected;
		m_bScrollToSelection = (primary_selected != nullptr);
	}

	for (kbEditorEntity* const entity : entities) {
		if (entity->IsHidden()) {
			continue;
		}

		// Keys rows by pointer, since entity names repeat (the test level has two "Crate_2") and ImGui IDs derive from labels.
		ImGui::PushID(entity);

		const bool is_selected = std::find(selected.begin(), selected.end(), entity) != selected.end();
		const char* const entity_name = entity->GetGameEntity()->name().c_str();
		if (ImGui::Selectable(entity_name, is_selected)) {
			std::vector<kbEditorEntity*> pick{ entity };
			g_Editor->SelectEntities(pick, false);

			// Adopts the clicked selection without scrolling, since the clicked row is already visible.
			m_LastSelectedEntity = entity;
			m_bScrollToSelection = false;
		}

		// Scrolls after the Selectable because SetScrollHereY() acts on the last submitted item.
		if (m_bScrollToSelection && entity == primary_selected) {
			ImGui::SetScrollHereY(0.5f);
			m_bScrollToSelection = false;
		}

		ImGui::PopID();
	}

	// Drops an unconsumed scroll request, since the selected entity may be hidden or gone.
	m_bScrollToSelection = false;

	ImGui::End();
}
