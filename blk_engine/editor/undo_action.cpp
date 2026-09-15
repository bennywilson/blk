/// undo_action.cpp
///
/// 2016 blk

#include "blk_core.h"
#include "editor.h"
#include "editor_entity.h"
#include "undo_action.h"

const int g_UndoStackSize = 15;
extern bool g_bEditorIsUndoingAnAction;

/// UndoStack::UndoStack
UndoStack::UndoStack() {
	Reset();
}

/// UndoStack::GetLastDirtyActionId
UINT64 UndoStack::GetLastDirtyActionId() const {
	if (m_StackCurrent < 0) {
		return UINT64_MAX;
	}

	for (int i = 0; i < m_StackLength; i++) {
		int curIdx = m_StackCurrent - i;
		if (curIdx < 0) {
			curIdx += g_UndoStackSize;
		}

		if (m_Stack[curIdx]->MarksMapAsDirty()) {
			return m_Stack[curIdx]->m_UndoActionId;
		}
	}

	return UINT64_MAX;
}

/// UndoStack::Reset
void UndoStack::Reset() {
	for (int i = 0; i < m_Stack.size(); i++) {
		if (m_Stack[i]) {
			delete m_Stack[i];
			m_Stack[i] = nullptr;
		}
	}
	m_Stack.clear();

	m_StackTop = -1;
	m_StackLength = 0;
	m_StackCurrent = -1;

	m_Stack.resize(g_UndoStackSize);
	m_NextUndoActionId = UINT64_MAX;
}

/// UndoStack::Push
void UndoStack::Push(UndoAction* const action) {
	m_StackCurrent++;

	if (m_StackCurrent >= g_UndoStackSize) {
		m_StackCurrent = 0;
	}

	if (m_StackLength < g_UndoStackSize) {
		m_StackLength++;
	}

	m_StackTop = m_StackCurrent;

	if (m_Stack[m_StackCurrent]) {
		delete m_Stack[m_StackCurrent];
	}

	if (m_NextUndoActionId == UINT64_MAX) {
		m_NextUndoActionId = 0;
	}

	// Marks the action applied because callers push it after performing it, so eviction must run Cleanup().
	action->m_UndoActionId = m_NextUndoActionId++;
	action->m_bIsApplied = true;
	m_Stack[m_StackCurrent] = action;

	DumpStack();
}

/// UndoStack::Undo
void UndoStack::Undo() {
	if (m_StackLength == 0) {
		MessageBoxA(g_Editor ? g_Editor->hwnd() : nullptr, "Undo buffer is empty", "Undo", MB_OK | MB_ICONINFORMATION);
		return;
	}

	g_bEditorIsUndoingAnAction = true;
	m_Stack[m_StackCurrent]->Undo();
	m_Stack[m_StackCurrent]->m_bIsApplied = false;
	g_bEditorIsUndoingAnAction = false;

	m_StackLength--;
	m_StackCurrent--;
	if (m_StackCurrent < 0) {
		m_StackCurrent = g_UndoStackSize - 1;
	}

	DumpStack();
}

/// UndoStack::Redo
void UndoStack::Redo() {
	if (m_StackTop == m_StackCurrent) {
		MessageBoxA(g_Editor ? g_Editor->hwnd() : nullptr, "No more actions to redo", "Redo", MB_OK | MB_ICONINFORMATION);
		return;
	}

	m_StackCurrent++;
	m_StackLength++;
	if (m_StackCurrent >= g_UndoStackSize) {
		m_StackCurrent = 0;
	}

	g_bEditorIsUndoingAnAction = true;
	m_Stack[m_StackCurrent]->Redo();
	m_Stack[m_StackCurrent]->m_bIsApplied = true;
	g_bEditorIsUndoingAnAction = false;

	blk::log("Redo() ------------------------------------------");
	DumpStack();
}

/// UndoStack::DumpStack
void UndoStack::DumpStack() {
	int bottomIdx = m_StackCurrent - m_StackLength;
	if (bottomIdx < 0) {
		bottomIdx += g_UndoStackSize;
	}
}

