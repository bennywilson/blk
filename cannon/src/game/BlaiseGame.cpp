/// BlaiseGame.cpp
///
/// 2019-2025 blk 1.0
#include "game.h"
#include "type_info.h"
#include "intersection_tests.h"
#include "level_component.h"
#include "BlaiseGame.h"
#include "UI/CannonUI.h"
#include <directxpackedvector.h>
#include "kbEditorEntity.h"

BlaiseGame* g_pBlaiseGame = nullptr;

/// BlaiseGame::BlaiseGame
BlaiseGame::BlaiseGame() :
	m_pMainCamera(nullptr),
	m_pPlayerComp(nullptr) {

	m_Camera.m_position.set(0.0f, 2600.0f, 0.0f);

	blk::error_check(g_pBlaiseGame == nullptr, "BlaiseGame::BlaiseGame() - g_pBlaiseGame is not nullptr");
	g_pBlaiseGame = this;
}

/// BlaiseGame::~BlaiseGame
BlaiseGame::~BlaiseGame() {

	blk::error_check(g_pBlaiseGame != nullptr, "BlaiseGame::~BlaiseGame() - g_pBlaiseGame is nullptr");
	g_pBlaiseGame = nullptr;
}

/// BlaiseGame::play_internal
void BlaiseGame::play_internal() {

}

/// BlaiseGame::init_internal
void BlaiseGame::init_internal() {
	m_GameStartTimer.Reset();

	CannonBallGameSettingsComponent* const pGameSettings = CannonBallGameSettingsComponent::Get();

	GetSoundManager().SetMasterVolume(pGameSettings->m_Volume / 100.0f);

	kbShaderParamOverrides_t shaderParam;

	float brightness = (pGameSettings->m_Brightness / 100.0f);
	brightness = (brightness * 0.5f) + 0.5f;

	shaderParam.SetVec4("globalTint", Vec4(0.0f, 0.0f, 0.0f, 1.0f - brightness));
	//g_pRenderer->SetGlobalShaderParam(shaderParam);

//	const float LOD = (float)pGameSettings->m_VisualQuality / 100.0f;
	//TerrainComponent::SetTerrainLOD(LOD);
}

/// BlaiseGame::stop_internal
void BlaiseGame::stop_internal() {
	m_pLocalPlayer = nullptr;
}

/// BlaiseGame::level_loaded_internal
void BlaiseGame::level_loaded_internal() {
	m_pMainCamera = nullptr;

	int cameraIdx = -1;
	const std::vector<GameEntity*>& GameEnts = GetGameEntities();
	for (int i = 0; i < GameEnts.size(); i++) {
		GameEntity* const pCurEnt = GameEnts[i];

		if (m_pMainCamera == nullptr) {
			m_pMainCamera = pCurEnt->component<CannonCameraComponent>();
			if (m_pMainCamera != nullptr) {
				cameraIdx = i;
			}
		}
	}

	if (cameraIdx >= 0) {
		swap_entities_by_idx(cameraIdx, GameEnts.size() - 1);
	}

	blk::warn_check(m_pMainCamera != nullptr, "BlaiseGame::LevelLoaded_Internal() - No camera found.");
	blk::warn_check(m_pPlayerComp != nullptr, "BlaiseGame::LevelLoaded_Internal() - No player found.");
}

/// BlaiseGame::preupdate_internal
void BlaiseGame::preupdate_internal() {
	const float frameDT = GetFrameDT();
	if (is_console_active() == false) {
		ProcessInput(frameDT);
	}
}

/// BlaiseGame::postupdate_internal
void BlaiseGame::postupdate_internal() {
	// Update renderer cam
	/*if (m_pMainCamera != nullptr && m_pMainCamera->GetOwner() != nullptr) {
		g_pD3D11Renderer->SetRenderViewTransform(nullptr, m_pMainCamera->GetOwner()->position(), m_pMainCamera->GetOwner()->rotation());
	}*/
}

