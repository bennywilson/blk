/// properties_panel.cpp
///
/// 2026 blk

#include <cfloat>
#include "blk_core.h"
#include "properties_panel.h"
#include "kbEditor.h"
#include "kbEditorEntity.h"
#include "entity_header.h"
#include "type_info.h"
#include "ResourceTab.h"
#include "kbUndoAction.h"
#include "resource_manager.h"
#include "imgui.h"

/// PropertiesPanel::PropertiesPanel
PropertiesPanel::PropertiesPanel(const int x, const int y, const int w, const int h) : EditorPanel(x, y, w, h) {
	g_Editor->RegisterEvent(this, WidgetCB_ResourceSelected);
}

/// PropertiesPanel::EventCB
void PropertiesPanel::EventCB(const widgetCBObject* const widget_cb_object) {
	if (widget_cb_object->widgetType == WidgetCB_ResourceSelected) {
		m_CurrentlySelectedResourceFileName = static_cast<const widgetCBResourceSelected*>(widget_cb_object)->resourceFileName;
	}
}

/// PropertiesPanel::draw_imgui
void PropertiesPanel::draw_imgui() {
	ImGui::SetNextWindowPos(ImVec2(300, 20), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSize(ImVec2(340, 480), ImGuiCond_FirstUseEver);
	ImGui::Begin("Properties");

	const std::vector<kbEditorEntity*>& selected = g_Editor->GetSelectedObjects();
	// Guard against a selected kbEditorEntity whose underlying GameEntity has
	// already been destroyed (e.g. mid level-unload/reload) -- GetGameEntity()
	// can return a dangling pointer in that window, not nullptr, so a plain
	// null check isn't enough. Only trust the selection if it's still present
	// in the live entity list, the same source of truth OutlinerPanel iterates.
	const std::vector<kbEditorEntity*>& live_entities = g_Editor->GetGameEntities();
	const bool selection_is_live = (selected.size() == 1) &&
		(std::find(live_entities.begin(), live_entities.end(), selected[0]) != live_entities.end());
	GameEntity* const game_entity = selection_is_live ? selected[0]->GetGameEntity() : nullptr;
	if (game_entity != nullptr) {
		for (size_t i = 0; i < game_entity->num_components(); i++) {
			ImGui::PushID((int)i);
			DrawComponent(game_entity->component(i));
			ImGui::PopID();
		}
	} else if (selected.size() > 1) {
		ImGui::TextDisabled("Multiple entities selected.");
	} else {
		ImGui::TextDisabled("No entity selected.");
	}

	ImGui::End();
}

/// PropertiesPanel::DrawComponent
void PropertiesPanel::DrawComponent(kbGameComponent* const component) {
	ImGui::PushID(component);
	if (ImGui::CollapsingHeader(component->GetComponentClassName(), ImGuiTreeNodeFlags_DefaultOpen)) {
		u8* const component_bytes = (u8*)component;

		std::vector<kbTypeInfoHierarchyIterator::iteratorType> fields;
		kbTypeInfoHierarchyIterator iterator(component);
		for (auto it = iterator.Begin(); iterator.IsDone() == false; it = iterator.GetNextTypeInfoField()) {
			fields.push_back(it);
		}
		std::sort(fields.begin(), fields.end(), [](const kbTypeInfoHierarchyIterator::iteratorType& a, const kbTypeInfoHierarchyIterator::iteratorType& b) {
			return a->second.Offset() < b->second.Offset();
		});

		for (const auto& field : fields) {
			const std::string& field_name = field->first;
			const kbTypeInfoVar& var = field->second;
			u8* const byte_offset_to_var = component_bytes + var.Offset();

			ImGui::PushID(field_name.c_str());
			if (var.IsArray()) {
				ImGui::TextDisabled("%s (array, not yet editable here)", field_name.c_str());
			} else if (var.Type() == KBTYPEINFO_STRUCT) {
				ImGui::TextDisabled("%s (struct, not yet editable here)", field_name.c_str());
			} else {
				DrawField(field_name, var.Type(), var.GetStructName(), component, byte_offset_to_var);
			}
			ImGui::PopID();
		}
	}
	ImGui::PopID();
}

/// PropertiesPanel::DrawField
void PropertiesPanel::DrawField(const std::string& field_name, const kbTypeInfoType_t field_type, const std::string& struct_name,
	kbGameComponent* const component, u8* const byte_offset_to_var) {

	ImGui::TextUnformatted(field_name.c_str());
	ImGui::SameLine(140.0f);
	ImGui::SetNextItemWidth(-FLT_MIN);

	switch (field_type) {
	case KBTYPEINFO_BOOL: {
		ImGui::Checkbox("##value", (bool*)byte_offset_to_var);
		if (ImGui::IsItemActivated()) {
			m_PendingEdit = PendingEdit_t{ component, field_type, field_name, byte_offset_to_var };
		}
		if (ImGui::IsItemDeactivatedAfterEdit()) {
			CommitPendingEdit();
		}
		break;
	}
	case KBTYPEINFO_INT: {
		ImGui::InputInt("##value", (int*)byte_offset_to_var);
		if (ImGui::IsItemActivated()) {
			m_PendingEdit = PendingEdit_t{ component, field_type, field_name, byte_offset_to_var };
			m_PendingEdit.int_snapshot = *(int*)byte_offset_to_var;
		}
		if (ImGui::IsItemDeactivatedAfterEdit()) {
			CommitPendingEdit();
		}
		break;
	}
	case KBTYPEINFO_FLOAT: {
		ImGui::InputFloat("##value", (float*)byte_offset_to_var);
		if (ImGui::IsItemActivated()) {
			m_PendingEdit = PendingEdit_t{ component, field_type, field_name, byte_offset_to_var };
			m_PendingEdit.float_snapshot = *(float*)byte_offset_to_var;
		}
		if (ImGui::IsItemDeactivatedAfterEdit()) {
			CommitPendingEdit();
		}
		break;
	}
	case KBTYPEINFO_VECTOR:
	case KBTYPEINFO_VECTOR4: {
		const int num_components = (field_type == KBTYPEINFO_VECTOR) ? 3 : 4;
		float* const components = (float*)byte_offset_to_var;
		const float item_width = ImGui::GetContentRegionAvail().x / num_components;
		for (int i = 0; i < num_components; i++) {
			ImGui::PushID(i);
			ImGui::SetNextItemWidth(item_width);
			ImGui::InputFloat("##value", &components[i]);
			if (ImGui::IsItemActivated()) {
				m_PendingEdit = PendingEdit_t{ component, field_type, field_name, &components[i] };
				m_PendingEdit.float_snapshot = components[i];
			}
			if (ImGui::IsItemDeactivatedAfterEdit()) {
				CommitPendingEdit();
			}
			if (i + 1 < num_components) {
				ImGui::SameLine();
			}
			ImGui::PopID();
		}
		break;
	}
	case KBTYPEINFO_KBSTRING: {
		kbString& kb_string = *(kbString*)byte_offset_to_var;
		char buf[256];
		strncpy_s(buf, sizeof(buf), kb_string.c_str(), _TRUNCATE);
		ImGui::InputText("##value", buf, sizeof(buf));
		if (ImGui::IsItemActivated()) {
			m_PendingEdit = PendingEdit_t{ component, field_type, field_name, byte_offset_to_var };
			m_PendingEdit.string_snapshot = kb_string.stl_str();
		}
		if (ImGui::IsItemDeactivatedAfterEdit()) {
			kb_string = buf;
			CommitPendingEdit();
		}
		break;
	}
	case KBTYPEINFO_ENUM: {
		const std::vector<std::string>* const enum_labels = g_NameToTypeInfoMap->GetEnum(struct_name);
		const int value = *(int*)byte_offset_to_var;
		const char* const preview = (value >= 0 && value < (int)enum_labels->size()) ? (*enum_labels)[value].c_str() : "";
		if (ImGui::BeginCombo("##value", preview)) {
			for (int i = 0; i < (int)enum_labels->size(); i++) {
				const bool is_selected = (i == value);
				if (ImGui::Selectable((*enum_labels)[i].c_str(), is_selected)) {
					*(int*)byte_offset_to_var = i;
					m_PendingEdit = PendingEdit_t{ component, field_type, field_name, byte_offset_to_var };
					CommitPendingEdit();
				}
			}
			ImGui::EndCombo();
		}
		break;
	}
	case KBTYPEINFO_GAMEENTITY:
		DrawGameEntityField(field_name, component, byte_offset_to_var);
		break;
	case KBTYPEINFO_PTR:
	case KBTYPEINFO_TEXTURE:
	case KBTYPEINFO_STATICMODEL:
	case KBTYPEINFO_SOUNDWAVE:
	case KBTYPEINFO_SHADER:
	case KBTYPEINFO_ANIMATION:
		DrawResourceField(field_name, field_type, component, byte_offset_to_var);
		break;
	default:
		break;
	}
}

/// PropertiesPanel::DrawGameEntityField
///
/// Mirrors kbPropertiesTab::PointerButtonCB's KBTYPEINFO_GAMEENTITY branch
/// exactly (kbPropertiesTab.cpp:194-220) -- including the fact that it does
/// NOT broadcast WidgetCB_EntityModified (that call sits after an early
/// `return` for this field type in the FLTK version) and offers no working
/// clear button: RefreshProperty's GAMEENTITY case never actually wires one
/// up in the FLTK panel today (ClearPointerButtonCB's GAMEENTITY branch
/// exists but is unreachable dead code), so this panel doesn't invent one
/// either.
void PropertiesPanel::DrawGameEntityField(const std::string& field_name, kbGameComponent* const component, u8* const byte_offset_to_var) {
	GameEntityPtr* const entity_ptr = (GameEntityPtr*)byte_offset_to_var;
	const GameEntity* const current = entity_ptr->GetEntity();
	ImGui::TextDisabled("%s", (current != nullptr) ? current->name().c_str() : "(none)");
	ImGui::SameLine();
	if (ImGui::SmallButton("Pick")) {
		const kbPrefab* const prefab = g_Editor->GetCurrentlySelectedPrefab();
		GameEntity* const picked = g_pResourceTab->GetSelectedGameEntity().GetEntity();
		if (picked != nullptr || prefab == nullptr) {
			entity_ptr->SetEntity(picked);
		} else {
			entity_ptr->SetEntity(const_cast<GameEntity*>(prefab->GetGameEntity(0)));
		}
		component->editor_change(field_name);
	}
}

/// PropertiesPanel::DrawResourceField
///
/// Mirrors kbPropertiesTab::PointerButtonCB/ClearPointerButtonCB's resource
/// branches exactly (kbPropertiesTab.cpp:222-243, 267-277) -- same
/// type-check-before-assign guard, same editor_change/WidgetCB_EntityModified
/// broadcast, no undo push (matching current behavior).
void PropertiesPanel::DrawResourceField(const std::string& field_name, const kbTypeInfoType_t field_type, kbGameComponent* const component, u8* const byte_offset_to_var) {
	Resource** const resource_slot = (Resource**)byte_offset_to_var;
	ImGui::TextDisabled("%s", (*resource_slot != nullptr) ? (*resource_slot)->name().c_str() : "(none)");
	ImGui::SameLine();
	if (ImGui::SmallButton("Pick") && m_CurrentlySelectedResourceFileName.empty() == false) {
		Resource* const candidate = g_ResourceManager.resource(m_CurrentlySelectedResourceFileName.c_str(), true, true);
		if (candidate != nullptr && candidate->type() == field_type && candidate != *resource_slot) {
			*resource_slot = candidate;
			component->editor_change(field_name);
			g_Editor->BroadcastEvent(widgetCBGeneric(WidgetCB_EntityModified, nullptr));
		}
	}
	ImGui::SameLine();
	if (ImGui::SmallButton("Clear")) {
		*resource_slot = nullptr;
		component->editor_change(field_name);
		g_Editor->BroadcastEvent(widgetCBGeneric(WidgetCB_EntityModified, nullptr));
	}
}

/// PropertiesPanel::CommitPendingEdit
void PropertiesPanel::CommitPendingEdit() {
	const PendingEdit_t edit = m_PendingEdit;
	kbGameComponent* const component = edit.component;
	void* const byte_offset_to_var = edit.byte_offset_to_var;

	switch (edit.type) {
	case KBTYPEINFO_FLOAT:
	case KBTYPEINFO_VECTOR:
	case KBTYPEINFO_VECTOR4: {
		float* const prev = new float(edit.float_snapshot);
		float* const cur = new float(*(float*)byte_offset_to_var);
		g_Editor->PushUndoAction(new kbUndoVariableAction(edit.type, prev, cur, byte_offset_to_var));
		delete prev;
		delete cur;
		break;
	}
	case KBTYPEINFO_INT: {
		// Matches TextFieldCB exactly, including the pre-existing no-op
		// Undo/RedoAction() for INT (kbUndoAction.cpp) -- kept as-is per
		// "match current behavior", not silently fixed here.
		int* const prev = new int(edit.int_snapshot);
		int* const cur = new int(*(int*)byte_offset_to_var);
		g_Editor->PushUndoAction(new kbUndoVariableAction(edit.type, prev, cur, byte_offset_to_var));
		delete prev;
		delete cur;
		break;
	}
	case KBTYPEINFO_KBSTRING: {
		kbString* const prev = new kbString(edit.string_snapshot.c_str());
		kbString* const cur = new kbString(((kbString*)byte_offset_to_var)->c_str());
		g_Editor->PushUndoAction(new kbUndoVariableAction(edit.type, prev, cur, byte_offset_to_var));
		delete prev;
		delete cur;
		break;
	}
	default:
		break;
	}

	if (edit.type == KBTYPEINFO_FLOAT || edit.type == KBTYPEINFO_VECTOR || edit.type == KBTYPEINFO_VECTOR4 ||
		edit.type == KBTYPEINFO_INT || edit.type == KBTYPEINFO_KBSTRING) {
		// TextFieldCB's transform-component-0 quirk: re-enable every OTHER
		// enabled component so they re-pick-up the new transform.
		GameEntity* const game_entity = component->GetOwner();
		if (game_entity != nullptr && game_entity->component(0) == component) {
			for (size_t i = 0; i < game_entity->num_components(); i++) {
				kbGameComponent* const other = game_entity->component(i);
				if (other->IsEnabled()) {
					other->Enable(false);
					other->Enable(true);
				}
			}
		}
		component->editor_change(edit.field_name);
	} else if (edit.type == KBTYPEINFO_ENUM) {
		// EnumCB's own distinct quirk: cycle Enable on the edited component
		// itself, not on the entity's other components.
		component->Enable(false);
		component->editor_change(edit.field_name);
		component->Enable(true);
	} else {
		// BOOL (CheckButtonCB): its transform-cycle logic is dead/commented
		// out in the FLTK source today -- just editor_change, matching that.
		component->editor_change(edit.field_name);
	}

	g_Editor->BroadcastEvent(widgetCBGeneric(WidgetCB_EntityModified, nullptr));
}