/// UndoVariableAction::UndoVariableAction
UndoVariableAction::UndoVariableAction(const TypeInfoType_t type, void* const bytePtrToUndoValue, void* const bytePtrToRedoValue, void* const pVariable) {
	m_pVariable = pVariable;
	m_VarType = type;

	switch (type) {
		case BLK_TYPEINFO_BOOL: {
			m_UndoBoolean = *(const bool*)bytePtrToUndoValue;
			m_RedoBoolean = *(const bool*)bytePtrToRedoValue;
			break;
		}

		case BLK_TYPEINFO_INT:
		case BLK_TYPEINFO_ENUM: {
			m_UndoInt = *(const int*)bytePtrToUndoValue;
			m_RedoInt = *(const int*)bytePtrToRedoValue;
			break;
		}

		// TODO: Captures only the first float of VECTOR and VECTOR4 values.
		case BLK_TYPEINFO_VECTOR4:
		case BLK_TYPEINFO_VECTOR:
		case BLK_TYPEINFO_FLOAT: {
			m_UndoFloat = *(const float*)bytePtrToUndoValue;
			m_RedoFloat = *(const float*)bytePtrToRedoValue;
			break;
		}

		case BLK_TYPEINFO_STRING: {
			m_UndoString = *(const String*)bytePtrToUndoValue;
			m_RedoString = *(const String*)bytePtrToRedoValue;
			break;
		}

		case BLK_TYPEINFO_PTR:
		case BLK_TYPEINFO_STATICMODEL:
		case BLK_TYPEINFO_SHADER:
		case BLK_TYPEINFO_ANIMATION: {
			m_pUndoPtr = bytePtrToUndoValue;
			m_pRedoPtr = bytePtrToRedoValue;
			break;
		}
	}
}

/// UndoVariableAction::UndoAction
void UndoVariableAction::Undo() {
	switch (m_VarType) {
		case BLK_TYPEINFO_VECTOR4:
		case BLK_TYPEINFO_VECTOR:
		case BLK_TYPEINFO_FLOAT: {
			*(float*)m_pVariable = m_UndoFloat;
			break;
		}

		case BLK_TYPEINFO_STRING: {
			*(String*)m_pVariable = m_UndoString;
			break;
		}

		// TODO: Restores nothing for these types, although the constructor captures them.
		case BLK_TYPEINFO_BOOL:
		case BLK_TYPEINFO_INT:
		case BLK_TYPEINFO_ENUM:
		case BLK_TYPEINFO_PTR:
		case BLK_TYPEINFO_STATICMODEL:
		case BLK_TYPEINFO_SHADER:
		case BLK_TYPEINFO_ANIMATION: {
			break;
		}
	}
}

/// UndoVariableAction::RedoAction
void UndoVariableAction::Redo() {
	switch (m_VarType) {
		case BLK_TYPEINFO_VECTOR4:
		case BLK_TYPEINFO_VECTOR:
		case BLK_TYPEINFO_FLOAT: {
			*(float*)m_pVariable = m_RedoFloat;
			break;
		}

		case BLK_TYPEINFO_STRING: {
			*(String*)m_pVariable = m_RedoString;
			break;
		}

		// TODO: Restores nothing for these types, although the constructor captures them.
		case BLK_TYPEINFO_BOOL:
		case BLK_TYPEINFO_INT:
		case BLK_TYPEINFO_ENUM:
		case BLK_TYPEINFO_PTR:
		case BLK_TYPEINFO_STATICMODEL:
		case BLK_TYPEINFO_SHADER:
		case BLK_TYPEINFO_ANIMATION: {
			break;
		}
	}
}

/// UndoDeleteComponent::UndoDeleteComponent
UndoDeleteComponent::UndoDeleteComponent(EditorEntity* const pEntity, Component* const pComponentToDelete, const int indexIntoComponentList) :
	m_pEditorEntity(pEntity),
	m_pComponent(pComponentToDelete),
	m_IndexIntoComponentList(indexIntoComponentList) {
}

/// UndoDeleteComponent::Cleanup
void UndoDeleteComponent::Cleanup() {
	delete m_pComponent;
}

/// UndoDeleteComponent::UndoAction
void UndoDeleteComponent::Undo() {
	m_pEditorEntity->GetGameEntity()->add_component(m_pComponent, m_IndexIntoComponentList);

	std::vector<EditorEntity*> entityList;
	entityList.push_back(m_pEditorEntity);

	g_Editor->DeselectEntities();
	g_Editor->SelectEntities(entityList, false);
}

/// UndoDeleteComponent::RedoAction
void UndoDeleteComponent::Redo() {
	i32 componentIdx = -1;
	GameEntity* const pEntity = (GameEntity*)m_pComponent->GetOwner(); // ENTITY HACK
	for (componentIdx = 0; componentIdx < pEntity->num_components(); componentIdx++) {
		if (pEntity->component(componentIdx) == m_pComponent) {
			break;
		}
	}

	pEntity->remove_component(m_pComponent);

	std::vector<EditorEntity*> entityList;
	entityList.push_back(m_pEditorEntity);

	g_Editor->SelectEntities(entityList, false);
}