/// BlaiseGame::add_entity_internal
void BlaiseGame::add_entity_internal(GameEntity* const pEntity) {
	if (pEntity == nullptr) {
		blk::warn("BlaiseGame::AddGameEntity_Internal() - nullptr Entity");
		return;
	}

}

/// BlaiseGame::remove_entity_internal
void BlaiseGame::remove_entity_internal(GameEntity* const pEntity) {
	if (pEntity == nullptr) {
		blk::warn("BlaiseGame::RemoveGameEntity_Internal() - nullptr Entity");
		return;
	}

	if (pEntity == m_pLocalPlayer) {
		m_pLocalPlayer = nullptr;
		m_pPlayerComp = nullptr;
	}
}

/// BlaiseGame::CreatePlayer
GameEntity* BlaiseGame::CreatePlayer(const int netId, const kbGUID& prefabGUID, const Vec3& DesiredLocation) {

	return nullptr;
}

/// BlaiseGame::ProcessInput
void BlaiseGame::ProcessInput(const float DT) {

	static bool bCursorHidden = false;
	static bool bWindowIsSelected = true;
	static bool bFirstRun = true;

	if (bFirstRun) {
		ShowCursor(false);
		bFirstRun = false;
	}
}

/// BlaiseGame::RenderHookCallBack
static float g_TimeMultiplier = 0.95f / 0.016f;

/// BlaiseGame::HackEditorInit
void BlaiseGame::HackEditorInit(HWND hwnd, std::vector<class kbEditorEntity*>& editorEntities) {

	for (int i = 0; i < editorEntities.size(); i++) {
		GameEntity* const pCurEnt = editorEntities[i]->GetGameEntity();

		if (m_pMainCamera == nullptr) {
			m_pMainCamera = (CannonCameraComponent*)pCurEnt->GetComponentByType(CannonCameraComponent::GetType());
		}
	}

	m_InputManager.Init(hwnd);
}

/// BlaiseGame::HackEditorUpdate
void BlaiseGame::HackEditorUpdate(const float DT, kbCamera* const pEditorCam) {

	m_InputManager.Update(DT);

	/*if ( m_pPlayerComp != nullptr ) {
		m_pPlayerComp->HandleInput( m_InputManager.GetInput(), DT );
	}*/

	if (m_pMainCamera != nullptr && pEditorCam != nullptr) {
		pEditorCam->m_position = m_pMainCamera->owner_position();
		pEditorCam->m_rotation = pEditorCam->m_rotationTarget = m_pMainCamera->owner_rotation();
	}
}

/// BlaiseGame::HackEditorShutdown
void BlaiseGame::HackEditorShutdown() {
	m_pPlayerComp = nullptr;
	m_pMainCamera = nullptr;
}

/// CannonLevelComponent::Constructor
void CannonLevelComponent::Constructor() {
	m_Dummy2 = -1;
}

/*
 *	CannonFogComponent::Constructor
 */
void CannonFogComponent::Constructor() {
	m_shader = nullptr;
	m_FogStartDist = 300;
	m_FogEndDist = 3000;
	m_FogClamp = 1.0f;
	m_FogColor = kbColor::white;
}

/// CannonFogComponent::enable_internal
void CannonFogComponent::enable_internal(const bool bEnable) {
	Super::enable_internal(bEnable);
}

/// CannonActorComponent::Constructor
void CannonCameraComponent::Constructor() {
	// Editor
	m_NearPlane = 1.0f;
	m_FarPlane = 20000.0f;		// TODO - NEAR/FAR PLANE - Tie into renderer properly
	m_positionOffset.set(0.0f, 0.0f, 0.0f);
	m_LookAtOffset.set(0.0f, 0.0f, 0.0f);

	m_MoveMode = MoveMode_Follow;
	m_pTarget = nullptr;

	// Game
	m_SwitchTargetBlendSpeed = 1.0f;
	m_SwitchTargetCurT = 1.0f;
	m_SwitchTargetStartPos.set(0.0f, 0.0f, 0.0f);

	m_SwitchPosOffsetBlendSpeed = 1.0f;
	m_SwitchPosOffsetCurT = 1.0f;
	m_PosOffsetTarget.set(0.0f, 0.0f, 0.0f);

	m_SwitchLookAtOffsetBlendSpeed = 1.0f;
	m_SwitchLookAtOffsetCurT = 1.0f;
	m_LookAtOffsetTarget.set(0.0f, 0.0f, 0.0f);

	m_CameraShakeStartTime = -1.0f;
	m_CameraShakeStartingOffset.set(0.0f, 0.0f);
	m_CameraShakeDuration = 0.0f;
	m_CameraShakeAmplitude.set(0.0f, 0.0f);
	m_CameraShakeFrequency.set(0.0f, 0.0f);
}

