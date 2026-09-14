/// kbUndoAction.cpp
///
/// 2016 blk

#include "blk_core.h"
#include "kbEditor.h"
#include "kbEditorEntity.h"
#include "kbUndoAction.h"

const int g_UndoStackSize = 15;
extern bool g_bEditorIsUndoingAnAction;

/// kbUndoStack::kbUndoStack
kbUndoStack::kbUndoStack() {
	Reset();
}

/// kbUndoStack::GetLastDirtyActionId
UINT64 kbUndoStack::GetLastDirtyActionId() const {
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

/// kbUndoStack::Reset
void kbUndoStack::Reset() {
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

/// kbUndoStack::Push
void kbUndoStack::Push(kbUndoAction* const action) {
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

/// kbUndoStack::Undo
void kbUndoStack::Undo() {
	if (m_StackLength == 0) {
		MessageBoxA(g_Editor ? g_Editor->hwnd() : nullptr, "Undo buffer is empty", "Undo", MB_OK | MB_ICONINFORMATION);
		return;
	}

	g_bEditorIsUndoingAnAction = true;
	m_Stack[m_StackCurrent]->UndoAction();
	m_Stack[m_StackCurrent]->m_bIsApplied = false;
	g_bEditorIsUndoingAnAction = false;

	m_StackLength--;
	m_StackCurrent--;
	if (m_StackCurrent < 0) {
		m_StackCurrent = g_UndoStackSize - 1;
	}

	DumpStack();
}

/// kbUndoStack::Redo
void kbUndoStack::Redo() {
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
	m_Stack[m_StackCurrent]->RedoAction();
	m_Stack[m_StackCurrent]->m_bIsApplied = true;
	g_bEditorIsUndoingAnAction = false;

	blk::log("Redo() ------------------------------------------");
	DumpStack();
}

/// kbUndoStack::DumpStack
void kbUndoStack::DumpStack() {
	int bottomIdx = m_StackCurrent - m_StackLength;
	if (bottomIdx < 0) {
		bottomIdx += g_UndoStackSize;
	}
}

/// kbUndoVariableAction::kbUndoVariableAction
kbUndoVariableAction::kbUndoVariableAction(const kbTypeInfoType_t type, void* const bytePtrToUndoValue, void* const bytePtrToRedoValue, void* const pVariable) {
	m_pVariable = pVariable;
	m_VarType = type;

	switch (type) {
		case KBTYPEINFO_BOOL: {
			m_UndoBoolean = *(const bool*)bytePtrToUndoValue;
			m_RedoBoolean = *(const bool*)bytePtrToRedoValue;
			break;
		}

		case KBTYPEINFO_INT:
		case KBTYPEINFO_ENUM: {
			m_UndoInt = *(const int*)bytePtrToUndoValue;
			m_RedoInt = *(const int*)bytePtrToRedoValue;
			break;
		}

		// TODO: Captures only the first float of VECTOR and VECTOR4 values.
		case KBTYPEINFO_VECTOR4:
		case KBTYPEINFO_VECTOR:
		case KBTYPEINFO_FLOAT: {
			m_UndoFloat = *(const float*)bytePtrToUndoValue;
			m_RedoFloat = *(const float*)bytePtrToRedoValue;
			break;
		}

		case KBTYPEINFO_KBSTRING: {
			m_UndoString = *(const kbString*)bytePtrToUndoValue;
			m_RedoString = *(const kbString*)bytePtrToRedoValue;
			break;
		}

		case KBTYPEINFO_PTR:
		case KBTYPEINFO_STATICMODEL:
		case KBTYPEINFO_SHADER:
		case KBTYPEINFO_ANIMATION: {
			m_pUndoPtr = bytePtrToUndoValue;
			m_pRedoPtr = bytePtrToRedoValue;
			break;
		}
	}
}

/// kbUndoVariableAction::UndoAction
void kbUndoVariableAction::UndoAction() {
	switch (m_VarType) {
		case KBTYPEINFO_VECTOR4:
		case KBTYPEINFO_VECTOR:
		case KBTYPEINFO_FLOAT: {
			*(float*)m_pVariable = m_UndoFloat;
			break;
		}

		case KBTYPEINFO_KBSTRING: {
			*(kbString*)m_pVariable = m_UndoString;
			break;
		}

		// TODO: Restores nothing for these types, although the constructor captures them.
		case KBTYPEINFO_BOOL:
		case KBTYPEINFO_INT:
		case KBTYPEINFO_ENUM:
		case KBTYPEINFO_PTR:
		case KBTYPEINFO_STATICMODEL:
		case KBTYPEINFO_SHADER:
		case KBTYPEINFO_ANIMATION: {
			break;
		}
	}
}

/// kbUndoVariableAction::RedoAction
void kbUndoVariableAction::RedoAction() {
	switch (m_VarType) {
		case KBTYPEINFO_VECTOR4:
		case KBTYPEINFO_VECTOR:
		case KBTYPEINFO_FLOAT: {
			*(float*)m_pVariable = m_RedoFloat;
			break;
		}

		case KBTYPEINFO_KBSTRING: {
			*(kbString*)m_pVariable = m_RedoString;
			break;
		}

		// TODO: Restores nothing for these types, although the constructor captures them.
		case KBTYPEINFO_BOOL:
		case KBTYPEINFO_INT:
		case KBTYPEINFO_ENUM:
		case KBTYPEINFO_PTR:
		case KBTYPEINFO_STATICMODEL:
		case KBTYPEINFO_SHADER:
		case KBTYPEINFO_ANIMATION: {
			break;
		}
	}
}

/// kbUndoDeleteComponent::kbUndoDeleteComponent
kbUndoDeleteComponent::kbUndoDeleteComponent(kbEditorEntity* const pEntity, kbComponent* const pComponentToDelete, const int indexIntoComponentList) :
	m_pEditorEntity(pEntity),
	m_pComponent(pComponentToDelete),
	m_IndexIntoComponentList(indexIntoComponentList) {
}

/// kbUndoDeleteComponent::Cleanup
void kbUndoDeleteComponent::Cleanup() {
	delete m_pComponent;
}

/// kbUndoDeleteComponent::UndoAction
void kbUndoDeleteComponent::UndoAction() {
	m_pEditorEntity->GetGameEntity()->add_component(m_pComponent, m_IndexIntoComponentList);

	std::vector<kbEditorEntity*> entityList;
	entityList.push_back(m_pEditorEntity);

	g_Editor->DeselectEntities();
	g_Editor->SelectEntities(entityList, false);
}

/// kbUndoDeleteComponent::RedoAction
void kbUndoDeleteComponent::RedoAction() {
	i32 componentIdx = -1;
	GameEntity* const pEntity = (GameEntity*)m_pComponent->GetOwner();	// ENTITY HACK
	for (componentIdx = 0; componentIdx < pEntity->num_components(); componentIdx++) {
		if (pEntity->component(componentIdx) == m_pComponent) {
			break;
		}
	}

	pEntity->remove_component(m_pComponent);

	std::vector<kbEditorEntity*> entityList;
	entityList.push_back(m_pEditorEntity);

	g_Editor->SelectEntities(entityList, false);
}

/// kbUndoDeleteActor::kbUndoDeleteActor
kbUndoDeleteActor::kbUndoDeleteActor(const std::vector<DeletedActorInfo_t>& entitiesToDelete) :
	m_pEntitiesToDelete(entitiesToDelete) {
}

/// kbUndoDeleteActor::Cleanup
void kbUndoDeleteActor::Cleanup() {
	for (int i = 0; i < m_pEntitiesToDelete.size(); i++) {
		delete m_pEntitiesToDelete[i].m_pEditorEntity;
	}
}

/// kbUndoDeleteActor::UndoAction
void kbUndoDeleteActor::UndoAction() {
	std::vector<kbEditorEntity*> entityList;

	for (int i = 0; i < m_pEntitiesToDelete.size(); i++) {
		kbEditorEntity* const pEntity = m_pEntitiesToDelete[i].m_pEditorEntity;

		for (int j = 0; j < pEntity->GetGameEntity()->num_components(); j++) {
			if (m_pEntitiesToDelete[i].m_bComponentEnabled[j]) {
				pEntity->GetGameEntity()->component(j)->Enable(true);
			}
		}
		g_Editor->AddEntity(pEntity);
		entityList.push_back(pEntity);
	}
}

/// kbUndoDeleteActor::RedoAction
void kbUndoDeleteActor::RedoAction() {
	std::vector<kbEditorEntity*>& gameEntities = g_Editor->GetGameEntities();
	for (int i = 0; i < m_pEntitiesToDelete.size(); i++) {
		kbEditorEntity* const pEntity = m_pEntitiesToDelete[i].m_pEditorEntity;
		gameEntities.erase(std::remove(gameEntities.begin(), gameEntities.end(), pEntity), gameEntities.end());
		for (int j = 0; j < pEntity->GetGameEntity()->num_components(); j++) {
			pEntity->GetGameEntity()->component(j)->Enable(false);
		}
		pEntity->render_sync();
	}
}

/// kbUndoTransformEntities::kbUndoTransformEntities
kbUndoTransformEntities::kbUndoTransformEntities(const std::vector<kbEditorEntity*>& entities, const std::vector<EntityTransform_t>& beforeTransforms, const std::vector<EntityTransform_t>& afterTransforms) :
	m_Entities(entities),
	m_BeforeTransforms(beforeTransforms),
	m_AfterTransforms(afterTransforms) {

	blk::error_check(m_Entities.size() == m_BeforeTransforms.size() && m_Entities.size() == m_AfterTransforms.size(),
		"kbUndoTransformEntities::kbUndoTransformEntities() - parallel vectors disagree (%d entities, %d before, %d after)",
		(int)m_Entities.size(), (int)m_BeforeTransforms.size(), (int)m_AfterTransforms.size());
}

/// kbUndoTransformEntities::ApplyTransforms
void kbUndoTransformEntities::ApplyTransforms(const std::vector<EntityTransform_t>& transforms) {
	if (m_Entities.size() != transforms.size()) {
		return;
	}

	// Skips entities no longer in the editor, comparing pointers only since a deleted one may already be freed.
	const std::vector<kbEditorEntity*>& liveEntities = g_Editor->GetGameEntities();
	for (size_t i = 0; i < m_Entities.size(); i++) {
		kbEditorEntity* const pEntity = m_Entities[i];
		if (std::find(liveEntities.begin(), liveEntities.end(), pEntity) == liveEntities.end()) {
			continue;
		}

		pEntity->set_position(transforms[i].m_position);
		pEntity->set_rotation(transforms[i].m_rotation);
		pEntity->set_scale(transforms[i].m_scale);
	}
}

/// kbUndoTransformEntities::UndoAction
void kbUndoTransformEntities::UndoAction() {
	ApplyTransforms(m_BeforeTransforms);
}

/// kbUndoTransformEntities::RedoAction
void kbUndoTransformEntities::RedoAction() {
	ApplyTransforms(m_AfterTransforms);
}

/// kbUndoSelectActor::kbUndoSelectActor
kbUndoSelectActor::kbUndoSelectActor(const std::vector<kbEditorEntity*>& undoEntities, const std::vector<kbEditorEntity*>& redoEntities) :
	m_UndoSelectedEntities(undoEntities),
	m_RedoSelectedEntities(redoEntities) {
}

/// kbUndoSelectActor::UndoAction
void kbUndoSelectActor::UndoAction() {
	g_Editor->SelectEntities(m_UndoSelectedEntities, false);
}

/// kbUndoSelectActor::RedoAction
void kbUndoSelectActor::RedoAction() {
	g_Editor->DeselectEntities();
	g_Editor->SelectEntities(m_RedoSelectedEntities, false);
}

