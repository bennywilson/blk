/// BlaiseGame.h
///
/// 2025 blk1.0

#pragma once

#include "game.h"

class CannonActorComponent;
class LevelComponent;

/// ECameraMoveMode
enum ECameraMoveMode {
	MoveMode_None,
	MoveMode_Follow,
};

/// CannonCameraShakeComponent
class CannonCameraShakeComponent : public ActorComponent {

	BLK_DECLARE_COMPONENT(CannonCameraShakeComponent, ActorComponent);

public:
	float GetDuration() const { return m_Duration; }
	Vec2 GetAmplitude() const { return Vec2(m_AmplitudeX, m_AmplitudeY); }
	Vec2 GetFrequency() const { return Vec2(m_FrequencyX, m_FrequencyY); }

	void enable_internal(const bool bEnable) override;
	void update_internal(const float deltaTime) override;

	BLK_PROPERTY()
	float m_Duration;

	BLK_PROPERTY()
	float m_AmplitudeX;

	BLK_PROPERTY()
	float m_AmplitudeY;

	BLK_PROPERTY()
	float m_FrequencyX;

	BLK_PROPERTY()
	float m_FrequencyY;

private:
	BLK_PROPERTY()
	float m_ActivationDelaySeconds;
	float m_ShakeStartTime;

	BLK_PROPERTY()
	bool m_bActivateOnEnable;
};

/// CannonCameraComponent
class CannonCameraComponent : public ActorComponent {

	BLK_DECLARE_COMPONENT(CannonCameraComponent, ActorComponent);

	//---------------------------------------------------------------------------------------------------
public:
	void StartCameraShake(const CannonCameraShakeComponent* const pCameraShakeComponent);

	void SetTarget(const GameEntity* const pTarget, const float blendRate);
	void SetPositionOffset(const Vec3& posOffset, const float blendRate);
	void SetLookAtOffset(const Vec3& lookAtOffset, const float blendRate);

protected:
	virtual void enable_internal(const bool bEnable) override;
	virtual void update_internal(const float DeltaTime) override;

private:
	// Editor
	BLK_PROPERTY()
	float m_NearPlane;

	BLK_PROPERTY()
	float m_FarPlane;

	BLK_PROPERTY()
	Vec3 m_positionOffset;

	BLK_PROPERTY()
	Vec3 m_LookAtOffset;

	// Game
	BLK_PROPERTY()
	ECameraMoveMode m_MoveMode;
	const GameEntity* m_pTarget;
	float m_SwitchTargetBlendSpeed;
	float m_SwitchTargetCurT;
	Vec3 m_SwitchTargetStartPos;

	float m_SwitchPosOffsetBlendSpeed;
	float m_SwitchPosOffsetCurT;
	Vec3 m_PosOffsetTarget;

	float m_SwitchLookAtOffsetBlendSpeed;
	float m_SwitchLookAtOffsetCurT;
	Vec3 m_LookAtOffsetTarget;

	float m_CameraShakeStartTime;
	Vec2 m_CameraShakeStartingOffset;
	float m_CameraShakeDuration;
	Vec2 m_CameraShakeAmplitude;
	Vec2 m_CameraShakeFrequency;
};

/// DealAttackInfo_t
template<typename T>
struct DealAttackInfo_t {
	CannonActorComponent* m_pAttacker = nullptr;
	float m_BaseDamage = 1.0f;
	float m_Radius = 0.0f;
	T m_AttackType = (T)0;
};

/// AttackHitInfo_t
struct AttackHitInfo_t {
	GameComponent* m_pHitComponent = nullptr;
	bool m_bHit = false;
};

/// CannonLevelComponent
class CannonLevelComponent : public LevelComponent {
public:
	BLK_DECLARE_COMPONENT(CannonLevelComponent, LevelComponent);

private:
	BLK_PROPERTY()
	int m_Dummy2;
};

/// BlaiseGame
class BlaiseGame : public Game {
public:
	BlaiseGame();
	virtual ~BlaiseGame();

	CannonCameraComponent* GetMainCamera() const { return m_pMainCamera; }

	CannonActorComponent* GetPlayer() const { return m_pPlayerComp; }

protected:

	virtual void init_internal() override;
	virtual void play_internal() override;
	virtual void stop_internal() override;
	virtual void level_loaded_internal() override;

	virtual void add_entity_internal(GameEntity* const pEntity) override;
	virtual void remove_entity_internal(GameEntity* const pEntity) override;


	virtual void preupdate_internal() override;
	virtual void postupdate_internal() override;