/// CannonActorComponent::enable_internal
void CannonCameraComponent::enable_internal(const bool bEnable) {
	Super::enable_internal(bEnable);

	m_pTarget = nullptr;
}

/// CannonActorComponent::SetTarget
void CannonCameraComponent::SetTarget(const GameEntity* const pTarget, const float blendRate) {
	m_SwitchTargetBlendSpeed = blendRate;

	if (m_SwitchTargetBlendSpeed > 0) {
		if (m_pTarget != nullptr) {
			m_SwitchTargetStartPos = m_pTarget->position();
			m_SwitchTargetCurT = 0.0f;

		} else {
			m_SwitchTargetBlendSpeed = -1.0f;
		}
	}

	m_pTarget = pTarget;
}

/// CannonActorComponent::SetPositionOffset
void CannonCameraComponent::SetPositionOffset(const Vec3& posOffset, const float blendRate) {

	if (blendRate < 0.0f) {
		m_SwitchPosOffsetCurT = 1.0f;
		m_positionOffset = posOffset;
	} else {
		m_SwitchPosOffsetCurT = 0.0f;
		m_SwitchPosOffsetBlendSpeed = blendRate;
		m_PosOffsetTarget = posOffset;
	}
}

/// CannonActorComponent::SetLookAtOffset
void CannonCameraComponent::SetLookAtOffset(const Vec3& lookAtOffset, const float blendRate) {

	if (blendRate < 0.0f) {
		m_SwitchLookAtOffsetCurT = 1.0f;
		m_LookAtOffset = lookAtOffset;
	} else {
		m_SwitchLookAtOffsetBlendSpeed = blendRate;
		m_SwitchLookAtOffsetCurT = 0.0f;
		m_LookAtOffsetTarget = lookAtOffset;
	}
}

/// CannonActorComponent::StartCameraShake
void CannonCameraComponent::StartCameraShake(const CannonCameraShakeComponent* const pCameraShakeComponent) {

	m_CameraShakeStartTime = g_GlobalTimer.TimeElapsedSeconds();
	m_CameraShakeStartingOffset = Vec2Rand(-m_CameraShakeAmplitude, m_CameraShakeAmplitude);
	m_CameraShakeDuration = pCameraShakeComponent->GetDuration();
	m_CameraShakeAmplitude = pCameraShakeComponent->GetAmplitude();
	m_CameraShakeFrequency = pCameraShakeComponent->GetFrequency();
}

