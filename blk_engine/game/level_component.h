/// level_component.h
///
/// 2016-2025 blk 1.0

#pragma once

#include "entity.h"

/// ELevelType
enum ELevelType {
	LevelType_Menu,
	LevelType_2D
};

/// kbLevelComponent
class kbLevelComponent : public kbGameComponent {
	KB_DECLARE_COMPONENT(kbLevelComponent, kbGameComponent)
public:
	~kbLevelComponent();

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

/// kbCinematicAction
class kbCinematicAction : public kbGameComponent {
public:
	friend class kbCinematicComponent;

	KB_DECLARE_COMPONENT(kbCinematicAction, kbGameComponent);

private:
	eCinematicActionType m_CineActionType;
	kbString m_sCineParam;
	f32 m_fCineParam;
	GameEntityPtr m_pCineParam;
	Vec3 m_vCineParam;

	f32 m_ActionStartTime;
	f32 m_ActionDuration;
};

/// kbCinematicComponent
class kbCinematicComponent : public kbGameComponent {
public:
	KB_DECLARE_COMPONENT(kbCinematicComponent, kbGameComponent);

	virtual ~kbCinematicComponent();

protected:
	void enable_internal(const bool bEnable) override;
	void update_internal(const float dt) override;

private:
	std::vector<kbCinematicAction> m_Actions;
};
