/// kbUndoAction.h
///
/// 2016 blk

#pragma once

/// kbUndoStack
struct kbUndoStack {
	kbUndoStack();

	UINT64 GetLastDirtyActionId() const;

	void Push(class kbUndoAction* const action);
	void Undo();
	void Redo();
	void Reset();

	std::vector<class kbUndoAction*> m_Stack;
	int	m_StackTop;
	int	m_StackCurrent;
	int	m_StackLength;

	void DumpStack();

	UINT64 m_NextUndoActionId;
};


/// kbUndoAction - Base class for undo actions.
///				   When the user "deletes" a component, entity, etc, the editor forgets about it and relies on the undo action to eventuall perform the deletion.

class kbUndoAction {
public:
	kbUndoAction() : m_UndoActionId(UINT64_MAX), m_bHasBeenRedone(false) { }
	virtual	~kbUndoAction() {
		if (m_bHasBeenRedone) {
			Cleanup();
		}
	}

	virtual void Cleanup() { }

	virtual void UndoAction() = 0;
	virtual void RedoAction() = 0;
	virtual bool MarksMapAsDirty() const = 0;

	UINT64 m_UndoActionId;
	bool m_bHasBeenRedone;
};

/// kbUndoVariableAction
class kbUndoVariableAction : public kbUndoAction {
public:
	kbUndoVariableAction(kbTypeInfoType_t type, void* bytePtrToUndoValue, void* bytePtrToRedoValue, void* pVariable);

	virtual void UndoAction() override;
	virtual void RedoAction() override;
	virtual bool MarksMapAsDirty() const override { return true; }

private:
	void* m_pVariable;
	kbTypeInfoType_t				m_VarType;

	bool							m_UndoBoolean;
	int								m_UndoInt;
	float							m_UndoFloat;
	void* m_pUndoPtr;
	kbString						m_UndoString;

	bool							m_RedoBoolean;
	int								m_RedoInt;
	float							m_RedoFloat;
	void* m_pRedoPtr;
	kbString						m_RedoString;
};

/// kbUndoDeleteComponent
class kbUndoDeleteComponent : public kbUndoAction {
public:
	kbUndoDeleteComponent(kbEditorEntity* const entity, kbComponent* const componentToDelete, int indexIntoComponentList);
	virtual void Cleanup();

	virtual void UndoAction() override;
	virtual void RedoAction() override;
	virtual bool MarksMapAsDirty() const override { return true; }

private:
	kbEditorEntity* m_pEditorEntity;
	kbComponent* m_pComponent;
	int	m_IndexIntoComponentList;
};

/// kbUndoDeleteActor
class kbUndoDeleteActor : public kbUndoAction {
public:
	struct DeletedActorInfo_t {
		kbEditorEntity* m_pEditorEntity;
		std::vector<bool>	m_bComponentEnabled;
	};

	kbUndoDeleteActor(std::vector<DeletedActorInfo_t>& entitiesToDelete);
	virtual void Cleanup();

	virtual void UndoAction() override;
	virtual void RedoAction() override;
	virtual bool MarksMapAsDirty() const override { return true; }

	const int NumDeleted() const { return (int)m_pEntitiesToDelete.size(); }

private:
	std::vector<DeletedActorInfo_t>	m_pEntitiesToDelete;
};

/// kbUndoTransformEntities
///
/// Phase 3: one viewport-gizmo drag == one action. The gizmo transforms the
/// whole selection at once, so this holds parallel before/after vectors rather
/// than the single value kbUndoVariableAction carries. Position, rotation and
/// scale are all recorded even though any one drag only changes one of them --
/// that costs a copy per entity and keeps the action independent of which
/// T/R/S handle produced it.
///
/// The entity pointers are borrowed, never owned: unlike kbUndoDeleteActor
/// this action never takes an entity out of the editor's list, so it has no
/// Cleanup(). It does have to survive an entity being deleted while it sits in
/// the stack, though, so both directions filter against
/// kbEditor::GetGameEntities() before dereferencing anything (same guard
/// kbMainTab::DrawGizmo() and PropertiesPanel::draw_imgui() already apply to
/// the selection list).
class kbUndoTransformEntities : public kbUndoAction {
public:
	struct EntityTransform_t {
		Vec3 m_position;
		Quat4 m_rotation;
		Vec3 m_scale;
	};

	kbUndoTransformEntities(const std::vector<kbEditorEntity*>& entities, const std::vector<EntityTransform_t>& beforeTransforms, const std::vector<EntityTransform_t>& afterTransforms);

	virtual void UndoAction() override;
	virtual void RedoAction() override;
	virtual bool MarksMapAsDirty() const override { return true; }

	const int NumTransformed() const { return (int)m_Entities.size(); }

private:
	void ApplyTransforms(const std::vector<EntityTransform_t>& transforms);

	std::vector<kbEditorEntity*> m_Entities;
	std::vector<EntityTransform_t> m_BeforeTransforms;
	std::vector<EntityTransform_t> m_AfterTransforms;
};

/// kbUndoSelectActor
class kbUndoSelectActor : public kbUndoAction {
public:
	kbUndoSelectActor(std::vector<kbEditorEntity*>& undoEntities, std::vector<kbEditorEntity*>& redoEntities);

	virtual void UndoAction() override;
	virtual void RedoAction() override;
	virtual bool MarksMapAsDirty() const override { return false; }

	const int NumSelected() const { return (int)m_UndoSelectedEntities.size(); }

private:
	std::vector<kbEditorEntity*> m_UndoSelectedEntities;
	std::vector<kbEditorEntity*> m_RedoSelectedEntities;
};
