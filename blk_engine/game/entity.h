/// GameEntity.h
///
/// 2016 blk

#pragma once

#include <rpc.h>

#define INVALID_ENTITYID UINT_MAX

bool operator<(const Guid& a, const Guid& b);

/// GameEntityPtr - All entities have a unique m_EntityId per game instance.  Entities loaded from disk (ex. from a .blklevel or .blkpkg file) will have m_GUID set
class GameEntityPtr {
public:
	GameEntityPtr() :
		m_EntityId(INVALID_ENTITYID) {}

	bool operator==(const GameEntityPtr op2) const { return m_EntityId == op2.m_EntityId; }

	void SetEntity(const Guid& guid);
	void SetEntity(GameEntity* const pGameEntity);
	GameEntity* GetEntity();
	const GameEntity* GetEntity() const;

	Guid GetGUID() const;

	int GetEntityIndex() const { return m_EntityId; }

private:

	Guid m_GUID;
	uint m_EntityId;
};

/// Entity
class Entity {
public:
	Entity();
	virtual ~Entity() {}

	void post_load();

	virtual void add_component(Component* const component, int insert_idx = -1);
	virtual void remove_component(Component* const component);

	Component* get_component(const size_t index) const { return m_components[index]; }
	size_t num_components() const { return m_components.size(); }

	const Guid& guid() const { return m_guid; }

	void mark_dirty() {
		m_bIsDirty = true;
		for (int i = 0; i < m_components.size(); i++) {
			m_components[i]->MarkAsDirty();
		}
	}
	bool is_dirty() const { return m_bIsDirty; }

protected:
	void clear_dirty() { m_bIsDirty = false; }

	std::vector<Component*> m_components;

	// Entities that came from file (level, package, etc) have a GUID
	Guid m_guid;

private:
	bool m_bIsDirty : 1;
};

/// GameEntity - GameEntities can only have GameComponents in their m_Components list
class GameEntity : public Entity {
public:

	explicit GameEntity(const Guid* const guid = nullptr, const bool bIsPrefab = false);
	explicit GameEntity(const GameEntity* const, const bool bIsPrefab, const Guid* const guid = nullptr);

	virtual ~GameEntity();

	void add_entity(GameEntity* const pEntity);
	virtual void add_component(Component* const pComponent, int indexToInsertAt = -1) override;
	GameComponent* component(const size_t index) const { return (GameComponent*)m_components[index]; }

	void update(const float DeltaTime);

	void enable_all_components();
	void disable_all_components();

	void render_sync();

	// Accessors
	const String& name() const { return m_pTransformComponent->name(); }

	const Vec3 position() const;
	void set_position(const Vec3& newPosition) {
		m_pTransformComponent->set_position(newPosition);
		mark_dirty();
	}

	const Quat4 rotation() const;
	void set_rotation(const Quat4& newRotation) {
		m_pTransformComponent->set_rotation(newRotation);
		mark_dirty();
	}

	const Vec3 scale() const { return m_pTransformComponent->scale(); }
	void set_scale(const Vec3& newScale) {
		m_pTransformComponent->set_scale(newScale);
		mark_dirty();
	}

	void calculate_world_matrix(Mat4& worldMatrix) const;

	const Bounds& get_bounds() const { return m_Bounds; }
	Bounds get_world_bounds() const;

	bool is_prefab() const { return m_bIsPrefab; }

	void DeleteWhenComponentsAreInactive(const bool bDelete) { m_bDeleteWhenComponentsAreInactive = bDelete; }

	GameEntity* owner() const { return m_pOwnerEntity; }

	ActorComponent* GetActorComponent() const { return m_pActorComponent; }
	Component* GetComponentByType(const void* const pTypeInfoClass) const;

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
	Bounds m_Bounds;

	TransformComponent* m_pTransformComponent;  // For convenience.  This is always the first entry in the m_Components list
	ActorComponent* m_pActorComponent;   // Only one ActorComponent is allowed per GameEntity
	std::vector<GameEntity*> m_ChildEntities;
	GameEntity* m_pOwnerEntity;

	// All entities will have a m_EntityId.  They're temporary values that may differ between game instances
	uint m_EntityId;

	bool m_bIsPrefab : 1;
	bool m_bDeleteWhenComponentsAreInactive : 1;
};

/// Prefab
class Prefab {
	friend class Editor;
	friend class ResourceManager;
	friend class File;

public:
	~Prefab() {}

	const std::string& GetPrefabName() const { return m_PrefabName; }
	const size_t NumGameEntities() const { return m_GameEntities.size(); }
	const GameEntity* GetGameEntity(const int idx) const { return m_GameEntities[idx]; }

private:
	Prefab() {}

	GUID m_GUID;

	std::string m_PrefabName;
	std::vector<GameEntity*> m_GameEntities;
};
