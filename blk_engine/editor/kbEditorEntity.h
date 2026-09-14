/// kbEditorEntity.h
///
/// 2016 blk
#pragma once

/// kbEditorEntity
class kbEditorEntity {
	friend class kbEditor;

public:
	kbEditorEntity();
	kbEditorEntity(GameEntity *const );
	~kbEditorEntity();

	void Update( const float DT );
	void render_sync();

	bool IsSelected() const { return m_bIsSelected; }
	void SetIsSelected( bool bIsSelected ) { m_bIsSelected = bIsSelected; }

	const kbBounds GetWorldBounds() const;

	const Vec3 position() const;
	void set_position( const Vec3 & newPosition );

	const Quat4	rotation() const;
	void set_rotation( const Quat4 & newOrientation );

	const Vec3 scale() const;
	void set_scale( const Vec3 & newScale );

	GameEntity* GetGameEntity() const;
	void SetGameEntity(GameEntity* const gameEntity) { m_pGameEntity = gameEntity; }

	// For non-editable entities that don't appear in editor panels.
	// ex: kbLevelComponent
	bool IsHidden() const { return m_bHidden; }
	void SetHidden(const bool bHidden) { m_bHidden = bHidden; }

private:
	GameEntity*	m_pGameEntity;

	bool m_bIsSelected : 1;
	bool m_bHidden : 1;
};
