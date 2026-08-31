/// properties_panel.h
///
/// 2026 blk

#pragma once

#include "editor_panel.h"

class kbGameComponent;

/// PropertiesPanel
///
/// Phase 3, Milestone 3: reflection-driven property grid for the single
/// currently-selected entity's components, running alongside the existing
/// FLTK "Entity Info" tab (kbPropertiesTab) -- not a replacement for it.
/// Immediate-mode reader/writer over the existing reflection system
/// (type_info.h) and entity/component state -- no new engine-side data
/// structures, following OutlinerPanel's precedent.
///
/// In scope: BOOL, INT, FLOAT, VECTOR, VECTOR4, KBSTRING, ENUM, GAMEENTITY,
/// and the Resource-pointer types (PTR/TEXTURE/STATICMODEL/SOUNDWAVE/
/// SHADER/ANIMATION). Deferred: KBTYPEINFO_STRUCT fields and any
/// IsArray()==true field (rendered as a placeholder row), multi-selection,
/// and temp-prefab-entity editing.
class PropertiesPanel : public EditorPanel {
public:
	PropertiesPanel(const int x, const int y, const int w, const int h);

	virtual void draw_imgui() override;
	virtual void EventCB(const widgetCBObject* const widget_cb_object) override;

private:
	void DrawComponent(kbGameComponent* const component);
	void DrawField(const std::string& field_name, const kbTypeInfoType_t field_type, const std::string& struct_name,
		kbGameComponent* const component, u8* const byte_offset_to_var);
	void DrawGameEntityField(const std::string& field_name, kbGameComponent* const component, u8* const byte_offset_to_var);
	void DrawResourceField(const std::string& field_name, const kbTypeInfoType_t field_type, kbGameComponent* const component, u8* const byte_offset_to_var);

	// Fires the write-back/undo/editor_change/broadcast side-effects exactly
	// once per finished scalar edit -- see PendingEdit_t below.
	void CommitPendingEdit();

	// Transient "value at activation" snapshot. Dear ImGui guarantees at
	// most one widget is active at a time, so a single slot (not a map
	// keyed by component+offset) is sufficient -- scoped entirely inside
	// this panel, analogous to ImGui's own internal per-ID widget state,
	// not a new persistent engine-side structure.
	struct PendingEdit_t {
		kbGameComponent* component = nullptr;
		kbTypeInfoType_t type = KBTYPEINFO_NONE;
		std::string field_name;
		void* byte_offset_to_var = nullptr;

		float float_snapshot = 0.0f;
		int int_snapshot = 0;
		std::string string_snapshot;
	};
	PendingEdit_t m_PendingEdit;

	// Independently tracks the ResourceTab's last-broadcast selection via
	// the same WidgetCB_ResourceSelected event kbPropertiesTab itself
	// listens to -- zero changes needed to ResourceTab/kbPropertiesTab.
	std::string m_CurrentlySelectedResourceFileName;
};