/// UndoDeleteActor::UndoDeleteActor
UndoDeleteActor::UndoDeleteActor(const std::vector<DeletedActorInfo_t>& entitiesToDelete) :
	m_pEntitiesToDelete(entitiesToDelete) {
}

/// UndoDeleteActor::Cleanup
void UndoDeleteActor::Cleanup() {
	for (int i = 0; i < m_pEntitiesToDelete.size(); i++) {
		delete m_pEntitiesToDelete[i].m_pEditorEntity;
	}
}

/// UndoDeleteActor::UndoAction
void UndoDeleteActor::Undo() {
	std::vector<EditorEntity*> entityList;

	for (int i = 0; i < m_pEntitiesToDelete.size(); i++) {
		EditorEntity* const pEntity = m_pEntitiesToDelete[i].m_pEditorEntity;

		for (int j = 0; j < pEntity->GetGameEntity()->num_components(); j++) {
			if (m_pEntitiesToDelete[i].m_bComponentEnabled[j]) {
				pEntity->GetGameEntity()->component(j)->Enable(true);
			}
		}
		g_Editor->AddEntity(pEntity);
		entityList.push_back(pEntity);
	}
}

/// UndoDeleteActor::RedoAction
void UndoDeleteActor::Redo() {
	std::vector<EditorEntity*>& gameEntities = g_Editor->GetGameEntities();
	for (int i = 0; i < m_pEntitiesToDelete.size(); i++) {
		EditorEntity* const pEntity = m_pEntitiesToDelete[i].m_pEditorEntity;
		gameEntities.erase(std::remove(gameEntities.begin(), gameEntities.end(), pEntity), gameEntities.end());
		for (int j = 0; j < pEntity->GetGameEntity()->num_components(); j++) {
			pEntity->GetGameEntity()->component(j)->Enable(false);
		}
		pEntity->render_sync();
	}
}

/// UndoTransformEntities::UndoTransformEntities
UndoTransformEntities::UndoTransformEntities(const std::vector<EditorEntity*>& entities, const std::vector<EntityTransform_t>& beforeTransforms, const std::vector<EntityTransform_t>& afterTransforms) :
	m_Entities(entities),
	m_BeforeTransforms(beforeTransforms),
	m_AfterTransforms(afterTransforms) {

	blk::error_check(m_Entities.size() == m_BeforeTransforms.size() && m_Entities.size() == m_AfterTransforms.size(),
		"UndoTransformEntities::UndoTransformEntities() - parallel vectors disagree (%d entities, %d before, %d after)",
		(int)m_Entities.size(), (int)m_BeforeTransforms.size(), (int)m_AfterTransforms.size());
}

/// UndoTransformEntities::ApplyTransforms
void UndoTransformEntities::ApplyTransforms(const std::vector<EntityTransform_t>& transforms) {
	if (m_Entities.size() != transforms.size()) {
		return;
	}

	// Skips entities no longer in the editor, comparing pointers only since a deleted one may already be freed.
	const std::vector<EditorEntity*>& liveEntities = g_Editor->GetGameEntities();
	for (size_t i = 0; i < m_Entities.size(); i++) {
		EditorEntity* const pEntity = m_Entities[i];
		if (std::find(liveEntities.begin(), liveEntities.end(), pEntity) == liveEntities.end()) {
			continue;
		}

		pEntity->set_position(transforms[i].m_position);
		pEntity->set_rotation(transforms[i].m_rotation);
		pEntity->set_scale(transforms[i].m_scale);
	}
}

/// UndoTransformEntities::UndoAction
void UndoTransformEntities::Undo() {
	ApplyTransforms(m_BeforeTransforms);
}

/// UndoTransformEntities::RedoAction
void UndoTransformEntities::Redo() {
	ApplyTransforms(m_AfterTransforms);
}

/// UndoSelectActor::UndoSelectActor
UndoSelectActor::UndoSelectActor(const std::vector<EditorEntity*>& undoEntities, const std::vector<EditorEntity*>& redoEntities) :
	m_UndoSelectedEntities(undoEntities),
	m_RedoSelectedEntities(redoEntities) {
}

/// UndoSelectActor::UndoAction
void UndoSelectActor::Undo() {
	g_Editor->SelectEntities(m_UndoSelectedEntities, false);
}

/// UndoSelectActor::RedoAction
void UndoSelectActor::Redo() {
	g_Editor->DeselectEntities();
	g_Editor->SelectEntities(m_RedoSelectedEntities, false);
}
