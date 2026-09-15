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
	ELevelType m_LevelType;
	f32 m_GlobalModelScale;
	f32 m_EditorIconScale;
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
	eCinematicActionType m_CineActionType;
	String m_sCineParam;
	f32 m_fCineParam;
	GameEntityPtr m_pCineParam;
	Vec3 m_vCineParam;

	f32 m_ActionStartTime;
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
	std::vector<CinematicAction> m_Actions;
};
