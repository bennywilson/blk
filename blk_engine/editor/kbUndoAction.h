/// kbUndoAction.h
///
/// 2016 blk

#pragma once

class kbUndoAction;

/// kbUndoStack
struct kbUndoStack {
	kbUndoStack();

	UINT64 GetLastDirtyActionId() const;

	void Push(kbUndoAction* const action);
	void Undo();
	void Redo();
	void Reset();
	void DumpStack();

	std::vector<kbUndoAction*> m_Stack;
	int m_StackTop;
	int m_StackCurrent;
	int m_StackLength;
	UINT64 m_NextUndoActionId;
};

/// kbUndoAction
///
/// Owns what the user deletes, keeping it alive for undo. The destructor frees it through Cleanup() while the action is still applied.
class kbUndoAction {
public:
	kbUndoAction() : m_UndoActionId(UINT64_MAX), m_bIsApplied(false) {}
	virtual ~kbUndoAction() {
		if (m_bIsApplied) {
			Cleanup();
		}
	}

	virtual void Cleanup() {}

	virtual void UndoAction() = 0;
	virtual void RedoAction() = 0;
	virtual bool MarksMapAsDirty() const = 0;

	UINT64 m_UndoActionId;
	bool m_bIsApplied;
};

/// kbUndoVariableAction
class kbUndoVariableAction : public kbUndoAction {
public:
	kbUndoVariableAction(const kbTypeInfoType_t type, void* const bytePtrToUndoValue, void* const bytePtrToRedoValue, void* const pVariable);

	virtual void UndoAction() override;
	virtual void RedoAction() override;
	virtual bool MarksMapAsDirty() const override { return true; }

private:
	void* m_pVariable;
	kbTypeInfoType_t m_VarType;

	bool m_UndoBoolean;
	int m_UndoInt;
	float m_UndoFloat;
	void* m_pUndoPtr;
	kbString m_UndoString;

	bool m_RedoBoolean;
	int m_RedoInt;
	float m_RedoFloat;
	void* m_pRedoPtr;
	kbString m_RedoString;
};

/// kbUndoDeleteComponent
class kbUndoDeleteComponent : public kbUndoAction {
public:
	kbUndoDeleteComponent(kbEditorEntity* const entity, kbComponent* const componentToDelete, const int indexIntoComponentList);
	virtual void Cleanup() override;

	virtual void UndoAction() override;
	virtual void RedoAction() override;
	virtual bool MarksMapAsDirty() const override { return true; }

private:
	kbEditorEntity* m_pEditorEntity;
	kbComponent* m_pComponent;
	int m_IndexIntoComponentList;
};

/// kbUndoDeleteActor
class kbUndoDeleteActor : public kbUndoAction {
public:
	struct DeletedActorInfo_t {
		kbEditorEntity* m_pEditorEntity;
		std::vector<bool> m_bComponentEnabled;
	};

	kbUndoDeleteActor(const std::vector<DeletedActorInfo_t>& entitiesToDelete);
	virtual void Cleanup() override;

	virtual void UndoAction() override;
	virtual void RedoAction() override;
	virtual bool MarksMapAsDirty() const override { return true; }

	const int NumDeleted() const { return (int)m_pEntitiesToDelete.size(); }

private:
	std::vector<DeletedActorInfo_t> m_pEntitiesToDelete;
};

/// kbUndoTransformEntities
///
/// Records one gizmo drag as parallel before/after T/R/S per selected entity, independent of which handle moved.
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
	kbUndoSelectActor(const std::vector<kbEditorEntity*>& undoEntities, const std::vector<kbEditorEntity*>& redoEntities);

	virtual void UndoAction() override;
	virtual void RedoAction() override;
	virtual bool MarksMapAsDirty() const override { return false; }

	const int NumSelected() const { return (int)m_UndoSelectedEntities.size(); }

private:
	std::vector<kbEditorEntity*> m_UndoSelectedEntities;
	std::vector<kbEditorEntity*> m_RedoSelectedEntities;
};
