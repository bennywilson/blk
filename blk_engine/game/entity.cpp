/// GameEntity.cpp
///
/// 2016 blk

#include "blk_core.h"
#include "blk_containers.h"
#include "Matrix.h"
#include "Quaternion.h"
#include "bounds.h"
#include "game.h"
#include "entity_header.h"

/// Entity::Entity
Entity::Entity() :
	m_bIsDirty(false) {
}

/// Entity::PostLoad
void Entity::post_load() {
	for (int i = 0; i < m_components.size(); i++) {
		m_components[i]->post_load();
	}
}

/// Entity::add_component
void Entity::add_component(kbComponent* const pComponent, int indexToInsertAt) {
	pComponent->SetOwner(this);

	const int lastComponentIdx = (int)m_components.size();
	if (indexToInsertAt == -1) {
		indexToInsertAt = lastComponentIdx;
	}

	if (pComponent->IsA(kbGameLogicComponent::GetType())) {
		indexToInsertAt = lastComponentIdx;
	} else if (m_components.size() > 0 && indexToInsertAt == lastComponentIdx) {
		while (indexToInsertAt > 0 && m_components[indexToInsertAt - 1]->IsA(kbGameLogicComponent::GetType())) {
			indexToInsertAt--;
		}
	}

	if (indexToInsertAt < 0 || indexToInsertAt >= m_components.size()) {
		m_components.push_back(pComponent);
	} else {
		m_components.insert(m_components.begin() + indexToInsertAt, pComponent);
	}
}

/// Entity::remove_component
void Entity::remove_component(kbComponent* const pComponent) {

	// TODO: This might move the game logic component from the back of the list!
	blk::std_remove_swap(m_components, pComponent);
}

//===================================================================================================
//	GameEntityPtr
//===================================================================================================
bool operator<(const kbGUID& a, const kbGUID& b) {
	return memcmp(&a, &b, sizeof(b)) < 0;
}

struct EntPtrHash {
	size_t operator()(const kbGUID& op) const {
		return std::hash<int>()(op.m_iGuid[0]) ^ std::hash<int>()(op.m_iGuid[1]) ^ std::hash<int>()(op.m_iGuid[2]) ^ std::hash<int>()(op.m_iGuid[3]);
	}
};

std::unordered_map<kbGUID, GameEntity*, EntPtrHash> g_GUIDToEntityMap;
std::unordered_map<int, GameEntity*> g_IndexToEntityMap;

/// GameEntityPtr::SetEntity
void GameEntityPtr::SetEntity(const kbGUID& guid) {
	m_GUID = guid;
	m_EntityId = INVALID_ENTITYID;

	std::unordered_map<kbGUID, GameEntity*, EntPtrHash>::const_iterator GUIDToEntityIt = g_GUIDToEntityMap.find(m_GUID);
	if (GUIDToEntityIt == g_GUIDToEntityMap.end()) {
		g_GUIDToEntityMap[guid] = nullptr;
	} else {
		if (GUIDToEntityIt->second != nullptr) {
			m_EntityId = GUIDToEntityIt->second->GetEntityId();
		}
	}
}

/// GameEntityPtr::SetEntity
void GameEntityPtr::SetEntity(GameEntity* const pGameEntity) {
	if (pGameEntity == nullptr) {
		m_EntityId = INVALID_ENTITYID;
		ZeroMemory(&m_GUID, sizeof(m_GUID));
		return;
	}

	// Enter into guid map
	m_GUID = pGameEntity->guid();

	if (m_GUID.IsValid()) {
		std::unordered_map<kbGUID, GameEntity*, EntPtrHash>::const_iterator GUIDToEntityIt = g_GUIDToEntityMap.find(m_GUID);

		if (GUIDToEntityIt != g_GUIDToEntityMap.cend() && GUIDToEntityIt->second != pGameEntity && GUIDToEntityIt->second != nullptr) {

			blk::error("GameEntityPtr::SetEntity() - Entities %s && %s share the same guid - %u %u %u %u",
					  pGameEntity->name().c_str(), GUIDToEntityIt->second->name().c_str(),
					  m_GUID.m_iGuid[0], m_GUID.m_iGuid[1], m_GUID.m_iGuid[2], m_GUID.m_iGuid[3]);
		}

		g_GUIDToEntityMap[m_GUID] = pGameEntity;
	}

	// Enter into entity index map
	m_EntityId = pGameEntity->GetEntityId();

	std::unordered_map<int, GameEntity*>::const_iterator IDToEntityIt = g_IndexToEntityMap.find(m_EntityId);

	if (IDToEntityIt != g_IndexToEntityMap.cend() && IDToEntityIt->second != pGameEntity && IDToEntityIt->second != nullptr) {
		blk::error("GameEntityPtr::SetEntity() - Entities %s && %s share the same guid - %u %u %u %u",
				 pGameEntity->name().c_str(), IDToEntityIt->second->name().c_str(),
				 m_GUID.m_iGuid[0], m_GUID.m_iGuid[1], m_GUID.m_iGuid[2], m_GUID.m_iGuid[3]);
	}

	g_IndexToEntityMap[m_EntityId] = pGameEntity;
}

