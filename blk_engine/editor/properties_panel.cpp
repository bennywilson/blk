/// properties_panel.cpp
///
/// 2026 blk

#include <algorithm>
#include "blk_core.h"
#include "properties_panel.h"
#include "editor.h"
#include "editor_entity.h"
#include "editor_platform.h"
#include "resources_panel.h"
#include "imgui.h"

/// PropertiesPanel::PropertiesPanel
PropertiesPanel::PropertiesPanel() {
	g_Editor->RegisterEvent(this, WidgetCB_ResourceSelected);
	g_Editor->RegisterEvent(this, WidgetCB_PrefabSelected);
	g_Editor->RegisterEvent(this, WidgetCB_EntitySelected);
	g_Editor->RegisterEvent(this, WidgetCB_EntityDeselected);
}

/// PropertiesPanel::~PropertiesPanel
PropertiesPanel::~PropertiesPanel() {
	ClearTempPrefabEntity();
}

/// PropertiesPanel::EventCB
void PropertiesPanel::EventCB(const widgetCBObject* const widget_cb_object) {
	switch (widget_cb_object->widgetType) {
		case WidgetCB_ResourceSelected:
			m_CurrentlySelectedResourceFileName = static_cast<const widgetCBResourceSelected*>(widget_cb_object)->resourceFileName;
			break;

		// Selecting a scene entity ends prefab editing.
		case WidgetCB_EntitySelected:
		case WidgetCB_EntityDeselected:
			ClearTempPrefabEntity();
			break;

		case WidgetCB_PrefabSelected: {
			ClearTempPrefabEntity();

			// Null-checks the prefab, since a Resources tree entry can outlive it.
			const Prefab* const prefab = g_Editor->GetCurrentlySelectedPrefab();
			if (prefab && prefab->GetGameEntity(0)) {
				m_pTempPrefabEntity = new EditorEntity(const_cast<GameEntity*>(prefab->GetGameEntity(0)));
			}
			break;
		}

		default:
			break;
	}
}

/// PropertiesPanel::ClearTempPrefabEntity
void PropertiesPanel::ClearTempPrefabEntity() {
	if (!m_pTempPrefabEntity) {
		return;
	}

	// Detaches first because the GameEntity belongs to the prefab in the ResourceManager.
	m_pTempPrefabEntity->SetGameEntity(nullptr);
	delete m_pTempPrefabEntity;
	m_pTempPrefabEntity = nullptr;
}

