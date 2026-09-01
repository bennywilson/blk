/// kbEditorEntity.h
///
/// 2016 blk
#pragma once

// The editor will use this to store extra information about displayed properties
struct varMetaData_t {
	varMetaData_t() :
		bExpanded( false ) { }

	bool bExpanded;
};

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
	void SetGameEntity(GameEntity *const gameEntity ) { m_pGameEntity = gameEntity; m_PropertyMetaData.clear(); }

	varMetaData_t*	GetPropertyMetaData( const kbComponent * pComponent, const size_t Offset );

	// Phase 3, Milestone 4: for entities that must be tracked in
	// kbEditor::m_GameEntities (so the normal delete-all-on-unload lifecycle
	// owns them -- see kbEditor::LoadMap()'s level-settings entity) but
	// aren't a placeable, user-facing entity and shouldn't appear in the
	// Outliner/ResourceTab entity lists.
	bool IsHidden() const { return m_bHidden; }
	void SetHidden(const bool bHidden) { m_bHidden = bHidden; }

private:
	GameEntity*	m_pGameEntity;

	std::map< std::string, varMetaData_t > m_PropertyMetaData;

	bool m_bIsSelected : 1;
	bool m_bHidden : 1;
};
