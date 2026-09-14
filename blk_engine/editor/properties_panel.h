/// properties_panel.h
///
/// 2026 blk

#pragma once

#include "editor_panel.h"

class kbComponent;
class kbEditorEntity;

/// PropertiesPanel
///
/// Reflection-driven property grid for the selected entity, or for a selected prefab through a temporary wrapper entity.
/// TODO: Edits one entity at a time. A multi-selection only shows a notice.
class PropertiesPanel : public EditorPanel {
public:
	PropertiesPanel();
	~PropertiesPanel();

	virtual void draw_imgui() override;
	virtual void EventCB(const widgetCBObject* const widget_cb_object) override;

private:
	// parent_component owns `component` when it's a struct field drawn recursively, else nullptr. Every edit notifies both.
	void DrawComponent(kbEditorEntity* const editor_entity, kbComponent* const component, kbComponent* const parent_component,
		const bool is_struct);
	void DrawField(const std::string& field_name, const kbTypeInfoType_t field_type, const std::string& struct_name,
		kbComponent* const component, kbComponent* const parent_component, u8* const byte_offset_to_var);
	void DrawArrayField(kbEditorEntity* const editor_entity, const std::string& field_name, const kbTypeInfoType_t element_type,
		const std::string& struct_name, kbComponent* const component, kbComponent* const parent_component, u8* const byte_offset_to_var);
	void DrawGameEntityField(const std::string& field_name, kbComponent* const component, kbComponent* const parent_component,
		u8* const byte_offset_to_var);
	void DrawResourceField(const std::string& field_name, const kbTypeInfoType_t field_type, kbComponent* const component,
		kbComponent* const parent_component, u8* const byte_offset_to_var);

	// Fires the write-back, undo, editor_change, and broadcast once per finished scalar edit.
	void CommitPendingEdit();

	void NotifyEditorChange(kbComponent* const component, kbComponent* const parent_component, const std::string& field_name);

	// Broadcasts WidgetCB_EntityModified, plus WidgetCB_PrefabModified while a prefab is being edited.
	// The latter is the only thing that marks a prefab dirty in ResourcesPanel, so every write path must call this.
	void BroadcastPropertyChanged();

	void ClearTempPrefabEntity();
	void ApplyPendingStructuralChanges();

	// Value-at-activation snapshot. One slot suffices because ImGui allows only one active widget at a time.
	struct PendingEdit_t {
		kbComponent* component = nullptr;
		kbComponent* parent_component = nullptr;
		kbTypeInfoType_t type = KBTYPEINFO_NONE;
		std::string field_name;
		void* byte_offset_to_var = nullptr;

		float float_snapshot = 0.0f;
		int int_snapshot = 0;
		std::string string_snapshot;
	};
	PendingEdit_t m_PendingEdit;

	// Defers structural edits (component removal, array resize/insert/remove) until after the frame's widgets,
	// since they invalidate the containers draw_imgui() is iterating.
	struct PendingArrayOp_t {
		enum Op_t { Op_None, Op_Resize, Op_Insert, Op_Remove };

		Op_t op = Op_None;
		void* array_ptr = nullptr;
		std::string struct_name;
		kbTypeInfoType_t element_type = KBTYPEINFO_NONE;
		int index = 0;
		int new_size = 0;
		kbComponent* component = nullptr;
		kbComponent* parent_component = nullptr;
		std::string field_name;
	};
	PendingArrayOp_t m_PendingArrayOp;

	kbComponent* m_pComponentToDelete = nullptr;
	kbEditorEntity* m_pComponentToDeleteOwner = nullptr;

	// Mirrors ResourcesPanel's last WidgetCB_ResourceSelected broadcast.
	std::string m_CurrentlySelectedResourceFileName;

	// Wraps the selected prefab's GameEntity(0) for editing. It doesn't own that entity, so ClearTempPrefabEntity() detaches it before deleting.
	kbEditorEntity* m_pTempPrefabEntity = nullptr;
};