/// CannonActorComponent::update_internal
void CannonCameraComponent::update_internal(const float DeltaTime) {
	Super::update_internal(DeltaTime);

	Vec2 camShakeOffset(0.0f, 0.0f);
	if (m_CameraShakeStartTime > 0.0f) {
		const float elapsedTime = g_GlobalTimer.TimeElapsedSeconds() - m_CameraShakeStartTime;
		if (elapsedTime > m_CameraShakeDuration) {
			m_CameraShakeStartTime = -1.0f;
		} else {
			const float fallOff = 1.0f - kbClamp((elapsedTime / m_CameraShakeDuration), 0.0f, 1.0f);
			camShakeOffset.x = sin(m_CameraShakeStartingOffset.x + (g_GlobalTimer.TimeElapsedSeconds() * m_CameraShakeFrequency.x)) * m_CameraShakeAmplitude.x * fallOff;
			camShakeOffset.y = sin(m_CameraShakeStartingOffset.y + (g_GlobalTimer.TimeElapsedSeconds() * m_CameraShakeFrequency.y)) * m_CameraShakeAmplitude.y * fallOff;
		}
	}

	switch (m_MoveMode) {
		case MoveMode_None: {
		}
						  break;

		case MoveMode_Follow: {
			if (m_pTarget != nullptr) {

				// Target blend to
				Vec3 targetPosition = m_pTarget->position();
				if (m_SwitchTargetCurT < 1.0f) {
					m_SwitchTargetCurT += m_SwitchTargetBlendSpeed * g_pGame->GetFrameDT();
					targetPosition = kbLerp(m_SwitchTargetStartPos, targetPosition, kbSaturate(m_SwitchTargetCurT));
				}

				// LookAt offset blend
				Vec3 lookAtOffset = m_LookAtOffset;
				if (m_SwitchLookAtOffsetCurT < 1.0f) {
					m_SwitchLookAtOffsetCurT += m_SwitchLookAtOffsetBlendSpeed * g_pGame->GetFrameDT();
					lookAtOffset = kbLerp(m_LookAtOffset, m_LookAtOffsetTarget, kbSaturate(m_SwitchLookAtOffsetCurT));
					if (m_SwitchLookAtOffsetCurT > 1.0f) {
						m_LookAtOffset = m_LookAtOffsetTarget;
					}
				}

				// Position offset blend
				Vec3 positionOffset = m_positionOffset;
				if (m_SwitchPosOffsetCurT < 1.0f) {
					m_SwitchPosOffsetCurT += m_SwitchPosOffsetBlendSpeed * g_pGame->GetFrameDT();
					positionOffset = kbLerp(m_positionOffset, m_PosOffsetTarget, kbSaturate(m_SwitchPosOffsetCurT));
					if (m_SwitchPosOffsetCurT >= 1.0f) {
						m_positionOffset = m_PosOffsetTarget;
					}
				}

				GetOwner()->set_position(targetPosition + positionOffset);

				Mat4 cameraDestRot;
				//cameraDestRot./look_at(GetOwner()->position(), targetPosition + lookAtOffset, Vec3::up);
				cameraDestRot.inverse_fast();
				GetOwner()->set_rotation(Quat4::from_mat4(cameraDestRot));

				const Vec3 cameraDestPos = targetPosition + positionOffset;
				GetOwner()->set_position(cameraDestPos + cameraDestRot[0].ToVec3() * camShakeOffset.x + cameraDestRot[1].ToVec3() * camShakeOffset.y);
				GetOwner()->set_position(cameraDestPos + cameraDestRot[0].ToVec3() * camShakeOffset.x + cameraDestRot[1].ToVec3() * camShakeOffset.y);
			}
		}
							break;
	}
}

/// CannonCameraShakeComponent::Constructor
void CannonCameraShakeComponent::Constructor() {
	m_Duration = 1.0f;
	m_AmplitudeX = 0.025f;
	m_AmplitudeY = 0.019f;

	m_FrequencyX = 15.0f;
	m_FrequencyY = 10.0f;

	m_ActivationDelaySeconds = 0.0f;
	m_bActivateOnEnable = false;

	m_ShakeStartTime = -1.0f;
}

/// CannonCameraShakeComponent::enable_internal
void CannonCameraShakeComponent::enable_internal(const bool bEnable) {
	Super::enable_internal(bEnable);

	if (bEnable) {
		m_ShakeStartTime = g_GlobalTimer.TimeElapsedSeconds() + m_ActivationDelaySeconds;
	}
}

/// CannonCameraShakeComponent::update_internal
void CannonCameraShakeComponent::update_internal(const float DeltaTime) {
	Super::update_internal(DeltaTime);
	\
	if (m_bActivateOnEnable && g_GlobalTimer.TimeElapsedSeconds() > m_ShakeStartTime) {
		// Disable so that this component doesn't prevent it's owning entity to linger past it's life time
		Enable(false);

		CannonCameraComponent* const pCam = (CannonCameraComponent*)g_pBlaiseGame->GetMainCamera();
		if (pCam != nullptr) {
			pCam->StartCameraShake(this);
		}
	}
}