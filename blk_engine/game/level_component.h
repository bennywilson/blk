/// level_component.h
///
/// 2016 blk

#pragma once

#include "entity.h"

/// ELevelType
enum ELevelType {
	LevelType_Menu,
	LevelType_2D
};

/// LevelComponent
class LevelComponent : public GameComponent {
	BLK_DECLARE_COMPONENT(LevelComponent, GameComponent)
public:
	~LevelComponent();

	ELevelType GetLevelType() const { return m_LevelType; }

	static f32 GetGlobalModelScale();
	static f32 GetEditorIconScale();
	static f32 GetGlobalVolumeScale();

protected:
	virtual void editor_change(const std::string& propertyName) override;

	virtual void enable_internal(const bool bEnable) override;

private:
	virtual void pdateDebugAndCheats() {}

	// Editor
	BLK_PROPERTY()
	ELevelType m_LevelType;
	BLK_PROPERTY()
	f32 m_GlobalModelScale;
	BLK_PROPERTY()
	f32 m_EditorIconScale;
	BLK_PROPERTY()
	f32 m_GlobalVolumeScale;
};

/// eCinematicActionType
enum eCinematicActionType {
	CineAction_Override,
	CineAction_Animate,
	CineAction_MoveTo
};

/// CinematicAction
class CinematicAction : public GameComponent {
public:
	friend class CinematicComponent;

	BLK_DECLARE_COMPONENT(CinematicAction, GameComponent);

private:
	BLK_PROPERTY()
	eCinematicActionType m_CineActionType;
	BLK_PROPERTY()
	String m_StringParam;
	BLK_PROPERTY()
	f32 m_FloatParam;
	BLK_PROPERTY()
	GameEntityPtr m_EntityParam;
	BLK_PROPERTY()
	Vec3 m_VectorParam;

	BLK_PROPERTY()
	f32 m_ActionStartTime;
	BLK_PROPERTY()
	f32 m_ActionDuration;
};

/// CinematicComponent
class CinematicComponent : public GameComponent {
public:
	BLK_DECLARE_COMPONENT(CinematicComponent, GameComponent);

	virtual ~CinematicComponent();

protected:
	void enable_internal(const bool bEnable) override;
	void update_internal(const float dt) override;

private:
	BLK_PROPERTY()
	std::vector<CinematicAction> m_Actions;
};
