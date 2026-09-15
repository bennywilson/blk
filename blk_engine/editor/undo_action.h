/// undo_action.h
///
/// 2016 blk

#pragma once

class UndoAction;

/// UndoStack
struct UndoStack {
	UndoStack();

	UINT64 GetLastDirtyActionId() const;

	void Push(UndoAction* const action);
	void Undo();
	void Redo();
	void Reset();
	void DumpStack();

	std::vector<UndoAction*> m_Stack;
	int m_StackTop;
	int m_StackCurrent;
	int m_StackLength;
	UINT64 m_NextUndoActionId;
};

/// UndoAction
///
/// Owns what the user deletes, keeping it alive for undo. The destructor frees it through Cleanup() while the action is still applied.
class UndoAction {
public:
	UndoAction() : m_UndoActionId(UINT64_MAX), m_bIsApplied(false) {}
	virtual ~UndoAction() {
		if (m_bIsApplied) {
			Cleanup();
		}
	}

	virtual void Cleanup() {}

	virtual void Undo() = 0;
	virtual void Redo() = 0;
	virtual bool MarksMapAsDirty() const = 0;

	UINT64 m_UndoActionId;
	bool m_bIsApplied;
};

/// UndoVariableAction
class UndoVariableAction : public UndoAction {
public:
	UndoVariableAction(const TypeInfoType_t type, void* const bytePtrToUndoValue, void* const bytePtrToRedoValue, void* const pVariable);

	virtual void Undo() override;
	virtual void Redo() override;
	virtual bool MarksMapAsDirty() const override { return true; }

private:
	void* m_pVariable;
	TypeInfoType_t m_VarType;

	bool m_UndoBoolean;
	int m_UndoInt;
	float m_UndoFloat;
	void* m_pUndoPtr;
	String m_UndoString;

	bool m_RedoBoolean;
	int m_RedoInt;
	float m_RedoFloat;
	void* m_pRedoPtr;
	String m_RedoString;
};

/// UndoDeleteComponent
class UndoDeleteComponent : public UndoAction {
public:
	UndoDeleteComponent(EditorEntity* const entity, Component* const componentToDelete, const int indexIntoComponentList);
	virtual void Cleanup() override;

	virtual void Undo() override;
	virtual void Redo() override;
	virtual bool MarksMapAsDirty() const override { return true; }

private:
	EditorEntity* m_pEditorEntity;
	Component* m_pComponent;
	int m_IndexIntoComponentList;
};

/// UndoDeleteActor
class UndoDeleteActor : public UndoAction {
public:
	struct DeletedActorInfo_t {
		EditorEntity* m_pEditorEntity;
		std::vector<bool> m_bComponentEnabled;
	};

	UndoDeleteActor(const std::vector<DeletedActorInfo_t>& entitiesToDelete);
	virtual void Cleanup() override;

	virtual void Undo() override;
	virtual void Redo() override;
	virtual bool MarksMapAsDirty() const override { return true; }

	const int NumDeleted() const { return (int)m_pEntitiesToDelete.size(); }

private:
	std::vector<DeletedActorInfo_t> m_pEntitiesToDelete;
};

/// UndoTransformEntities
///
/// Records one gizmo drag as parallel before/after T/R/S per selected entity, independent of which handle moved.
class UndoTransformEntities : public UndoAction {
public:
	struct EntityTransform_t {
		Vec3 m_position;
		Quat4 m_rotation;
		Vec3 m_scale;
	};

	UndoTransformEntities(const std::vector<EditorEntity*>& entities, const std::vector<EntityTransform_t>& beforeTransforms, const std::vector<EntityTransform_t>& afterTransforms);

	virtual void Undo() override;
	virtual void Redo() override;
	virtual bool MarksMapAsDirty() const override { return true; }

	const int NumTransformed() const { return (int)m_Entities.size(); }

private:
	void ApplyTransforms(const std::vector<EntityTransform_t>& transforms);

	std::vector<EditorEntity*> m_Entities;
	std::vector<EntityTransform_t> m_BeforeTransforms;
	std::vector<EntityTransform_t> m_AfterTransforms;
};

/// UndoSelectActor
class UndoSelectActor : public UndoAction {
public:
	UndoSelectActor(const std::vector<EditorEntity*>& undoEntities, const std::vector<EditorEntity*>& redoEntities);

	virtual void Undo() override;
	virtual void Redo() override;
	virtual bool MarksMapAsDirty() const override { return false; }

	const int NumSelected() const { return (int)m_UndoSelectedEntities.size(); }

private:
	std::vector<EditorEntity*> m_UndoSelectedEntities;
	std::vector<EditorEntity*> m_RedoSelectedEntities;
};
