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
#include "resources_panel.h"
#include "kbUndoAction.h"
#include "resource_manager.h"
#include "imgui.h"

/// PropertiesPanel::PropertiesPanel
PropertiesPanel::PropertiesPanel(const int x, const int y, const int w, const int h) : EditorPanel(x, y, w, h) {
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

	case WidgetCB_EntitySelected:
	case WidgetCB_EntityDeselected:
		// Selecting a scene entity ends prefab editing, matching
		// kbPropertiesTab's EntitySelected/EntityDeselected handlers.
		ClearTempPrefabEntity();
		break;

	case WidgetCB_PrefabSelected: {
		ClearTempPrefabEntity();

		// kbPropertiesTab dereferenced GetCurrentlySelectedPrefab() here with
		// no null check; guarded, since a prefab selection can now come from
		// a panel whose tree entry outlived the prefab.
		const kbPrefab* const prefab = g_Editor->GetCurrentlySelectedPrefab();
		if (prefab != nullptr && prefab->GetGameEntity(0) != nullptr) {
			m_pTempPrefabEntity = new kbEditorEntity(const_cast<GameEntity*>(prefab->GetGameEntity(0)));
		}
		break;
	}

	default:
		break;
	}
}

/// PropertiesPanel::ClearTempPrefabEntity
void PropertiesPanel::ClearTempPrefabEntity() {
	if (m_pTempPrefabEntity == nullptr) {
		return;
	}

	// Detach before deleting: the GameEntity belongs to the prefab in the
	// ResourceManager, not to this wrapper.
	m_pTempPrefabEntity->SetGameEntity(nullptr);
	delete m_pTempPrefabEntity;
	m_pTempPrefabEntity = nullptr;
}

