/// component.h
///
/// 2016 blk

#pragma once

#include <vector>
#include "entity_header.h"
#include "Quaternion.h"
#include "matrix.h"

class GameEntity;

///	BaseComponent
///
/// - Exists as the end point for children calling CollectAncestorTypeInfo() up their hierarchy
/// - Each child has a static m_TypeInfo which contains their own typeInfo as well as their ancestors'
/// - Should never be used as a base class pointer.  Use Component* instead
class BaseComponent {
public:
	virtual	 ~BaseComponent() = 0 { }

	// Hack: use a void * instead of TypeInfoClass to work around mismatched type compile warning when passing in a type declared in the game project (as opposed to the
	// engine project).
	virtual	bool IsA(const void* const type) const { return false; }

protected:
	virtual void CollectAncestorTypeInfo_Internal(std::vector<class TypeInfoClass*>& collection) { }
};

/// Component
/// 
/// Derived classes should provide default init values in their constructors and do actual initializations in their Initialize() function
/// A derived class' Initialize() function does NOT need to call their parent's Initialize()
class Component : public BaseComponent {
	friend class Entity;

	BLK_DECLARE_COMPONENT(Component, BaseComponent);

public:
	virtual ~Component() { enable_internal(false); }

	virtual void Enable(const bool setEnabled) { }
	bool IsEnabled() const { return m_IsEnabled; }

	// Called during level load after the owning entity is fully loaded
	virtual void post_load() { }

	virtual void editor_change(const std::string& propertyName) { }

	virtual void render_sync() { }

	Entity* GetOwner() const { return m_pOwner; }

	void MarkAsDirty() { m_bIsDirty = true; }

	void SetOwner(Entity* const pGameEntity);

	// TODO: This is very hacky
	void SetOwningComponent(Component* const pOwningComponent) { m_pOwningComponent = pOwningComponent; }

protected:
	virtual void enable_internal(const bool bIsEnabled) { }
	virtual void update_internal(const float DeltaTimeSeconds) { }
	virtual void LifeTimeExpired() { }

	Component* GetOwningComponent() const { return m_pOwningComponent; }
	bool IsDirty() const { return m_bIsDirty; }

	bool m_bIsDirty;
	bool m_IsEnabled;

private:
	Entity* m_pOwner;
	Component* m_pOwningComponent;
};

/// GameComponent
class GameComponent : public Component {
	BLK_DECLARE_COMPONENT(GameComponent, Component);

public:
	virtual void Enable(const bool setEnabled) override;
	virtual void editor_change(const std::string& propertyName);

	void Update(const float DeltaTimeSeconds);

	GameEntity* GetOwner() const { return (GameEntity*)Super::GetOwner(); }
	String owner_name() const;
	Vec3 owner_position() const;
	Vec3 owner_scale() const;
	Quat4 owner_rotation() const;

	template<typename T>
	T* component() const {
		T* component = (T*)GetOwner()->GetComponentByType(T::GetType());
		return component;
	}

	void SetOwnerPosition(const Vec3& position);
	void SetOwnerRotation(const Quat4& rotation);

	float GetStartingLifeTime() const { return m_StartingLifeTime; }
	float GetLifeTimeRemaining() const { return m_LifeTimeRemaining; }

private:
	float m_StartingLifeTime;
	float m_LifeTimeRemaining;
};

/// TransformComponent
///
/// - Every game entity will have a TransformComponent as its first component to hold the entity's position/Rotation/scale
/// - Some components will be derived from TransformComponent to represent the component's local position/Rotation/scale
class TransformComponent : public GameComponent {
	BLK_DECLARE_COMPONENT(TransformComponent, GameComponent);

public:
	void set_name(const std::string name) { m_name = name; }
	void set_position(const Vec3& position) { m_position = position; }
	void set_scale(const Vec3& scale) { m_scale = scale; }
	void set_rotation(const Quat4& rotation) { m_rotation = rotation; }

	const String& name() const { return m_name; }
	const Vec3 position() const;
	const Vec3 scale() const;
	const Quat4 rotation() const;

protected:
	String m_name;
	Vec3 m_position;
	Vec3 m_scale;
	Quat4 m_rotation;
};

/// GameLogicComponent
///
/// This is a component for running game logic (AI, Player, etc).  
/// It's added to the end of a GameEntity's component list so that it will be run last
class GameLogicComponent : public GameComponent {
	BLK_DECLARE_COMPONENT(GameLogicComponent, GameComponent);

protected:
	virtual void update_internal(const float DeltaTime) override;

private:
	int	m_DummyTemp;	// Hack: TypeInfoHierarchyIterator currently requires at least one element in a component
};