/// GameEntityPtr::GetEntity
GameEntity* GameEntityPtr::GetEntity() {
	if (m_EntityId != INVALID_ENTITYID) {

		std::unordered_map<int, GameEntity*>::const_iterator it = g_IndexToEntityMap.find(m_EntityId);
		if (it != g_IndexToEntityMap.cend()) {
			return it->second;
		}
	}

	std::unordered_map<kbGUID, GameEntity*, EntPtrHash>::const_iterator it = g_GUIDToEntityMap.find(m_GUID);
	if (it == g_GUIDToEntityMap.cend()) {
		return nullptr;
	}

	return it->second;
}

const GameEntity* GameEntityPtr::GetEntity() const {
	GameEntityPtr* const pThisNoConst = const_cast<GameEntityPtr*>(this);
	return pThisNoConst->GetEntity();
}

/// GameEntityPtr::GetGUID
kbGUID GameEntityPtr::GetGUID() const {
	return m_GUID;
}

//===================================================================================================
//	GameEntity
//===================================================================================================

uint g_EntityNumber = 0;

/// GameEntity
GameEntity::GameEntity(const kbGUID* const guid, const bool bIsPrefab) :
	m_Bounds(Vec3(-1.0f, -1.0f, -1.0f), Vec3(1.0f, 1.0f, 1.0f)),
	m_pActorComponent(nullptr),
	m_pOwnerEntity(nullptr),
	m_EntityId(g_EntityNumber++),
	m_bIsPrefab(bIsPrefab),
	m_bDeleteWhenComponentsAreInactive(false) {

	m_pTransformComponent = new TransformComponent();
	m_pTransformComponent->set_position(Vec3::zero);
	add_component(m_pTransformComponent);

	if (bIsPrefab == false) {
		const std::string newName = "Entity_" + std::to_string(m_EntityId);
		m_pTransformComponent->set_name(newName.c_str());

		if (guid != nullptr) {
			m_guid = *guid;
		}
	} else {
		m_pTransformComponent->set_name("Prefab");

		if (guid != nullptr) {
			m_guid = *guid;
		} else if (g_UseEditor == true) {
			CoCreateGuid(&m_guid.m_Guid);
		} else {
			blk::error("GameEntity::GameEntity() - Prefab created with invalid GUID");
		}
	}

	if (m_guid.IsValid() == false) {
		CoCreateGuid(&m_guid.m_Guid);
	}

	GameEntityPtr entityPtr;
	entityPtr.SetEntity(this);
}

/// GameEntity::GameEntity( const GameEntity * )
GameEntity::GameEntity(const GameEntity* pGameEntity, const bool bIsPrefab, const kbGUID* const guid) :
	m_Bounds(pGameEntity->get_bounds()),
	m_pActorComponent(nullptr),
	m_pOwnerEntity(nullptr),
	m_EntityId(g_EntityNumber++),
	m_bIsPrefab(bIsPrefab),
	m_bDeleteWhenComponentsAreInactive(false) {

	for (int i = 0; i < pGameEntity->m_components.size(); i++) {
		const kbTypeInfoClass* const pTypeInfoClass = g_NameToTypeInfoMap->GetTypeInfoFromClassName(pGameEntity->m_components[i]->GetComponentClassName());
		kbComponent* newComponent = pTypeInfoClass->ConstructInstance(pGameEntity->m_components[i]);
		add_component(newComponent);

		if (i == 0) {
			m_pTransformComponent = (TransformComponent*)newComponent;
			if (m_pTransformComponent->IsA(TransformComponent::GetType()) == false) {
				blk::error("GameEntity::GameEntity() - Somehow the first component is not the transform component");
			}
		}
	}

	if (m_bIsPrefab == false) {
		for (int i = 0; i < m_components.size(); i++) {
			if (pGameEntity->m_components[i]->IsEnabled()) {
				m_components[i]->Enable(false);
				m_components[i]->Enable(true);
			} else {
				m_components[i]->Enable(false);
			}
		}
	}

	if (bIsPrefab == false) {
		if (guid != nullptr) {
			m_guid = *guid;
		}
	} else {
		if (guid != nullptr) {
			m_guid = *guid;
		} else if (g_UseEditor == true) {
			CoCreateGuid(&m_guid.m_Guid);
		} else {
			blk::error("GameEntity::GameEntity() - Prefab created with invalid GUID");
		}
	}

	if (m_guid.IsValid() == false) {
		CoCreateGuid(&m_guid.m_Guid);
	}

	GameEntityPtr entityPtr;
	entityPtr.SetEntity(this);
}

/// GameEntity::~GameEntity
GameEntity::~GameEntity() {
	// Disable components first so they can clean up any cross references before destruction
	for (int i = 0; i < m_components.size(); i++) {
		m_components[i]->Enable(false);
	}

	for (int i = 0; i < m_components.size(); i++) {
		delete m_components[i];
	}

	for (int i = 0; i < m_ChildEntities.size(); i++) {
		delete m_ChildEntities[i];
	}

	if (m_guid.IsValid()) {
		g_GUIDToEntityMap.erase(m_guid);
	}

	blk::error_check(m_EntityId != INVALID_ENTITYID, "GameEntity::~GameEntity() - Destroying entity with an invalid entity id");
	g_IndexToEntityMap.erase(m_EntityId);
}