/// PropertiesPanel::draw_imgui
void PropertiesPanel::draw_imgui() {
	ImGui::SetNextWindowPos(ImVec2(300, 20), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSize(ImVec2(340, 480), ImGuiCond_FirstUseEver);
	ImGui::Begin("Properties");

	const std::vector<EditorEntity*>& selected = g_Editor->GetSelectedObjects();

	// Edits the prefab over the live selection, since selecting a prefab doesn't clear g_Editor's list.
	EditorEntity* edit_target = m_pTempPrefabEntity;

	if (!edit_target) {
		// Trusts the selection only while it's in the live entity list, since GetGameEntity() can dangle mid level-reload.
		const std::vector<EditorEntity*>& live_entities = g_Editor->GetGameEntities();
		const bool selection_is_live = (selected.size() == 1) &&
			(std::find(live_entities.begin(), live_entities.end(), selected[0]) != live_entities.end());
		if (selection_is_live) {
			edit_target = selected[0];
		}
	}

	GameEntity* const game_entity = edit_target ? edit_target->GetGameEntity() : nullptr;
	if (game_entity) {
		for (size_t i = 0; i < game_entity->num_components(); i++) {
			ImGui::PushID((int)i);
			DrawComponent(edit_target, game_entity->component(i), nullptr, false);
			ImGui::PopID();
		}
	} else if (selected.size() > 1) {
		ImGui::TextDisabled("Multiple entities selected.");
	} else {
		ImGui::TextDisabled("No entity selected.");
	}

	ImGui::End();

	ApplyPendingStructuralChanges();
}

/// PropertiesPanel::ApplyPendingStructuralChanges
///
/// Runs after the frame's widgets, never during them. See PendingArrayOp_t.
void PropertiesPanel::ApplyPendingStructuralChanges() {
	if (m_PendingArrayOp.op != PendingArrayOp_t::Op_None) {
		const PendingArrayOp_t op = m_PendingArrayOp;
		m_PendingArrayOp = PendingArrayOp_t();

		switch (op.op) {
			case PendingArrayOp_t::Op_Resize:
				switch (op.element_type) {
					case BLK_TYPEINFO_SHADER:
						((std::vector<Shader*>*)op.array_ptr)->resize(op.new_size);
						break;
					case BLK_TYPEINFO_TEXTURE:
						((std::vector<Texture*>*)op.array_ptr)->resize(op.new_size);
						break;
					default:
						g_NameToTypeInfoMap->ResizeVector(op.array_ptr, op.struct_name, op.new_size);
						break;
				}
				break;

			case PendingArrayOp_t::Op_Insert:
				g_NameToTypeInfoMap->InsertVectorElement(op.array_ptr, op.struct_name, op.index);
				break;

			case PendingArrayOp_t::Op_Remove:
				g_NameToTypeInfoMap->RemoveVectorElement(op.array_ptr, op.struct_name, op.index);
				break;

			default:
				break;
		}

		// Cycles Enable so the owning component re-reads an array whose storage moved.
		// op.component is always the array's owner, since an element pointer would already dangle here.
		if (op.component->IsEnabled()) {
			op.component->Enable(false);
			op.component->Enable(true);
		}

		NotifyEditorChange(op.component, op.parent_component, op.field_name);
		BroadcastPropertyChanged();
	}

	if (m_pComponentToDelete) {
		Component* const component = m_pComponentToDelete;
		EditorEntity* const owner = m_pComponentToDeleteOwner;
		m_pComponentToDelete = nullptr;
		m_pComponentToDeleteOwner = nullptr;

		std::string message = "Delete ";
		message += component->GetComponentClassName();
		message += "?";

		editor_platform::confirm("Delete Component", message, [component, owner]() {
			// The answer may arrive frames after the click, so make sure the entity
			// and the component are still there before touching either.
			const std::vector<EditorEntity*>& entities = g_Editor->GetGameEntities();
			if (std::find(entities.begin(), entities.end(), owner) == entities.end()) {
				return;
			}

			GameEntity* const game_entity = owner->GetGameEntity();

			int component_index = -1;
			for (size_t i = 0; i < game_entity->num_components(); i++) {
				if (game_entity->component(i) == component) {
					component_index = (int)i;
					break;
				}
			}
			if (component_index < 0) {
				return;
			}

			game_entity->remove_component(component);
			g_Editor->PushUndoAction(new UndoDeleteComponent(owner, component, component_index));
		});
	}
}

/// PropertiesPanel::DrawComponent
void PropertiesPanel::DrawComponent(EditorEntity* const editor_entity, Component* const component, Component* const parent_component,
	const bool is_struct) {

	ImGui::PushID(component);

	const char* const class_name = component->GetComponentClassName();

	// AllowOverlap lets the "X" drawn over the full-width CollapsingHeader take its own clicks.
	// TreeNodeEx needs no flag, since structs draw no "X".
	const bool open = is_struct ? ImGui::TreeNodeEx(class_name, ImGuiTreeNodeFlags_DefaultOpen) : ImGui::CollapsingHeader(class_name, ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);

	// Transform components can't be removed. Insert/remove for a struct in an array belongs to DrawArrayField().
	if (!is_struct && !component->IsA(TransformComponent::GetType())) {
		ImGui::SameLine();
		if (ImGui::SmallButton("X")) {
			// Confirms and applies after the frame. See PendingArrayOp_t.
			m_pComponentToDelete = component;
			m_pComponentToDeleteOwner = editor_entity;
		}
	}

	if (open) {
		u8* const component_bytes = (u8*)component;

		std::vector<TypeInfoHierarchyIterator::iteratorType> fields;
		TypeInfoHierarchyIterator iterator(component);
		for (auto it = iterator.Begin(); !iterator.IsDone(); it = iterator.GetNextTypeInfoField()) {
			fields.push_back(it);
		}
		std::sort(fields.begin(), fields.end(), [](const TypeInfoHierarchyIterator::iteratorType& a, const TypeInfoHierarchyIterator::iteratorType& b) {
			return a->second.Offset() < b->second.Offset();
		});

		for (const auto& field : fields) {
			const std::string& field_name = field->first;
			const TypeInfoVar& var = field->second;

			// Hides a struct's "IsEnabled" field, an artifact of structs being Components.
			if (is_struct && field_name == "IsEnabled") {
				continue;
			}

			u8* const byte_offset_to_var = component_bytes + var.Offset();

			ImGui::PushID(field_name.c_str());
			if (var.IsArray()) {
				DrawArrayField(editor_entity, field_name, var.Type(), var.GetStructName(), component, parent_component, byte_offset_to_var);
			} else if (var.Type() == BLK_TYPEINFO_STRUCT) {
				DrawComponent(editor_entity, (Component*)byte_offset_to_var, component, true);
			} else {
				DrawField(field_name, var.Type(), var.GetStructName(), component, parent_component, byte_offset_to_var, &var);
			}
			ImGui::PopID();
		}

		if (is_struct) {
			ImGui::TreePop();
		}
	}

	ImGui::PopID();
}

/// PropertiesPanel::DrawArrayField
///
/// Reads SHADER and TEXTURE arrays as plain std::vector<T*>, everything else through the reflection map's type-erased helpers.
/// Resizes only on Enter, since a half-typed "1" on the way to "12" would truncate. TODO: Structural edits push no undo action.
void PropertiesPanel::DrawArrayField(EditorEntity* const editor_entity, const std::string& field_name, const TypeInfoType_t element_type,
	const std::string& struct_name, Component* const component, Component* const parent_component, u8* const byte_offset_to_var) {

	size_t element_count = 0;
	switch (element_type) {
		case BLK_TYPEINFO_SHADER:
			element_count = ((const std::vector<Shader*>*)byte_offset_to_var)->size();
			break;
		case BLK_TYPEINFO_TEXTURE:
			element_count = ((const std::vector<Texture*>*)byte_offset_to_var)->size();
			break;
		default:
			element_count = g_NameToTypeInfoMap->GetVectorSize(byte_offset_to_var, struct_name);
			break;
	}

	// Offers only whole-vector resize for SHADER and TEXTURE, since the reflection map doesn't register them by struct name.
	const bool supports_element_ops = (element_type != BLK_TYPEINFO_SHADER && element_type != BLK_TYPEINFO_TEXTURE);

	const bool open = ImGui::TreeNode(field_name.c_str(), "%s", field_name.c_str());

	ImGui::SameLine();
	ImGui::SetNextItemWidth(80.0f);
	int requested_size = (int)element_count;
	if (ImGui::InputInt("##size", &requested_size, 0, 0, ImGuiInputTextFlags_EnterReturnsTrue)) {
		if (requested_size < 0 || requested_size > 128) {
			blk::warn("Array value is not between 0 and 128");
		} else if (requested_size != (int)element_count) {
			m_PendingArrayOp = PendingArrayOp_t{ PendingArrayOp_t::Op_Resize, byte_offset_to_var, struct_name,
				element_type, 0, requested_size, component, parent_component, field_name };
		}
	}

	// Appends, since an empty array otherwise has only the Enter-to-commit size box.
	if (supports_element_ops) {
		ImGui::SameLine();
		if (ImGui::SmallButton("+")) {
			m_PendingArrayOp = PendingArrayOp_t{ PendingArrayOp_t::Op_Insert, byte_offset_to_var, struct_name,
				element_type, (int)element_count, 0, component, parent_component, field_name };
		}
	}

	if (open) {
		for (size_t i = 0; i < element_count; i++) {
			ImGui::PushID((int)i);

			// Records the array's owning component, never the element, since reallocation would leave an element pointer dangling.
			if (supports_element_ops) {
				if (ImGui::SmallButton("+")) {
					m_PendingArrayOp = PendingArrayOp_t{ PendingArrayOp_t::Op_Insert, byte_offset_to_var, struct_name,
						element_type, (int)i, 0, component, parent_component, field_name };
				}
				ImGui::SameLine();
				if (ImGui::SmallButton("-")) {
					m_PendingArrayOp = PendingArrayOp_t{ PendingArrayOp_t::Op_Remove, byte_offset_to_var, struct_name,
						element_type, (int)i, 0, component, parent_component, field_name };
				}
				ImGui::SameLine();
			}

			switch (element_type) {
				case BLK_TYPEINFO_SHADER: {
					const std::vector<Shader*>* const shaders = (const std::vector<Shader*>*)byte_offset_to_var;
					DrawResourceField(field_name, element_type, component, parent_component, (u8*)&(*shaders)[i]);
					break;
				}
				case BLK_TYPEINFO_TEXTURE: {
					const std::vector<Texture*>* const textures = (const std::vector<Texture*>*)byte_offset_to_var;
					DrawResourceField(field_name, element_type, component, parent_component, (u8*)&(*textures)[i]);
					break;
				}
				default: {
					u8* const element_bytes = (u8*)g_NameToTypeInfoMap->GetVectorElement(byte_offset_to_var, struct_name, i);
					if (!element_bytes) {
						break;
					}
					if (element_type == BLK_TYPEINFO_STRUCT) {
						DrawComponent(editor_entity, (Component*)element_bytes, component, true);
					} else {
						char index_label[16];
						sprintf_s(index_label, "[%d]", (int)i);
						DrawField(index_label, element_type, struct_name, component, parent_component, element_bytes);
					}
					break;
				}
			}

			ImGui::PopID();
		}

		ImGui::TreePop();
	}
}

// Label-to-widget gap, sized for the longest reflected field names ("CullModeOverride", "nonToonDiffuseScaleAndBias").
static const float k_field_label_width = 165.0f;

/// PropertiesPanel::DrawField
/// Clamps a committed value to the property's MinVal/MaxVal. Runs before the undo entry is
/// pushed, so redo replays the clamped value rather than the one that was typed. Clamping on
/// commit instead of per keystroke keeps a partly typed number ("50" on the way to "500")
/// from being yanked mid-edit.
static void clamp_to_range(const TypeInfoVar* const range, float& value) {
	if (range == nullptr) {
		return;
	}
	if (range->HasMin() && value < range->Min()) {
		value = range->Min();
	}
	if (range->HasMax() && value > range->Max()) {
		value = range->Max();
	}
}

static void clamp_to_range(const TypeInfoVar* const range, int& value) {
	// Clamped in the int domain, and only written when a bound actually bites: routing
	// every commit through float would quietly round values past 2^24.
	if (range == nullptr) {
		return;
	}
	if (range->HasMin() && (float)value < range->Min()) {
		value = (int)range->Min();
	}
	if (range->HasMax() && (float)value > range->Max()) {
		value = (int)range->Max();
	}
}

void PropertiesPanel::DrawField(const std::string& field_name, const TypeInfoType_t field_type, const std::string& struct_name,
	Component* const component, Component* const parent_component, u8* const byte_offset_to_var,
	const TypeInfoVar* const range) {

	// Offsets the widget from this label's start, since SameLine(offset) ignores DC.Indent.x and a fixed column slides under nested labels.
	// max() pushes an over-long name's widget right. Breaks inside BeginGroup() or Columns, whose offsets would count twice.
	const float label_start_x = ImGui::GetCursorPosX();
	const float label_width = ImGui::CalcTextSize(field_name.c_str()).x + ImGui::GetStyle().ItemSpacing.x;

	ImGui::TextUnformatted(field_name.c_str());
	ImGui::SameLine(label_start_x + (label_width > k_field_label_width ? label_width : k_field_label_width));
	ImGui::SetNextItemWidth(-FLT_MIN);

	switch (field_type) {
		case BLK_TYPEINFO_BOOL: {
			ImGui::Checkbox("##value", (bool*)byte_offset_to_var);
			if (ImGui::IsItemActivated()) {
				m_PendingEdit = PendingEdit_t{ component, parent_component, field_type, field_name, byte_offset_to_var };
			}
			if (ImGui::IsItemDeactivatedAfterEdit()) {
				CommitPendingEdit();
			}
			break;
		}
		case BLK_TYPEINFO_INT: {
			ImGui::InputInt("##value", (int*)byte_offset_to_var);
			if (ImGui::IsItemActivated()) {
				m_PendingEdit = PendingEdit_t{ component, parent_component, field_type, field_name, byte_offset_to_var };
				m_PendingEdit.int_snapshot = *(int*)byte_offset_to_var;
			}
			if (ImGui::IsItemDeactivatedAfterEdit()) {
				clamp_to_range(range, *(int*)byte_offset_to_var);
				CommitPendingEdit();
			}
			break;
		}
		case BLK_TYPEINFO_FLOAT: {
			ImGui::InputFloat("##value", (float*)byte_offset_to_var);
			if (ImGui::IsItemActivated()) {
				m_PendingEdit = PendingEdit_t{ component, parent_component, field_type, field_name, byte_offset_to_var };
				m_PendingEdit.float_snapshot = *(float*)byte_offset_to_var;
			}
			if (ImGui::IsItemDeactivatedAfterEdit()) {
				clamp_to_range(range, *(float*)byte_offset_to_var);
				CommitPendingEdit();
			}
			break;
		}
		case BLK_TYPEINFO_VECTOR:
		case BLK_TYPEINFO_VECTOR4: {
			const int num_components = (field_type == BLK_TYPEINFO_VECTOR) ? 3 : 4;
			float* const components = (float*)byte_offset_to_var;
			const float item_width = ImGui::GetContentRegionAvail().x / num_components;
			for (int i = 0; i < num_components; i++) {
				ImGui::PushID(i);
				ImGui::SetNextItemWidth(item_width);
				ImGui::InputFloat("##value", &components[i]);
				if (ImGui::IsItemActivated()) {
					m_PendingEdit = PendingEdit_t{ component, parent_component, field_type, field_name, &components[i] };
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
		case BLK_TYPEINFO_STRING: {
			String& string_value = *(String*)byte_offset_to_var;
			char buf[256];
			strncpy_s(buf, sizeof(buf), string_value.c_str(), _TRUNCATE);
			ImGui::InputText("##value", buf, sizeof(buf));
			if (ImGui::IsItemActivated()) {
				m_PendingEdit = PendingEdit_t{ component, parent_component, field_type, field_name, byte_offset_to_var };
				m_PendingEdit.string_snapshot = string_value.stl_str();
			}
			if (ImGui::IsItemDeactivatedAfterEdit()) {
				string_value = buf;
				CommitPendingEdit();
			}
			break;
		}
		case BLK_TYPEINFO_ENUM: {
			const std::vector<std::string>* const enum_labels = g_NameToTypeInfoMap->GetEnum(struct_name);
			const int value = *(int*)byte_offset_to_var;
			const char* const preview = (value >= 0 && value < (int)enum_labels->size()) ? (*enum_labels)[value].c_str() : "";
			if (ImGui::BeginCombo("##value", preview)) {
				for (int i = 0; i < (int)enum_labels->size(); i++) {
					const bool is_selected = (i == value);
					if (ImGui::Selectable((*enum_labels)[i].c_str(), is_selected)) {
						*(int*)byte_offset_to_var = i;
						m_PendingEdit = PendingEdit_t{ component, parent_component, field_type, field_name, byte_offset_to_var };
						CommitPendingEdit();
					}
				}
				ImGui::EndCombo();
			}
			break;
		}
		case BLK_TYPEINFO_GAMEENTITY:
			DrawGameEntityField(field_name, component, parent_component, byte_offset_to_var);
			break;
		case BLK_TYPEINFO_PTR:
		case BLK_TYPEINFO_TEXTURE:
		case BLK_TYPEINFO_STATICMODEL:
		case BLK_TYPEINFO_SOUNDWAVE:
		case BLK_TYPEINFO_SHADER:
		case BLK_TYPEINFO_ANIMATION:
			DrawResourceField(field_name, field_type, component, parent_component, byte_offset_to_var);
			break;
		default:
			break;
	}
}

/// PropertiesPanel::DrawGameEntityField
///
/// TODO: Broadcasts no WidgetCB_EntityModified and has no Clear button, unlike DrawResourceField().
void PropertiesPanel::DrawGameEntityField(const std::string& field_name, Component* const component, Component* const parent_component,
	u8* const byte_offset_to_var) {

	GameEntityPtr* const entity_ptr = (GameEntityPtr*)byte_offset_to_var;
	const GameEntity* const current = entity_ptr->GetEntity();
	ImGui::TextDisabled("%s", current ? current->name().c_str() : "(none)");
	ImGui::SameLine();
	if (ImGui::SmallButton("Pick")) {
		const Prefab* const prefab = g_Editor->GetCurrentlySelectedPrefab();
		GameEntity* const picked = g_pResourcesPanel->GetSelectedGameEntity().GetEntity();
		if (picked || !prefab) {
			entity_ptr->SetEntity(picked);
		} else {
			entity_ptr->SetEntity(const_cast<GameEntity*>(prefab->GetGameEntity(0)));
		}
		NotifyEditorChange(component, parent_component, field_name);
	}
}

/// PropertiesPanel::DrawResourceField
///
/// Assigns only a resource whose type matches the field. TODO: Pushes no undo action.
void PropertiesPanel::DrawResourceField(const std::string& field_name, const TypeInfoType_t field_type, Component* const component,
	Component* const parent_component, u8* const byte_offset_to_var) {

	Resource** const resource_slot = (Resource**)byte_offset_to_var;
	ImGui::TextDisabled("%s", *resource_slot ? (*resource_slot)->name().c_str() : "(none)");
	ImGui::SameLine();
	if (ImGui::SmallButton("Pick") && !m_CurrentlySelectedResourceFileName.empty()) {
		Resource* const candidate = g_ResourceManager.resource(m_CurrentlySelectedResourceFileName.c_str(), true, true);
		if (candidate && candidate->type() == field_type && candidate != *resource_slot) {
			*resource_slot = candidate;
			NotifyEditorChange(component, parent_component, field_name);
			BroadcastPropertyChanged();
		}
	}
	ImGui::SameLine();
	if (ImGui::SmallButton("Clear")) {
		*resource_slot = nullptr;
		NotifyEditorChange(component, parent_component, field_name);
		BroadcastPropertyChanged();
	}
}

/// PropertiesPanel::NotifyEditorChange
void PropertiesPanel::NotifyEditorChange(Component* const component, Component* const parent_component, const std::string& field_name) {
	component->editor_change(field_name);
	if (parent_component) {
		parent_component->editor_change(field_name);
	}
}

/// PropertiesPanel::BroadcastPropertyChanged
void PropertiesPanel::BroadcastPropertyChanged() {
	if (m_pTempPrefabEntity) {
		g_Editor->BroadcastEvent(widgetCBGeneric(WidgetCB_PrefabModified, m_pTempPrefabEntity->GetGameEntity()));
	}

	g_Editor->BroadcastEvent(widgetCBGeneric(WidgetCB_EntityModified, nullptr));
}

/// PropertiesPanel::CommitPendingEdit
void PropertiesPanel::CommitPendingEdit() {
	const PendingEdit_t edit = m_PendingEdit;
	Component* const component = edit.component;
	void* const byte_offset_to_var = edit.byte_offset_to_var;

	switch (edit.type) {
		case BLK_TYPEINFO_FLOAT:
		case BLK_TYPEINFO_VECTOR:
		case BLK_TYPEINFO_VECTOR4: {
			float prev = edit.float_snapshot;
			float cur = *(float*)byte_offset_to_var;
			g_Editor->PushUndoAction(new UndoVariableAction(edit.type, &prev, &cur, byte_offset_to_var));
			break;
		}
		case BLK_TYPEINFO_INT: {
			int prev = edit.int_snapshot;
			int cur = *(int*)byte_offset_to_var;
			g_Editor->PushUndoAction(new UndoVariableAction(edit.type, &prev, &cur, byte_offset_to_var));
			break;
		}
		case BLK_TYPEINFO_STRING: {
			String prev(edit.string_snapshot.c_str());
			String cur(((String*)byte_offset_to_var)->c_str());
			g_Editor->PushUndoAction(new UndoVariableAction(edit.type, &prev, &cur, byte_offset_to_var));
			break;
		}
		default:
			break;
	}

	if (edit.type == BLK_TYPEINFO_FLOAT || edit.type == BLK_TYPEINFO_VECTOR || edit.type == BLK_TYPEINFO_VECTOR4 ||
		edit.type == BLK_TYPEINFO_INT || edit.type == BLK_TYPEINFO_STRING) {
		// Re-enables every enabled component when component 0 (the transform) is edited, so they pick up the new transform.
		// Structs aren't GameComponents and have no owner to walk.
		GameEntity* const game_entity = component->IsA(GameComponent::GetType()) ? (GameEntity*)component->GetOwner() : nullptr;
		if (game_entity && game_entity->component(0) == component) {
			for (size_t i = 0; i < game_entity->num_components(); i++) {
				Component* const other = game_entity->component(i);
				if (other->IsEnabled()) {
					other->Enable(false);
					other->Enable(true);
				}
			}
		}
		NotifyEditorChange(component, edit.parent_component, edit.field_name);
	} else if (edit.type == BLK_TYPEINFO_ENUM) {
		// Cycles Enable on the edited component itself.
		component->Enable(false);
		NotifyEditorChange(component, edit.parent_component, edit.field_name);
		component->Enable(true);
	} else {
		// BOOL edits only notify.
		NotifyEditorChange(component, edit.parent_component, edit.field_name);
	}

	BroadcastPropertyChanged();
}