/// DamageComponent 
class DamageComponent : public GameLogicComponent {
	BLK_DECLARE_COMPONENT(DamageComponent, GameLogicComponent);

public:
	float GetMinDamage() const { return m_MinDamage; }
	float GetMaxDamage() const { return m_MaxDamage; }

private:
	float m_MinDamage;
	float m_MaxDamage;
};

/// ActorComponent 
class ActorComponent : public GameLogicComponent {
	BLK_DECLARE_COMPONENT(ActorComponent, GameLogicComponent);

public:
	virtual void take_damage(const class DamageComponent* const damageComponent, const GameLogicComponent* const attackerComponent);

	float GetHealth() const { return m_CurrentHealth; }
	float GetMaxHealth() const { return m_MaxHealth; }

	bool sDead() const { return m_CurrentHealth <= 0.0f; }

protected:
	virtual void enable_internal(const bool bIsEnabled) override;

	float m_MaxHealth;
	float m_CurrentHealth;
};

/// DeleteEntityComponent
class DeleteEntityComponent : public GameComponent {
	BLK_DECLARE_COMPONENT(DeleteEntityComponent, GameComponent);
protected:
	virtual void LifeTimeExpired();

private:
	float m_Dummy;
};


/// PlayerStartComponent
class PlayerStartComponent : public GameComponent {
	BLK_DECLARE_COMPONENT(PlayerStartComponent, GameComponent);

	int m_DummyVar;
};

/// AnimEvent
class AnimEvent : public GameComponent {
	BLK_DECLARE_COMPONENT(AnimEvent, GameComponent);

public:
	const String GetEventName() const { return m_EventName; }
	float GetEventTime() const { return m_EventTime; }
	float GetEventValue() const { return m_EventValue; }

	static float Evaluate(const std::vector<AnimEvent>& eventList, const float t);

private:
	String m_EventName;
	float m_EventValue;
	float m_EventTime;
};

/// AnimEventInfo_t
struct AnimEventInfo_t {
	AnimEventInfo_t(const AnimEvent& animEvent, const Component* const pOwnerComponent) :
		m_AnimEvent(animEvent),
		m_pComponent(pOwnerComponent) { }

	const AnimEvent& m_AnimEvent;
	const Component* m_pComponent;
};

/// IAnimEventListener
class IAnimEventListener abstract {
public:
	virtual void OnAnimEvent(const AnimEventInfo_t& animEvent) = 0;
};

/// VectorAnimEvent
class VectorAnimEvent : public GameComponent {
	BLK_DECLARE_COMPONENT(VectorAnimEvent, GameComponent);

public:
	const String GetEventName() const { return m_EventName; }
	float GetEventTime() const { return m_EventTime; }
	Vec4 GetEventValue() const { return m_EventValue; }

	static Vec4	Evaluate(const std::vector<VectorAnimEvent>& eventList, const float t);

private:
	String m_EventName;
	Vec4 m_EventValue;
	float m_EventTime;
};

/// EditorGlobalSettingsComponent
class EditorGlobalSettingsComponent : public GameComponent {
	BLK_DECLARE_COMPONENT(EditorGlobalSettingsComponent, GameComponent);

public:
	int	m_CameraSpeedIdx;
};

///  EditorLevelSettingsComponent
class EditorLevelSettingsComponent : public GameComponent {
	BLK_DECLARE_COMPONENT(EditorLevelSettingsComponent, GameComponent);

public:
	Vec3 m_CameraPosition;
	Quat4 m_CameraRotation;
};

/// IStateMachine
template<typename StateEnum>
class StateMachineNode abstract {
public:

	StateMachineNode() :
		m_RequestedState((StateEnum)0),
		m_StateStartTime(-1.0f),
		m_bHasStateChangeRequest(false) { }

	void BeginState(const StateEnum previousState) {
		m_StateStartTime = g_GlobalTimer.TimeElapsedSeconds();
		BeginState_Internal(previousState);
	}

	void UpdateState() {
		UpdateState_Internal();
	}

	void EndState(const StateEnum nextState) {
		EndState_Internal(nextState);
	}

	float GetTimeSinceStateBegan() const {
		return g_GlobalTimer.TimeElapsedSeconds() - m_StateStartTime;
	}

