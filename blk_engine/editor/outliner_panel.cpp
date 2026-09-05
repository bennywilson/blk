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

	// A selection made anywhere but here -- a viewport click-to-select, an
	// undo, a prefab drop -- can land on a row scrolled out of view, leaving
	// the Outliner looking like nothing happened. Notice the change and scroll
	// that row into view once, while it is being drawn below. Selection is
	// single-entity throughout the editor, so the first entry is the one to
	// reveal.
	const kbEditorEntity* const primary_selected = selected.empty() ? nullptr : selected[0];
	if (primary_selected != m_LastSelectedEntity) {
		m_LastSelectedEntity = primary_selected;
		m_bScrollToSelection = (primary_selected != nullptr);
	}

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

			// Adopt the new selection without asking for a scroll: the row the
			// user just clicked is by definition already visible, and
			// recentring it would yank the list out from under the click.
			m_LastSelectedEntity = entity;
			m_bScrollToSelection = false;
		}

		// After the Selectable, not before -- SetScrollHereY() acts on the item
		// most recently submitted.
		if (m_bScrollToSelection && entity == primary_selected) {
			ImGui::SetScrollHereY(0.5f);
			m_bScrollToSelection = false;
		}

		ImGui::PopID();
	}

	// The selected entity may never have been drawn this frame -- it can be
	// hidden, or gone from the entity list entirely -- so make sure a pending
	// request can't persist into later frames and hijack a scroll then.
	m_bScrollToSelection = false;

	ImGui::End();
}
