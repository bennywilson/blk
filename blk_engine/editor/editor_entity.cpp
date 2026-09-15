/// editor_entity.cpp
///
/// 2016 blk

#include "blk_core.h"
#include "entity_header.h"
#include "editor_entity.h"

/// EditorEntity::EditorEntity
EditorEntity::EditorEntity() :
	m_pGameEntity(new GameEntity()),
	m_bIsSelected(false),
	m_bHidden(false) {
}

/// EditorEntity::EditorEntity
EditorEntity::EditorEntity(GameEntity* pGameEntity) :
	m_pGameEntity(pGameEntity),
	m_bIsSelected(false),
	m_bHidden(false) {
}

/// EditorEntity::~EditorEntity
EditorEntity::~EditorEntity() {
	delete m_pGameEntity;
}

/// EditorEntity::update
void EditorEntity::Update(const float DT) {
	m_pGameEntity->update(DT);
}

/// EditorEntity::RenderSync
void EditorEntity::render_sync() {
	m_pGameEntity->render_sync();
}

/// EditorEntity::GetWorldBounds
const Bounds EditorEntity::GetWorldBounds() const {
	return m_pGameEntity->get_world_bounds();
}

/// EditorEntity::GetPosition
const Vec3 EditorEntity::position() const {
	return m_pGameEntity->position();
}

/// EditorEntity::SetPosition
void EditorEntity::set_position(const Vec3& newPosition) {
	m_pGameEntity->set_position(newPosition);
}

/// EditorEntity::GetOrientation
const Quat4 EditorEntity::rotation() const {
	return m_pGameEntity->rotation();
}

/// EditorEntity::SetOrientation
void EditorEntity::set_rotation(const Quat4& newOrientation) {
	m_pGameEntity->set_rotation(newOrientation);
}

/// EditorEntity::GetScale
const Vec3 EditorEntity::scale() const {
	return m_pGameEntity->scale();
}

/// EditorEntity::SetScale
void EditorEntity::set_scale(const Vec3& newScale) {
	m_pGameEntity->set_scale(newScale);
}

/// EditorEntity::GetGameEntity
GameEntity* EditorEntity::GetGameEntity() const {
	return m_pGameEntity;
}