	bool HasStateChangeRequest() const {
		return m_bHasStateChangeRequest;
	}

	StateEnum GetAndClearRequestedStateChange() {
		m_bHasStateChangeRequest = false;
		return m_RequestedState;
	}

protected:
	void RequestStateChange(const StateEnum requestedState) { m_bHasStateChangeRequest = true, m_RequestedState = requestedState; }

private:
	virtual void BeginState_Internal(const StateEnum previousState) { }
	virtual void UpdateState_Internal() { }
	virtual void EndState_Internal(const StateEnum nextState) { }

	StateEnum m_RequestedState;

	float m_StateStartTime;
	bool m_bHasStateChangeRequest;
};

template<typename StateClass, typename StateEnum>
class IStateMachine abstract {
public:
	IStateMachine() : m_CurrentState(StateEnum::NumStates) {
		ZeroMemory(m_States, sizeof(m_States));
		m_CurrentState = (StateEnum)StateEnum::NumStates;
		m_PreviousState = (StateEnum)StateEnum::NumStates;
	}

	virtual ~IStateMachine() {
		ShutdownStateMachine();

		for (int i = 0; i < StateEnum::NumStates; i++) {
			delete m_States[i];
		}
	}

	StateEnum GetPreviousState() const {
		return (StateEnum)m_PreviousState;
	}

	virtual void UpdateStateMachine() {
		// This condition is valid if state hasn't been set yet
		if (m_CurrentState >= StateEnum::NumStates) {
			return;
		}

		bool stateChanged = false;
		if (m_States[m_CurrentState]->HasStateChangeRequest()) {
			const StateEnum requestedState = m_States[m_CurrentState]->GetAndClearRequestedStateChange();
			if (requestedState != m_CurrentState) {
				m_States[m_CurrentState]->EndState(requestedState);

				m_PreviousState = m_CurrentState;
				m_CurrentState = requestedState;
				m_States[m_CurrentState]->BeginState(m_PreviousState);
				stateChanged = true;

				StateChangeCB(m_PreviousState, m_CurrentState);
			}
		}

		if (stateChanged == false) {
			m_States[m_CurrentState]->UpdateState();
		}
	}

	void InitializeStateMachine(StateClass* const stateNodes[StateEnum::NumStates]) {
		for (int i = 0; i < StateEnum::NumStates; i++) {
			delete m_States[i];
			m_States[i] = stateNodes[i];

			blk::error_check(stateNodes[i] != nullptr && m_States[i] != nullptr, "IStateMachine::InitializeStates() - NULL state.  Please call InitializeStates with proper values");
		}

		InitializeStateMachine_Internal();
	}

	void ShutdownStateMachine() {
		ShutdownStateMachine_Internal();
	}

	void RequestStateChange(const StateEnum newState) {
		if (newState < (StateEnum)(0) || newState >= StateEnum::NumStates) {
			blk::error("IStateMachine::RequestStateChange() - Invalid State requested");
		}

		if (newState == m_CurrentState) {
			return;
		}

		if (m_CurrentState != StateEnum::NumStates) {
			m_States[m_CurrentState]->EndState(newState);
		}

		blk::error_check(m_States[newState] != nullptr, "IStateMachine::RequestStateChange() - NULL state.  Please call InitializeStates with proper values");

		m_PreviousState = m_CurrentState;
		m_CurrentState = newState;
		m_States[m_CurrentState]->BeginState(m_PreviousState);

		StateChangeCB(m_PreviousState, m_CurrentState);
	}

	StateEnum GetCurrentState() const { return m_CurrentState; }

	bool IsInitialized() const { return m_CurrentState != StateEnum::NumStates; }

protected:
	virtual void StateChangeCB(const StateEnum previousState, const StateEnum nextState) { }

	virtual void InitializeStateMachine_Internal() { }
	virtual void ShutdownStateMachine_Internal() { }

	StateClass* m_States[StateEnum::NumStates];
	StateEnum m_CurrentState;
	StateEnum m_PreviousState;
};

/// ISingleton
template <typename T>
class ISingleton {
public:
	ISingleton() {
		blk::error_check(m_pInstance == nullptr, "Multiple instances of an ISingleton");
	}

	static T* Get() {
		if (m_pInstance == nullptr) {
			m_pInstance = new T();
		}

		return m_pInstance;
	}

	static void DeleteSingleton() {
		delete m_pInstance;
		m_pInstance = nullptr;
	}

private:
	inline static T* m_pInstance = nullptr;
};