/// GameEntity::add_component
void GameEntity::add_component(kbComponent* const pComponent, int indexToInsertAt) {

	if (pComponent == nullptr || pComponent->IsA(kbGameComponent::GetType()) == false) {
		blk::error("%s is trying to add a null component or one that is not a kbGameComponent.", name().c_str());
	}

	if (pComponent->IsA(kbActorComponent::GetType())) {
		if (m_pActorComponent != nullptr) {
			blk::error("%s is trying to add multiple kbGameLogicComponent.", name().c_str());
			return;
		}

		m_pActorComponent = static_cast<kbActorComponent*>(pComponent);
	}

	Entity::add_component(pComponent, indexToInsertAt);
}

/// GameEntity::add_entity
void GameEntity::add_entity(GameEntity* const pEntity) {
	pEntity->m_pOwnerEntity = this;
	m_ChildEntities.push_back(pEntity);

	// Make sure pEntity is not in kbGame's list as it will now be managed by this
	g_pGame->RemoveGameEntity(pEntity);
}

/// GameEntity::Update
void GameEntity::update(const float DeltaTime) {
	START_SCOPED_TIMER(GAME_ENTITY_UPDATE)

	{
		START_SCOPED_TIMER(COMPONENT_UPDATE)

			for (int i = 0; i < m_components.size(); i++) {
				// todo: make sure entity is still valid before updating the next component (ex. projectile may have removed the entity)
				if (component(i)->IsEnabled()) {
					component(i)->Update(DeltaTime);
				}
			}
	}

	for (int i = 0; i < m_ChildEntities.size(); i++) {
		m_ChildEntities[i]->update(DeltaTime);
	}

	if (m_bDeleteWhenComponentsAreInactive) {
		bool bActiveComponentsExist = false;
		for (int i = 1; i < m_components.size(); i++) {
			if (m_components[i]->IsEnabled()) {
				bActiveComponentsExist = true;
				break;
			}
		}

		if (bActiveComponentsExist == false) {
			g_pGame->RemoveGameEntity(this);
			return;
		}
	}

	clear_dirty();
}

/// GameEntity::enable_all_components
void GameEntity::enable_all_components() {
	for (int i = 0; i < m_components.size(); i++) {
		m_components[i]->Enable(true);
	}
}

/// GameEntity::disable_all_components
void GameEntity::disable_all_components() {
	for (int i = 0; i < m_components.size(); i++) {
		m_components[i]->Enable(false);
	}
}

/// GameEntity::RenderSync
void GameEntity::render_sync() {

	for (int i = 0; i < m_components.size(); i++) {
		m_components[i]->render_sync();
	}

	for (int i = 0; i < m_ChildEntities.size(); i++) {
		m_ChildEntities[i]->render_sync();
	}
}

/// GameEntity::calculate_world_matrix
void GameEntity::calculate_world_matrix(Mat4& inOutMatrix) const {

	Mat4 scaleMat(Mat4::identity);

	const float modelScale = kbLevelComponent::GetGlobalModelScale();
	scaleMat[0].x = scale().x * modelScale;
	scaleMat[1].y = scale().y * modelScale;
	scaleMat[2].z = scale().z * modelScale;

	inOutMatrix = scaleMat * rotation().to_mat4();
	inOutMatrix[3] = position();
	inOutMatrix[3].w = 1.0f;
}

/// GameEntity::get_world_bounds
kbBounds GameEntity::get_world_bounds() const {
	kbBounds returnBounds = m_Bounds;
	returnBounds.Scale(scale());
	returnBounds.Translate(position());
	return returnBounds;
}

/// GameEntity::GetRotation
const Quat4 GameEntity::rotation() const {
	if (m_pOwnerEntity != nullptr) {
		// This entity's Rotation is in model space while the parent's is in world
		return  m_pTransformComponent->rotation() * m_pOwnerEntity->rotation();
	}

	return m_pTransformComponent->rotation();
}

/// GameEntity::position
const Vec3 GameEntity::position() const {
	if (m_pOwnerEntity != nullptr) {
		const Quat4 entityRotation = rotation();
		const Vec3 worldSpaceOffset = entityRotation.to_mat4().transform_point(m_pTransformComponent->position());
		return worldSpaceOffset + m_pOwnerEntity->position();
	}

	return m_pTransformComponent->position();
}

/// GameEntity::GetComponentByType
kbComponent* GameEntity::GetComponentByType(const void* const pTypeInfoClass) const {
	if (pTypeInfoClass == nullptr) {
		return nullptr;
	}

	for (int i = 0; i < m_components.size(); i++) {
		if (m_components[i]->IsA(pTypeInfoClass)) {
			return m_components[i];
		}
	}

	return nullptr;
}
