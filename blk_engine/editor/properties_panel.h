/// properties_panel.h
///
/// 2026 blk

#pragma once

#include "editor_panel.h"

class kbComponent;
class kbEditorEntity;

/// PropertiesPanel
///
/// Phase 3, Milestones 3 and 6: reflection-driven property grid over the
/// existing reflection system (type_info.h) and entity/component state, with
/// no new engine-side data structures -- following OutlinerPanel's precedent.
/// Milestone 6 closed the last gaps against the FLTK "Entity Info" tab
/// (kbPropertiesTab) and deleted it, so this is now the only property grid.
///
/// Covers BOOL, INT, FLOAT, VECTOR, VECTOR4, KBSTRING, ENUM, GAMEENTITY, the
/// Resource-pointer types (PTR/TEXTURE/STATICMODEL/SOUNDWAVE/SHADER/
/// ANIMATION), KBTYPEINFO_STRUCT (recursive), array fields (resize/insert/
/// remove), component deletion, and prefab editing through a temporary
/// entity.
///
/// Editing more than one selected entity at once is NOT supported -- and was
/// not supported by the FLTK panel either, which gated its whole property
/// list on "exactly one entity selected" behind its own todo comment. This is
/// unimplemented in both, not a regression.
class PropertiesPanel : public EditorPanel {
public:
	PropertiesPanel(const int x, const int y, const int w, const int h);
	~PropertiesPanel();

	virtual void draw_imgui() override;
	virtual void EventCB(const widgetCBObject* const widget_cb_object) override;

private:
	// parent_component is the component owning `component` when `component` is
	// a KBTYPEINFO_STRUCT field being drawn recursively, else nullptr. Every
	// edit notifies both, matching kbPropertiesTab's m_pComponent /
	// m_pParentComponent pairing.
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

	// Fires the write-back/undo/editor_change/broadcast side-effects exactly
	// once per finished scalar edit -- see PendingEdit_t below.
	void CommitPendingEdit();

	void NotifyEditorChange(kbComponent* const component, kbComponent* const parent_component, const std::string& field_name);

	// kbPropertiesTab::PropertyChangedCB's equivalent. Every finished edit
	// broadcasts WidgetCB_EntityModified, and -- while a prefab is being
	// edited through m_pTempPrefabEntity -- WidgetCB_PrefabModified as well.
	// That second broadcast is the ONLY thing that marks a prefab and its
	// package dirty in ResourcesPanel, and nothing in the build catches its
	// absence, so it has to ride along with every write path.
	void BroadcastPropertyChanged();

	void ClearTempPrefabEntity();
	void ApplyPendingStructuralChanges();

	// Transient "value at activation" snapshot. Dear ImGui guarantees at
	// most one widget is active at a time, so a single slot (not a map
	// keyed by component+offset) is sufficient -- scoped entirely inside
	// this panel, analogous to ImGui's own internal per-ID widget state,
	// not a new persistent engine-side structure.
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

	// Structural edits -- removing a component, or resizing/inserting into/
	// removing from an array -- invalidate the very containers draw_imgui() is
	// mid-iteration over, so they are recorded here and applied after the
	// frame's widgets are done. The FLTK panel had no such hazard: its
	// callbacks fired from Windows' message dispatch, never from inside its
	// own draw.
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

	// Independently tracks ResourcesPanel's last-broadcast selection via
	// the same WidgetCB_ResourceSelected event it emits.
	std::string m_CurrentlySelectedResourceFileName;

	// Non-null while a prefab is selected in the Resources panel: a throwaway
	// kbEditorEntity wrapping the prefab's GameEntity(0), edited exactly as a
	// scene entity would be. It does NOT own that GameEntity -- detach it
	// before deleting the wrapper (see ClearTempPrefabEntity).
	kbEditorEntity* m_pTempPrefabEntity = nullptr;
};
