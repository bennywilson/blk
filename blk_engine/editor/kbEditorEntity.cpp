/// kbEditorEntity.cpp
///
/// 2016 blk

#include "blk_core.h"
#include "Matrix.h"
#include "bounds.h"
#include "entity_header.h"
#include "kbEditorEntity.h"

/// kbEditorEntity::kbEditorEntity
kbEditorEntity::kbEditorEntity() :
	m_pGameEntity( new GameEntity() ),
	m_bIsSelected( false ) {

}

/// kbEditorEntity::kbEditorEntity
kbEditorEntity::kbEditorEntity( GameEntity * pGameEntity ) :
	m_pGameEntity( pGameEntity ),
	m_bIsSelected( false ) {
}

/// kbEditorEntity::~kbEditorEntity
kbEditorEntity::~kbEditorEntity() {
	delete m_pGameEntity;
}

/// kbEditorEntity::GetPropertyMetaData
varMetaData_t *	kbEditorEntity::GetPropertyMetaData( const kbComponent * pComponent, const size_t Offset ) {
	if ( m_pGameEntity == NULL || pComponent == NULL )
		return NULL;

	std::string metaDataLookUp = std::to_string( ( UINT_PTR )pComponent);
	metaDataLookUp += "_";
	metaDataLookUp += std::to_string( ( unsigned int ) (Offset));
	
	return &m_PropertyMetaData[metaDataLookUp];
}

/// kbEditorEntity::update
void kbEditorEntity::Update( const float DT ) {
	m_pGameEntity->update( DT );
}

/// kbEditorEntity::RenderSync
void kbEditorEntity::render_sync() {
	m_pGameEntity->render_sync();
}

/// kbEditorEntity::GetWorldBounds
const kbBounds kbEditorEntity::GetWorldBounds() const {
	return m_pGameEntity->get_world_bounds();
}

/// kbEditorEntity::GetPosition
const Vec3 kbEditorEntity::position() const {
	return m_pGameEntity->position(); 
}

/// kbEditorEntity::SetPosition
void kbEditorEntity::set_position( const Vec3 & newPosition ) {
	m_pGameEntity->set_position( newPosition ); 
}

/// kbEditorEntity::GetOrientation
const Quat4 kbEditorEntity::rotation() const {
	return m_pGameEntity->rotation(); 
}

/// kbEditorEntity::SetOrientation
void kbEditorEntity::set_rotation( const Quat4 & newOrientation ) {
	m_pGameEntity->set_rotation( newOrientation ); 
}

/// kbEditorEntity::GetScale
const Vec3 kbEditorEntity::scale() const {
	return m_pGameEntity->scale(); 
}

/// kbEditorEntity::SetScale
void kbEditorEntity::set_scale( const Vec3 & newScale ) {
	m_pGameEntity->set_scale( newScale ); 
}

/// kbEditorEntity::GetGameEntity
GameEntity * kbEditorEntity::GetGameEntity() const {
	return m_pGameEntity; 
}

