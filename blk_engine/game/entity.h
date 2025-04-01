/// GameEntity.h
///
/// 2016-2025 blk 1.0

#pragma once

#include <rpc.h>

#define INVALID_ENTITYID UINT_MAX

bool operator<(const kbGUID& a, const kbGUID& b);

/// GameEntityPtr - All entities have a unique m_EntityId per game instance.  Entities loaded from disk (ex. from a .kblevel or .kbPkg file) will have m_GUID set
class GameEntityPtr {
public:
	GameEntityPtr() : m_EntityId(INVALID_ENTITYID) { }

	bool operator==(const GameEntityPtr op2) const { return m_EntityId == op2.m_EntityId; }

	void SetEntity(const kbGUID& guid);
	void SetEntity(GameEntity* const pGameEntity);
	GameEntity* GetEntity();
	const GameEntity* GetEntity() const;

	kbGUID GetGUID() const;

	int	GetEntityIndex() const { return m_EntityId; }

private:

	kbGUID m_GUID;
	uint m_EntityId;
};

/// Entity
class Entity {
public:
	Entity();
	virtual	~Entity() { }

	void post_load();

	virtual void add_component(kbComponent* const component, int insert_idx = -1);
	virtual void remove_component(kbComponent* const component);

	kbComponent* get_component(const size_t index) const { return m_components[index]; }
	size_t num_components() const { return m_components.size(); }

	const kbGUID& guid() const { return m_guid; }

	void mark_dirty() { m_bIsDirty = true; for (int i = 0; i < m_components.size(); i++) { m_components[i]->MarkAsDirty(); } }
	bool is_dirty() const { return m_bIsDirty; }

protected:
	void clear_dirty() { m_bIsDirty = false; }

	std::vector<kbComponent*> m_components;

	// Entities that came from file (level, package, etc) have a GUID
	kbGUID m_guid;

private:
	bool m_bIsDirty : 1;
};

/// GameEntity - kbGameEntities can only have kbGameComponents in their m_Components list
class GameEntity : public Entity {
public:

	explicit GameEntity(const kbGUID* const guid = nullptr, const bool bIsPrefab = false);
	explicit GameEntity(const GameEntity* const, const bool bIsPrefab, const kbGUID* const guid = nullptr);

	virtual	~GameEntity();
  
	void add_entity(GameEntity* const pEntity);
	virtual void add_component(kbComponent* const pComponent, int indexToInsertAt = -1) override;
	kbGameComponent* component(const size_t index) const { return (kbGameComponent*)m_components[index]; }

	void update(const float DeltaTime);
		 
	void enable_all_components();
	void disable_all_components();
		 
	void render_sync();

	// Accessors
	const kbString& name() const { return m_pTransformComponent->name(); }

	const Vec3 position() const;
	void set_position(const Vec3& newPosition) { m_pTransformComponent->set_position(newPosition); mark_dirty(); }

	const Quat4 rotation() const;
	void set_rotation(const Quat4& newRotation) { m_pTransformComponent->set_rotation(newRotation); mark_dirty(); }

	const Vec3 scale() const { return m_pTransformComponent->scale(); }
	void set_scale(const Vec3& newScale) { m_pTransformComponent->set_scale(newScale); mark_dirty(); }

	void calculate_world_matrix(Mat4& worldMatrix) const;

	const kbBounds& get_bounds() const { return m_Bounds; }
	kbBounds get_world_bounds() const;

	bool is_prefab() const { return m_bIsPrefab; }

	void DeleteWhenComponentsAreInactive(const bool bDelete) { m_bDeleteWhenComponentsAreInactive = bDelete; }

	GameEntity* owner() const { return m_pOwnerEntity; }

	kbActorComponent* GetActorComponent() const { return m_pActorComponent; }
	kbComponent* GetComponentByType(const void* const pTypeInfoClass) const;

	template<typename T>
	T* component() const {
		for (int i = 0; i < m_components.size(); i++) {
			if (m_components[i]->IsA(T::GetType())) {
				return (T*)m_components[i];
			}
		}
		return nullptr;
	}

	const std::vector<GameEntity*>& GetChildEntities() const { return m_ChildEntities; }

	const uint GetEntityId() const { return m_EntityId; }

private:
	kbBounds m_Bounds;

	TransformComponent* m_pTransformComponent;		// For convenience.  This is always the first entry in the m_Components list
	kbActorComponent* m_pActorComponent;			// Only one kbActorComponent is allowed per GameEntity
	std::vector<GameEntity*> m_ChildEntities;
	GameEntity* m_pOwnerEntity;

	// All entities will have a m_EntityId.  They're temporary values that may differ between game instances
	uint m_EntityId;

	bool m_bIsPrefab : 1;
	bool m_bDeleteWhenComponentsAreInactive : 1;
};

/// kbPrefab
class kbPrefab {
	friend class kbEditor;
	friend class ResourceManager;
	friend class kbFile;

public:
	~kbPrefab() { }

	const std::string& GetPrefabName() const { return m_PrefabName; }
	const size_t NumGameEntities() const { return m_GameEntities.size(); }
	const GameEntity* GetGameEntity(const int idx) const { return m_GameEntities[idx]; }

private:
	kbPrefab() { }

	GUID m_GUID;

	std::string	m_PrefabName;
	std::vector<GameEntity*> m_GameEntities;
};