/// PropertiesPanel::draw_imgui
void PropertiesPanel::draw_imgui() {
	ImGui::SetNextWindowPos(ImVec2(300, 20), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSize(ImVec2(340, 480), ImGuiCond_FirstUseEver);
	ImGui::Begin("Properties");

	const std::vector<kbEditorEntity*>& selected = g_Editor->GetSelectedObjects();

	// A prefab being edited takes precedence over the live selection, matching
	// kbPropertiesTab -- which cleared its own selection copy on
	// WidgetCB_PrefabSelected. This panel reads g_Editor's list live instead,
	// and that list isn't cleared by a prefab selection, so the precedence has
	// to be explicit here.
	kbEditorEntity* edit_target = m_pTempPrefabEntity;

	if (edit_target == nullptr) {
		// Guard against a selected kbEditorEntity whose underlying GameEntity has
		// already been destroyed (e.g. mid level-unload/reload) -- GetGameEntity()
		// can return a dangling pointer in that window, not nullptr, so a plain
		// null check isn't enough. Only trust the selection if it's still present
		// in the live entity list, the same source of truth OutlinerPanel iterates.
		const std::vector<kbEditorEntity*>& live_entities = g_Editor->GetGameEntities();
		const bool selection_is_live = (selected.size() == 1) &&
			(std::find(live_entities.begin(), live_entities.end(), selected[0]) != live_entities.end());
		if (selection_is_live) {
			edit_target = selected[0];
		}
	}

	GameEntity* const game_entity = (edit_target != nullptr) ? edit_target->GetGameEntity() : nullptr;
	if (game_entity != nullptr) {
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
/// Runs after the frame's widgets, never during them -- see PendingArrayOp_t.
void PropertiesPanel::ApplyPendingStructuralChanges() {

	if (m_PendingArrayOp.op != PendingArrayOp_t::Op_None) {
		const PendingArrayOp_t op = m_PendingArrayOp;
		m_PendingArrayOp = PendingArrayOp_t();

		switch (op.op) {
		case PendingArrayOp_t::Op_Resize:
			switch (op.element_type) {
			case KBTYPEINFO_SHADER:
				((std::vector<kbShader*>*)op.array_ptr)->resize(op.new_size);
				break;
			case KBTYPEINFO_TEXTURE:
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

		// ArrayResizeCB's enable-cycle: the component has to re-read an array
		// whose storage just moved. Insert/remove got no such cycle in the FLTK
		// version (they only rebuilt the widget tree); giving all three the same
		// treatment, since the reallocation hazard is identical.
		//
		// op.component is always the component that OWNS the array, never an
		// element inside it -- an element pointer would already be dangling
		// here, which is exactly what crashed on the first pass at this.
		if (op.component->IsEnabled()) {
			op.component->Enable(false);
			op.component->Enable(true);
		}

		NotifyEditorChange(op.component, op.parent_component, op.field_name);
		BroadcastPropertyChanged();
	}

	if (m_pComponentToDelete != nullptr) {
		kbComponent* const component = m_pComponentToDelete;
		kbEditorEntity* const owner = m_pComponentToDeleteOwner;
		m_pComponentToDelete = nullptr;
		m_pComponentToDeleteOwner = nullptr;

		std::string message = "Delete ";
		message += component->GetComponentClassName();
		message += "?";

		if (MessageBoxA(nullptr, message.c_str(), "Delete Component", MB_YESNO | MB_ICONQUESTION) == IDYES) {
			GameEntity* const game_entity = owner->GetGameEntity();

			int component_index = -1;
			for (size_t i = 0; i < game_entity->num_components(); i++) {
				if (game_entity->component(i) == component) {
					component_index = (int)i;
					break;
				}
			}

			game_entity->remove_component(component);
			g_Editor->PushUndoAction(new kbUndoDeleteComponent(owner, component, component_index));
		}
	}
}

/// PropertiesPanel::DrawComponent
void PropertiesPanel::DrawComponent(kbEditorEntity* const editor_entity, kbComponent* const component, kbComponent* const parent_component,
	const bool is_struct) {

	ImGui::PushID(component);

	const char* const class_name = component->GetComponentClassName();

	// ImGuiTreeNodeFlags_AllowOverlap: CollapsingHeader spans the full content
	// width, so the "X" drawn over it below lands inside the header's own hit
	// box. Without this the header claims the click and the component merely
	// collapses -- m_pComponentToDelete is never set, and component delete is
	// silently unreachable. TreeNodeEx doesn't need it: a tree node's hit box
	// is just its arrow plus label, and structs draw no "X" anyway.
	const bool open = is_struct ?
		ImGui::TreeNodeEx(class_name, ImGuiTreeNodeFlags_DefaultOpen) :
		ImGui::CollapsingHeader(class_name, ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);

	// Transform components can't be removed -- same exemption the FLTK panel
	// made before drawing its "X" button. Insert/remove for a struct that
	// lives in an array belongs to the array, not to the struct: see
	// DrawArrayField.
	if (is_struct == false && component->IsA(TransformComponent::GetType()) == false) {
		ImGui::SameLine();
		if (ImGui::SmallButton("X")) {
			// Confirmed and applied after the frame -- see PendingArrayOp_t.
			m_pComponentToDelete = component;
			m_pComponentToDeleteOwner = editor_entity;
		}
	}

	if (open) {
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

			// A struct's own "Enabled" field is an artifact of structs being
			// kbComponents; the FLTK panel hid it too.
			if (is_struct && field_name == "Enabled") {
				continue;
			}

			u8* const byte_offset_to_var = component_bytes + var.Offset();

			ImGui::PushID(field_name.c_str());
			if (var.IsArray()) {
				DrawArrayField(editor_entity, field_name, var.Type(), var.GetStructName(), component, parent_component, byte_offset_to_var);
			} else if (var.Type() == KBTYPEINFO_STRUCT) {
				DrawComponent(editor_entity, (kbComponent*)byte_offset_to_var, component, true);
			} else {
				DrawField(field_name, var.Type(), var.GetStructName(), component, parent_component, byte_offset_to_var);
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
/// KBTYPEINFO_SHADER and KBTYPEINFO_TEXTURE arrays are plain std::vector<T*>
/// and read directly; everything else goes through the reflection map's
/// type-erased vector helpers, keyed by struct name. Resizing only fires on
/// Enter, not per keystroke -- it destroys elements, so a half-typed "1" on
/// the way to "12" must not truncate the array. Matching the FLTK panel, none
/// of the three structural operations pushes an undo action.
void PropertiesPanel::DrawArrayField(kbEditorEntity* const editor_entity, const std::string& field_name, const kbTypeInfoType_t element_type,
	const std::string& struct_name, kbComponent* const component, kbComponent* const parent_component, u8* const byte_offset_to_var) {

	size_t element_count = 0;
	switch (element_type) {
	case KBTYPEINFO_SHADER:
		element_count = ((const std::vector<kbShader*>*)byte_offset_to_var)->size();
		break;
	case KBTYPEINFO_TEXTURE:
		element_count = ((const std::vector<Texture*>*)byte_offset_to_var)->size();
		break;
	default:
		element_count = g_NameToTypeInfoMap->GetVectorSize(byte_offset_to_var, struct_name);
		break;
	}

	// The SHADER/TEXTURE vectors aren't registered with the reflection map
	// under a struct name, so only the whole-vector resize works on them --
	// exactly the FLTK panel's split. Everything else gets per-element
	// insert/remove as well.
	const bool supports_element_ops = (element_type != KBTYPEINFO_SHADER && element_type != KBTYPEINFO_TEXTURE);

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

	// Append. Without this an empty array has no way to grow but the size box,
	// which needs Enter to commit and reads as inert until you find that out.
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

			// Insert-before / remove for this element. These record the
			// component that OWNS the array, never the element itself: the
			// vector reallocates, so an element pointer captured here would
			// dangle by the time the op is applied after the frame.
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
			case KBTYPEINFO_SHADER: {
				const std::vector<kbShader*>* const shaders = (const std::vector<kbShader*>*)byte_offset_to_var;
				DrawResourceField(field_name, element_type, component, parent_component, (u8*)&(*shaders)[i]);
				break;
			}
			case KBTYPEINFO_TEXTURE: {
				const std::vector<Texture*>* const textures = (const std::vector<Texture*>*)byte_offset_to_var;
				DrawResourceField(field_name, element_type, component, parent_component, (u8*)&(*textures)[i]);
				break;
			}
			default: {
				u8* const element_bytes = (u8*)g_NameToTypeInfoMap->GetVectorElement(byte_offset_to_var, struct_name, i);
				if (element_bytes == nullptr) {
					break;
				}
				if (element_type == KBTYPEINFO_STRUCT) {
					DrawComponent(editor_entity, (kbComponent*)element_bytes, component, true);
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

/// PropertiesPanel::DrawField
void PropertiesPanel::DrawField(const std::string& field_name, const kbTypeInfoType_t field_type, const std::string& struct_name,
	kbComponent* const component, kbComponent* const parent_component, u8* const byte_offset_to_var) {

	ImGui::TextUnformatted(field_name.c_str());
	ImGui::SameLine(140.0f);
	ImGui::SetNextItemWidth(-FLT_MIN);

	switch (field_type) {
	case KBTYPEINFO_BOOL: {
		ImGui::Checkbox("##value", (bool*)byte_offset_to_var);
		if (ImGui::IsItemActivated()) {
			m_PendingEdit = PendingEdit_t{ component, parent_component, field_type, field_name, byte_offset_to_var };
		}
		if (ImGui::IsItemDeactivatedAfterEdit()) {
			CommitPendingEdit();
		}
		break;
	}
	case KBTYPEINFO_INT: {
		ImGui::InputInt("##value", (int*)byte_offset_to_var);
		if (ImGui::IsItemActivated()) {
			m_PendingEdit = PendingEdit_t{ component, parent_component, field_type, field_name, byte_offset_to_var };
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
			m_PendingEdit = PendingEdit_t{ component, parent_component, field_type, field_name, byte_offset_to_var };
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
	case KBTYPEINFO_KBSTRING: {
		kbString& kb_string = *(kbString*)byte_offset_to_var;
		char buf[256];
		strncpy_s(buf, sizeof(buf), kb_string.c_str(), _TRUNCATE);
		ImGui::InputText("##value", buf, sizeof(buf));
		if (ImGui::IsItemActivated()) {
			m_PendingEdit = PendingEdit_t{ component, parent_component, field_type, field_name, byte_offset_to_var };
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
					m_PendingEdit = PendingEdit_t{ component, parent_component, field_type, field_name, byte_offset_to_var };
					CommitPendingEdit();
				}
			}
			ImGui::EndCombo();
		}
		break;
	}
	case KBTYPEINFO_GAMEENTITY:
		DrawGameEntityField(field_name, component, parent_component, byte_offset_to_var);
		break;
	case KBTYPEINFO_PTR:
	case KBTYPEINFO_TEXTURE:
	case KBTYPEINFO_STATICMODEL:
	case KBTYPEINFO_SOUNDWAVE:
	case KBTYPEINFO_SHADER:
	case KBTYPEINFO_ANIMATION:
		DrawResourceField(field_name, field_type, component, parent_component, byte_offset_to_var);
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
void PropertiesPanel::DrawGameEntityField(const std::string& field_name, kbComponent* const component, kbComponent* const parent_component,
	u8* const byte_offset_to_var) {

	GameEntityPtr* const entity_ptr = (GameEntityPtr*)byte_offset_to_var;
	const GameEntity* const current = entity_ptr->GetEntity();
	ImGui::TextDisabled("%s", (current != nullptr) ? current->name().c_str() : "(none)");
	ImGui::SameLine();
	if (ImGui::SmallButton("Pick")) {
		const kbPrefab* const prefab = g_Editor->GetCurrentlySelectedPrefab();
		GameEntity* const picked = g_pResourcesPanel->GetSelectedGameEntity().GetEntity();
		if (picked != nullptr || prefab == nullptr) {
			entity_ptr->SetEntity(picked);
		} else {
			entity_ptr->SetEntity(const_cast<GameEntity*>(prefab->GetGameEntity(0)));
		}
		NotifyEditorChange(component, parent_component, field_name);
	}
}

/// PropertiesPanel::DrawResourceField
///
/// Mirrors kbPropertiesTab::PointerButtonCB/ClearPointerButtonCB's resource
/// branches exactly (kbPropertiesTab.cpp:222-243, 267-277) -- same
/// type-check-before-assign guard, same editor_change/WidgetCB_EntityModified
/// broadcast, no undo push (matching current behavior).
void PropertiesPanel::DrawResourceField(const std::string& field_name, const kbTypeInfoType_t field_type, kbComponent* const component,
	kbComponent* const parent_component, u8* const byte_offset_to_var) {

	Resource** const resource_slot = (Resource**)byte_offset_to_var;
	ImGui::TextDisabled("%s", (*resource_slot != nullptr) ? (*resource_slot)->name().c_str() : "(none)");
	ImGui::SameLine();
	if (ImGui::SmallButton("Pick") && m_CurrentlySelectedResourceFileName.empty() == false) {
		Resource* const candidate = g_ResourceManager.resource(m_CurrentlySelectedResourceFileName.c_str(), true, true);
		if (candidate != nullptr && candidate->type() == field_type && candidate != *resource_slot) {
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
void PropertiesPanel::NotifyEditorChange(kbComponent* const component, kbComponent* const parent_component, const std::string& field_name) {
	component->editor_change(field_name);
	if (parent_component != nullptr) {
		parent_component->editor_change(field_name);
	}
}

/// PropertiesPanel::BroadcastPropertyChanged
void PropertiesPanel::BroadcastPropertyChanged() {
	if (m_pTempPrefabEntity != nullptr) {
		g_Editor->BroadcastEvent(widgetCBGeneric(WidgetCB_PrefabModified, m_pTempPrefabEntity->GetGameEntity()));
	}

	g_Editor->BroadcastEvent(widgetCBGeneric(WidgetCB_EntityModified, nullptr));
}

/// PropertiesPanel::CommitPendingEdit
void PropertiesPanel::CommitPendingEdit() {
	const PendingEdit_t edit = m_PendingEdit;
	kbComponent* const component = edit.component;
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
		// enabled component so they re-pick-up the new transform. Structs are
		// kbComponents but not kbGameComponents, so they have no owning entity
		// to walk -- the FLTK version made the same IsA() check first.
		GameEntity* const game_entity = component->IsA(kbGameComponent::GetType()) ? (GameEntity*)component->GetOwner() : nullptr;
		if (game_entity != nullptr && game_entity->component(0) == component) {
			for (size_t i = 0; i < game_entity->num_components(); i++) {
				kbComponent* const other = game_entity->component(i);
				if (other->IsEnabled()) {
					other->Enable(false);
					other->Enable(true);
				}
			}
		}
		NotifyEditorChange(component, edit.parent_component, edit.field_name);
	} else if (edit.type == KBTYPEINFO_ENUM) {
		// EnumCB's own distinct quirk: cycle Enable on the edited component
		// itself, not on the entity's other components.
		component->Enable(false);
		NotifyEditorChange(component, edit.parent_component, edit.field_name);
		component->Enable(true);
	} else {
		// BOOL (CheckButtonCB): its transform-cycle logic is dead/commented
		// out in the FLTK source today -- just editor_change, matching that.
		NotifyEditorChange(component, edit.parent_component, edit.field_name);
	}

	BroadcastPropertyChanged();
}
