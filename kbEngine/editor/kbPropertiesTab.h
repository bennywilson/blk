/// kbPropertiesTab.h
///
/// 2016-2025 blk 1.0

#pragma once

#pragma warning(push)
#pragma warning(disable:4312)
#include <fl/fl_tabs.h>
#pragma warning(pop)

class kbEditorEntity;

struct propertiesTabCBData_t {
	propertiesTabCBData_t(
		kbEditorEntity* const pEditorEntity,
		const kbGameEntityPtr* const inGameEntityPtr,
		kbComponent* const pComponent,
		kbComponent* const pParentComponent,
		const kbResource** pResource,
		const kbString variableName,
		void* const pVariableValue,
		const kbTypeInfoType_t variableType,
		const std::string& structName,
		const void* const pArray,
		const i32 arrayIdx = -1
	);

	kbEditorEntity* m_pEditorEntity;
	kbGameEntityPtr	m_GameEntityPtr;
	kbComponent* m_pComponent;
	kbComponent* m_pParentComponent;
	const kbResource** m_pResource;
	kbString m_VariableName;
	void* m_pVariablePtr;
	kbTypeInfoType_t m_VariableType;
	std::string m_StructName;
	const void* m_pArray;
	i32	m_ArrayIndex;
};

/// kbPropertiesTab
class kbPropertiesTab : public Fl_Tabs, kbWidget {
public:
	kbPropertiesTab(const i32 x, const i32 y, const i32 w, const i32 h);

	virtual void EventCB(const widgetCBObject* const widgetCBObject) override;

	virtual void Update() override;

	void RequestRefreshNextUpdate() { m_bRefreshNextUpdate = true; }

	std::vector<kbEditorEntity*>& GetSelectedEntities() { return m_SelectedEntities; }
	kbEditorEntity* GetTempPrefabEntity() { return m_pTempPrefabEntity; }

private:
	void RefreshEntity();
	void RefreshComponent(kbEditorEntity* const pEntity,kbComponent* const pComponent, kbComponent* const pParentComponent, i32& startX, i32& curY, const i32 inputHeight, const bool bIsStruct = false, const void* const arrayPtr = nullptr, const i32 arrayIndex = -1);
	void RefreshProperty(kbEditorEntity* const pEntity, const std::string& propertyName, const kbTypeInfoType_t propertyType, const std::string& structName, kbComponent* const pComponent, const u8* const byteOffsetToVar, kbComponent* const pParentComponent, i32& x, i32& y, const int inputHeight, const void* const arrayPtr = nullptr, const i32 arrayIndex = -1);

	u32 FontSize()	const { return 10; }
	u32 LineSpacing() const { return FontSize() + 5; }

	Fl_Tabs* m_pPropertiesTab;
	Fl_Group* m_pEntityProperties;
	Fl_Group* m_pResourceProperties;

	std::vector<kbEditorEntity*> m_SelectedEntities;
	std::string	m_CurrentlySelectedResource;
	std::vector<propertiesTabCBData_t> m_CallBackData;

	kbEditorEntity* m_pTempPrefabEntity;
	bool m_bRefreshNextUpdate;

	static void CheckButtonCB(Fl_Widget* widget, void* voidPtr);
	static void PointerButtonCB(Fl_Widget* widget, void* voidPtr);
	static void ClearPointerButtonCB(Fl_Widget* widget, void* voidPtr);
	static void TextFieldCB(Fl_Widget* widget, void* voidPtr);
	static void ArrayExpandCB(Fl_Widget* widet, void* voidPtr);
	static void ArrayResizeCB(Fl_Widget* widget, void* voidPtr);
	static void EnumCB(Fl_Widget* widget, void* voidPtr);
	static void DeleteComponent(Fl_Widget* widget, void* voidPtr);
	static void InsertArrayStruct(Fl_Widget* widget, void* voidPtr);
	static void DeleteArrayStruct(Fl_Widget* widget, void* voidPtr);
	static void PropertyChangedCB(const kbGameEntityPtr gameEntityPtr);		// Each call back should this before returning
};

extern kbPropertiesTab* g_pPropertiesTab;