	virtual GameEntity* CreatePlayer(const int netId, const Guid& prefabGUID, const Vec3& desiredLocation) override;

	virtual void HackEditorInit(HWND hwnd, std::vector<class EditorEntity*>& editorEntities) override;
	virtual void HackEditorUpdate(const float DT, Camera* const pCamera) override;
	virtual void HackEditorShutdown() override;

protected:
	Camera m_Camera;

	Timer m_GameStartTimer;

	CannonCameraComponent* m_pMainCamera;
	CannonActorComponent* m_pPlayerComp;

private:

	void ProcessInput(const float deltaTimeSec);
};

/// CannonFogComponent
class CannonFogComponent : public GameComponent {
	BLK_DECLARE_COMPONENT(CannonFogComponent, GameComponent);

protected:
	virtual void enable_internal(const bool bEnable) override;

private:
	BLK_PROPERTY()
	Shader* m_shader;

	BLK_PROPERTY()
	float m_FogStartDist;

	BLK_PROPERTY()
	float m_FogEndDist;

	BLK_PROPERTY()
	float m_FogClamp;

	BLK_PROPERTY()
	Color m_FogColor;
};


extern BlaiseGame* g_pBlaiseGame;

inline bool WasAttackJustPressed(const Input_t* const pInput = nullptr) {
	const Input_t& input = (pInput == nullptr) ? (g_pInputManager->get_input()) : (*pInput);
	return input.WasKeyJustPressed('K') || input.GamepadButtonStates[12].m_Action == Input_t::KA_JustPressed;
}

inline bool WasSpecialAttackPressed(const Input_t* const pInput = nullptr) {
	const Input_t& input = (pInput == nullptr) ? (g_pInputManager->get_input()) : (*pInput);
	return input.WasKeyJustPressed('J') || input.LeftTrigger > 0.1f || input.RightTrigger > 0.1f;
}

inline bool WasStartButtonPressed(const Input_t* const pInput = nullptr) {
	const Input_t& input = (pInput == nullptr) ? (g_pInputManager->get_input()) : (*pInput);
	return input.GamepadButtonStates[4].m_Action == Input_t::KA_JustPressed;
}

inline bool WasBackButtonPressed(const Input_t* const pInput = nullptr) {
	const Input_t& input = (pInput == nullptr) ? (g_pInputManager->get_input()) : (*pInput);
	return input.WasNonCharKeyJustPressed(Input_t::Escape) || input.GamepadButtonStates[5].m_Action == Input_t::KA_JustPressed;
}

inline bool WasConfirmationButtonPressed(const Input_t* const pInput = nullptr) {
	if (WasStartButtonPressed(pInput) || WasAttackJustPressed(pInput)) {
		return true;
	}

	const Input_t& input = (pInput == nullptr) ? (g_pInputManager->get_input()) : (*pInput);
	if (input.WasNonCharKeyJustPressed(Input_t::Return)) {
		return true;
	}

	return false;
}

inline Vec2 GetLeftStick(const Input_t* const pInput = nullptr) {
	const Input_t& input = (pInput == nullptr) ? (g_pInputManager->get_input()) : (*pInput);
	Vec2 retLeftStick = Vec2::zero;

	if (input.IsKeyPressedOrDown('A')) {
		retLeftStick.x = -1.0f;
	} else if (input.IsKeyPressedOrDown('D')) {
		retLeftStick.x = 1.0f;
	} else {
		retLeftStick.x = input.m_LeftStick.x;
	}

	if (input.IsKeyPressedOrDown('W')) {
		retLeftStick.y = 1.0f;
	} else if (input.IsKeyPressedOrDown('S')) {
		retLeftStick.y = -1.0f;
	} else {
		retLeftStick.y = input.m_LeftStick.y;
	}

	return retLeftStick;
}

inline Vec2 GetPrevLeftStick(const Input_t* const pInput = nullptr) {
	const Input_t& input = (pInput == nullptr) ? (g_pInputManager->get_input()) : (*pInput);
	Vec2 leftStick = Vec2::zero;

	if (input.IsKeyPressedOrDown('A')) {
		leftStick.x = -1.0f;
	} else if (input.IsKeyPressedOrDown('D')) {
		leftStick.x = 1.0f;
	} else {
		leftStick.x = input.m_PrevLeftStick.x;
	}

	if (input.IsKeyPressedOrDown('W')) {
		leftStick.y = 1.0f;
	} else if (input.IsKeyPressedOrDown('S')) {
		leftStick.y = -1.0f;
	} else {
		leftStick.y = input.m_PrevLeftStick.y;
	}

	return leftStick;
}
