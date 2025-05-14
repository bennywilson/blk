//===================================================================================================
// KungFuLevelComponent.cpp
//
// 2019-2020 kbEngine 2.0
//===================================================================================================
#include "game.h"
#include "BlaiseGame.h"
#include "UI/CannonUI.h"
#include "KungFuLevelComponent.h"
#include "kbEditor.h"
#include "kbEditorEntity.h"

kbConsoleVariable g_CullGrass("cullgrass", false, kbConsoleVariable::Console_Bool, "", "");


namespace KungFuGame {

	enum eSkipCheats {
		Skip_None,
		Skip_MainMenuAndIntro,
		Skip_ToEnd,
		Skip_NoEnemies,
	};

};

KungFuGame::eSkipCheats g_SkipCheat = KungFuGame::Skip_None;


/// KungFuGame_MainMenuState
class KungFuGame_MainMenuState : public KungFuGame_BaseState {

	//---------------------------------------------------------------------------------------------------
public:

	KungFuGame_MainMenuState(KungFuLevelComponent* const pLevelComponent) : KungFuGame_BaseState(pLevelComponent) { }

private:

	virtual void WidgetEventCB(kbUIWidgetComponent* const pWidget, const kbInput_t* pInput) override {

		if (pInput == nullptr || WasConfirmationButtonPressed(pInput) == false) {
			return;
		}

		const auto pMainMenu = pWidget->GetAs<CannonBallMainMenuComponent>();
		if (pMainMenu != nullptr) {
			if (pMainMenu->GetSelectedIndex() == 0) {
				RequestStateChange(KungFuGame::Intro);
			} else if (pMainMenu->GetSelectedIndex() == 1) {
				RequestStateChange(KungFuGame::Paused);
			} else {
				g_pBlaiseGame->RequestQuitGame();
			}
		}
	}

	virtual void UpdateState_Internal() override {

	}

	KungFuSheepComponent* m_pSheep = nullptr;
};

/// KungFuGame_IntroGameState
class KungFuGame_IntroGameState : public KungFuGame_BaseState {

	//---------------------------------------------------------------------------------------------------
public:
	KungFuGame_IntroGameState(KungFuLevelComponent* const pLevelComponent) : KungFuGame_BaseState(pLevelComponent) { }

private:
	virtual void BeginState_Internal(KungFuGame::eKungFuGame_State previousState) override {
		m_CurrentState = 0;
	}

	virtual void UpdateState_Internal() override { }

	virtual void EndState_Internal(KungFuGame::eKungFuGame_State nextState) override { }

	int m_CurrentState = 0;
};

/// KungFuGame_GameplayState
class KungFuGame_GameplayState : public KungFuGame_BaseState {

	//---------------------------------------------------------------------------------------------------
public:

	KungFuGame_GameplayState(KungFuLevelComponent* const pLevelComponent) : KungFuGame_BaseState(pLevelComponent) { }


private:

	virtual void BeginState_Internal(KungFuGame::eKungFuGame_State previousState) override {

		if (previousState == KungFuGame::Paused) {
			KungFuLevelComponent::Get()->SetPlayLevelMusic(1, false);
		} else if (previousState == KungFuGame::Intro) {
			m_NumSnolafsKilled = 0;
			m_GamePlayStartTime = g_GlobalTimer.TimeElapsedSeconds();
			m_bFirstUpdate = true;
		}
	}

	virtual void UpdateState_Internal() override {
	
	}

	float m_GamePlayStartTime;
	float m_LastSpawnTime = 0.0f;
	bool m_bFirstUpdate = false;
	int m_NumSnolafsKilled = 0;
};

/// KungFuGame_PausedState
class KungFuGame_PausedState : public KungFuGame_BaseState {

	//---------------------------------------------------------------------------------------------------
public:
	KungFuGame_PausedState(KungFuLevelComponent* const pLevelComponent) : KungFuGame_BaseState(pLevelComponent) {
		m_pPauseMenu = nullptr;
	}

private:

	virtual void BeginState_Internal(KungFuGame::eKungFuGame_State previousState) override {

		if (previousState != KungFuGame::MainMenu) {
			g_pGame->SetDeltaTimeScale(0.0f);
		}

		m_pPauseMenu = nullptr;
		for (int i = 0; i < g_pBlaiseGame->GetGameEntities().size(); i++) {

			GameEntity* const pEnt = g_pBlaiseGame->GetGameEntities()[i];
			m_pPauseMenu = pEnt->component<CannonBallPauseMenuUIComponent>();
			if (m_pPauseMenu != nullptr) {
				m_pPauseMenu->Enable(true);
				break;
			}
		}

		KungFuLevelComponent* const pLevelComp = g_pBlaiseGame->GetLevelComponent<KungFuLevelComponent>();
		pLevelComp->SetPlayLevelMusic(1, false);
	}

	virtual void EndState_Internal(KungFuGame::eKungFuGame_State nextState) override {
		g_pGame->SetDeltaTimeScale(1.0f);

		for (int i = 0; i < g_pBlaiseGame->GetGameEntities().size(); i++) {

			GameEntity* const pEnt = g_pBlaiseGame->GetGameEntities()[i];
			CannonBallPauseMenuUIComponent* const pPauseMenu = pEnt->component<CannonBallPauseMenuUIComponent>();
			if (pPauseMenu == nullptr) {
				continue;
			}

			pPauseMenu->Enable(false);
		}

		KungFuLevelComponent::Get()->SetPlayLevelMusic(1, true);

		if (nextState == KungFuGame::MainMenu && KungFuSheepDirector::Get()->GetPreviousState() == KungFuGame::Gameplay) {
			KungFuLevelComponent::Get()->RemoveSheep();
		}
	}

	virtual void WidgetEventCB(kbUIWidgetComponent* const pWidget, const kbInput_t* pInput) override {

		const auto pPauseMenu = KungFuSheepDirector::Get()->GetPauseMenu();
		blk::error_check(pPauseMenu != nullptr, "KungFuGame_PausedState::WidgetEventCB() - null pause menu component");

		if (WasConfirmationButtonPressed(pInput)) {
			if (pPauseMenu->GetSelectedWidgetIdx() == 0) {
				if (KungFuSheepDirector::Get()->GetPreviousState() == KungFuGame::MainMenu) {
					RequestStateChange(KungFuGame::MainMenu);
				} else {
					RequestStateChange(KungFuGame::Gameplay);
				}
			} else if (pPauseMenu->GetSelectedWidgetIdx() == 4) {
				RequestStateChange(KungFuGame::MainMenu);
			}
			return;
		}
	}

private:
	CannonBallPauseMenuUIComponent* m_pPauseMenu;
};

/// KungFuGame_PlayerDeadState
class KungFuGame_PlayerDeadState : public KungFuGame_BaseState {

	//---------------------------------------------------------------------------------------------------
public:
	KungFuGame_PlayerDeadState(KungFuLevelComponent* const pLevelComponent) : KungFuGame_BaseState(pLevelComponent) { }

private:

	virtual void BeginState_Internal(KungFuGame::eKungFuGame_State previousState) override {

	
	}

	virtual void UpdateState_Internal() override {

		if (g_GlobalTimer.TimeElapsedSeconds() > m_StateStartTime + 5.0f) {
			RequestStateChange(KungFuGame::MainMenu);
		}
	}

	virtual void EndState_Internal(KungFuGame::eKungFuGame_State nextState) override {

		KungFuLevelComponent::Get()->RemoveSheep();
	}

	float m_StateStartTime;
};


KungFuLevelComponent* KungFuLevelComponent::s_Inst = nullptr;

/// KungFuLevelComponent::Constructor
void KungFuLevelComponent::Constructor() {

	// Editor
	m_LevelLength = 100.0f;

	m_pBasePortraitTexture = nullptr;
	m_pHuggedPortraitTexture = nullptr;
	m_pDeadPortriatTexture = nullptr;

	// Runtime
	m_WaterDropletFXStartTime = -1.0f;
	m_LastWaterSplashSoundTime = 0.0f;

	m_pHealthBarUI = nullptr;
	m_pCannonBallUI = nullptr;

	m_EndSnolafs[0] = m_EndSnolafs[1] = nullptr;

	m_pSheep = nullptr;
	m_p3000Ton = nullptr;

	m_pFox = nullptr;
}

/// KungFuLevelComponent::enable_internal
void KungFuLevelComponent::enable_internal(const bool bEnable) {
	Super::enable_internal(bEnable);

	m_pHealthBarUI = nullptr;
	m_pCannonBallUI = nullptr;
	m_EndSnolafs[0] = m_EndSnolafs[1] = nullptr;

	if (bEnable) {

		blk::error_check(s_Inst == nullptr, "KungFuLevelComponent::enable_internal() - Multiple enabled instances of KungFuLevelComponent");
		s_Inst = this;

		if (g_UseEditor == false) {
			g_ResourceManager.get_package("./assets/Packages/fx.kbPkg");
			g_ResourceManager.get_package("./assets/Packages/Snolaf.kbPkg");
			g_ResourceManager.get_package("./assets/Packages/Sheep.kbPkg");
			g_ResourceManager.get_package("./assets/Packages/3000Ton.kbPkg");
		}

		if (m_WaterDropletScreenFX.GetEntity() != nullptr) {

			for (int i = 0; i < NumWaterSplashes; i++) {
				blk::error_check(m_WaterSplashFXInst[i].m_Entity.GetEntity() == nullptr, "KungFuLevelComponent::enable_internal() - Water Droplet Screen FX Instance already allocated");

				m_WaterSplashFXInst[i].m_Entity.SetEntity(g_pGame->CreateEntity(m_WaterDropletScreenFX.GetEntity()));
				StaticModelComponent* const pSM = m_WaterSplashFXInst[i].m_Entity.GetEntity()->component<StaticModelComponent>();
				pSM->Enable(false);
			}
		}

		for (int i = 0; i < KungFuGame::kSnolafPoolSize; i++) {
			GameEntity* const pSnolaf = g_pGame->CreateEntity(m_SnolafPrefab.GetEntity());
			ReturnSnolafToPool(pSnolaf->component<KungFuSnolafComponent>());
		}

	} else {

		if (s_Inst == this) {
			s_Inst = nullptr;
		}

		for (int i = 0; i < NumWaterSplashes; i++) {
			GameEntity* const pEnt = m_WaterSplashFXInst[i].m_Entity.GetEntity();
			if (pEnt != nullptr) {
				g_pGame->RemoveGameEntity(pEnt);
				m_WaterSplashFXInst[i].m_Entity.SetEntity(nullptr);
			}
		}
		if (g_pBlaiseGame->IsPlaying())
		{
			while (m_SnolafPool.size() > 0)
			{
				GameEntity* const pSnolaf = m_SnolafPool.back();
				m_SnolafPool.pop_back();
				g_pGame->RemoveGameEntity(pSnolaf);
			}
			m_SnolafPool.clear();
		}
		KungFuSheepDirector::DeleteSingleton();
	}
}

/// KungFuLevelComponent::update_internal
const Vec4 g_WaterDropletNormalFactorScroll[] = {
		Vec4(0.1000f, 0.1000f, 0.00000f, 0.01f),
		Vec4(0.1000f, 0.1000f, 0.00000f, 0.007f) };

const float g_WaterDropStartDelay[] = { 0.1f, 0.01f };

void KungFuLevelComponent::update_internal(const float DeltaTime) {
	Super::update_internal(DeltaTime);

	static bool bKeyDown = false;
	if (bKeyDown == false)
	{
		if (GetAsyncKeyState('P'))
		{
			bKeyDown = true;
			std::vector<GameEntity*> gameEnts;
			if (g_UseEditor)
			{
				for (int i = 0; i < g_Editor->GetGameEntities().size(); i++)
				{
					gameEnts.push_back(g_Editor->GetGameEntities()[i]->GetGameEntity());
				}
			} else
			{
				for (int i = 0; i < g_pGame->GetGameEntities().size(); i++)
				{
					gameEnts.push_back(g_pGame->GetGameEntities()[i]);
				}
			}

			for (int i = 0; i < gameEnts.size(); i++)
			{
				static kbString TreeName("BG Trees");
				GameEntity* const pTargetEnt = gameEnts[i];
				if (pTargetEnt->name() != TreeName)
				{
					continue;
				}

				static float minScale = 250.0f;
				static float maxScale = 275.0f;
				const float randScale = (kbfrand() * (maxScale - minScale)) + minScale;
				Vec3 scale = Vec3Rand(Vec3(randScale, randScale, randScale), Vec3(randScale, randScale, randScale));
				pTargetEnt->set_scale(scale);

				Mat4 rotationMat = Mat4::identity;
				rotationMat.make_identity();

				float randRot = kbfrand() * kbPI * 2.0f;
				rotationMat[0][0] = cos(randRot);
				rotationMat[2][0] = -sin(randRot);
				rotationMat[0][2] = sin(randRot);
				rotationMat[2][2] = cos(randRot);
				pTargetEnt->set_rotation(Quat4::from_mat4(rotationMat));
			}
		} else
		{
			bKeyDown = FALSE;
		}
	} else if (GetAsyncKeyState('P') == false)
	{
		bKeyDown = false;
	}

	if (g_UseEditor)
	{
		return;
	}

	// Not all game entities are loaded in enable_internal unfortunately
	if (m_pHealthBarUI == nullptr || m_pCannonBallUI == nullptr) {
		m_pHealthBarUI = nullptr;
		m_pCannonBallUI = nullptr;
		for (int i = 0; i < g_pBlaiseGame->GetGameEntities().size() && (m_pHealthBarUI == nullptr || m_pCannonBallUI == nullptr); i++) {
			GameEntity* const pTargetEnt = g_pBlaiseGame->GetGameEntities()[i];
			if (m_pHealthBarUI == nullptr) {
				m_pHealthBarUI = pTargetEnt->component<CannonHealthBarUIComponent>();
			}

			if (m_pCannonBallUI == nullptr) {
				m_pCannonBallUI = pTargetEnt->component<CannonBallUIComponent>();
			}

			if (m_BLM.GetEntity() == nullptr) {
				static kbString sBLM("#BLM");
				if (pTargetEnt->name() == sBLM) {
					m_BLM.SetEntity(pTargetEnt);
					pTargetEnt->disable_all_components();
				}
			}

			if (m_Credits.GetEntity() == nullptr) {
				static kbString sCredits("#Credits");
				if (pTargetEnt->name() == sCredits) {
					m_Credits.SetEntity(pTargetEnt);
					pTargetEnt->disable_all_components();
				}
			}
		}
	}

	if (KungFuSheepDirector::Get()->IsInitialized() == false) {


	}

	if (m_p3000Ton == nullptr || m_PresentsEnt[0].GetEntity() == nullptr) {

		static const kbString sBossName("3000 Ton");
		static const kbString sPresent_1("Outro - Present_1");
		static const kbString sPresent_2("Outro - Present_2");
		static const kbString sBreakBridgeDecal("Outro - Bridge Decal");
		static const kbString sFox("Fox");
		static const kbString sBridgeExplosionFX("Outro - Bridge Explosion FX");

		// TODO - Optimize
		for (int i = 0; i < g_pBlaiseGame->GetGameEntities().size(); i++) {
			GameEntity* const pEnt = g_pBlaiseGame->GetGameEntities()[i];
			if (pEnt->name() == sBossName) {
				m_p3000Ton = pEnt->component<CannonActorComponent>();
			} else if (pEnt->name() == sPresent_1) {
				m_PresentsEnt[0].SetEntity(pEnt);
			} else if (pEnt->name() == sPresent_2) {
				m_PresentsEnt[1].SetEntity(pEnt);
			} else if (pEnt->name() == sBreakBridgeDecal) {
				m_BridgeBreakDecal.SetEntity(pEnt);
			} else if (pEnt->name() == sFox) {
				m_pFox = pEnt->component<CannonActorComponent>();
			} else if (pEnt->name() == sBridgeExplosionFX) {
				m_BridgeExplosionFX.SetEntity(pEnt);
			}
		}
	}

	KungFuSheepDirector::Get()->UpdateStateMachine();

	if (m_WaterDropletFXStartTime > 0.0f) {

		int numFinished = 0;

		for (int i = 0; i < NumWaterSplashes; i++) {

			const float curTime = g_GlobalTimer.TimeElapsedSeconds();
			const float fxStartTime = m_WaterDropletFXStartTime + m_WaterSplashFXInst[i].m_InitialDelay;

			if (curTime < fxStartTime) {
				continue;
			}

			const float fxDuration = m_WaterSplashFXInst[i].m_Duration;
			float normalizedTime = (g_GlobalTimer.TimeElapsedSeconds() - fxStartTime) / fxDuration;
			StaticModelComponent* const pSM = m_WaterSplashFXInst[i].m_Entity.GetEntity()->component<StaticModelComponent>();

			if (normalizedTime > 1.0f) {
				pSM->Enable(false);
				numFinished++;
			} else {
				pSM->Enable(true);
				const float delayScrollTime = g_WaterDropStartDelay[i];
				if (normalizedTime > delayScrollTime) {
					normalizedTime = kbClamp((normalizedTime - delayScrollTime) * (1.0f / delayScrollTime), 0.0f, 999.0f);
					static kbString normalFactor_scrollRate("normalFactor_scrollRate");
					Vec4 scroll = g_WaterDropletNormalFactorScroll[i];
					scroll.w *= -normalizedTime;

					pSM->set_material_param_vec4(0, normalFactor_scrollRate.stl_str(), scroll);

					// Blend out time
					{
						const float blendOutStart = fxStartTime + (fxDuration * 0.75f);
						const float blendOutTime = kbClamp((g_GlobalTimer.TimeElapsedSeconds() - blendOutStart) / (fxDuration * 0.25f), 0.0f, 1.0f);
						static kbString colorFactor("colorFactor");
						pSM->set_material_param_vec4(0, colorFactor.stl_str(), Vec4(1.0f, 1.0f, 1.0f, 1.0f - blendOutTime));
					}
				}
			}
		}

		if (numFinished == NumWaterSplashes) {
			m_WaterDropletFXStartTime = -1.0f;
		}
	}

	// Global Fog
	{
		kbShaderParamOverrides_t shaderParam;
		shaderParam.SetVec4("globalFogColor", Vec4(174.0f / 256.0f, 183.0f / 256.0f, 198.0f / 256.0f, 1.0f));
	}

	// Global Sun
	{
		float sunIntensity = 0.0f;
		float travelDist = GetPlayerTravelDistance();
		float startBlendInDist = 30.0f;
		sunIntensity = kbSaturate((GetPlayerTravelDistance() - startBlendInDist) / 75.0f);

		kbShaderParamOverrides_t shaderParam;
		shaderParam.SetVec4("globalSunFactor", Vec4(sunIntensity, sunIntensity, sunIntensity, sunIntensity));

		// globalSunFactor
	}

	// UI

	}
	UpdateDebugAndCheats();
}

/// KungFuLevelComponent::UpdateSheepHealthBar
void KungFuLevelComponent::UpdateSheepHealthBar(const float healthVal) {

	if (m_pHealthBarUI == nullptr) {
		for (int i = 0; i < g_pBlaiseGame->GetGameEntities().size() && (m_pHealthBarUI == nullptr); i++) {
			GameEntity* const pTargetEnt = g_pBlaiseGame->GetGameEntities()[i];
			if (m_pHealthBarUI == nullptr) {
				m_pHealthBarUI = pTargetEnt->component<CannonHealthBarUIComponent>();
			}
		}
	}

	m_pHealthBarUI->SetTargetHealth(healthVal);
}

/// KungFuLevelComponent::UpdateCannonBallMeter
void KungFuLevelComponent::UpdateCannonBallMeter(const float fillVal, const bool bActivated) {

	if (m_pCannonBallUI == nullptr) {
		return;
	}

	if (bActivated) {
		m_pCannonBallUI->CannonBallActivatedCB();
	} else {
		m_pCannonBallUI->SetFill(fillVal);
	}
}


/// KungFuLevelComponent::SpawnSheep
KungFuSheepComponent* KungFuLevelComponent::SpawnSheep() {
	GameEntity* const sheep = g_pGame->CreateEntity(m_SheepPrefab.GetEntity());

	sheep->set_position(KungFuGame::kSheepStartPos);
	sheep->set_rotation(KungFuGame::kSheepStartRot);

	m_pSheep = sheep->component<KungFuSheepComponent>();
	return m_pSheep;
}

/// KungFuLevelComponent::SpawnEnemy
void KungFuLevelComponent::SpawnEnemy(const bool bSpawnLeft, const int waveSize) {

	if (m_SnolafPrefab.GetEntity() == nullptr) {
		return;
	}
}

/// KungFuLevelComponent::DoWaterDropletScreenFX
void KungFuLevelComponent::DoWaterDropletScreenFX() {

	if (m_WaterDropletScreenFX.GetEntity() == nullptr || m_WaterDropletFXStartTime > 0.0f) {
		return;
	}

	m_WaterDropletFXStartTime = g_GlobalTimer.TimeElapsedSeconds();
	m_WaterSplashFXInst[0].m_Duration = 1.5f;
	m_WaterSplashFXInst[0].m_InitialDelay = 1.0f;

	m_WaterSplashFXInst[1].m_Duration = 2.0f;
	m_WaterSplashFXInst[1].m_InitialDelay = 0.75f;

	//m_WaterDropletScreenFXInst.SetEntity( g_pGame->CreateEntity( m_WaterDropletScreenFX.GetEntity() ) );

}

/// KungFuLevelComponent::DoSplashSound
void KungFuLevelComponent::DoSplashSound() {

	if (m_WaterSplashSound.size() == 0) {
		return;
	}

	if (g_GlobalTimer.TimeElapsedSeconds() < m_LastWaterSplashSoundTime + 0.1f) {
		return;
	}

	m_LastWaterSplashSoundTime = g_GlobalTimer.TimeElapsedSeconds();
	m_WaterSplashSound[rand() % m_WaterSplashSound.size()].PlaySoundAtPosition(Vec3(0.0f, 0.0f, 0.0f));
}

/// KungFuLevelComponent::DoBreakBridgeEffect
void KungFuLevelComponent::DoBreakBridgeEffect(const bool bBreakIt) {
	m_BridgeBreakDecal.GetEntity()->component<StaticModelComponent>()->Enable(bBreakIt);
	m_BridgeExplosionFX.GetEntity()->component<ParticleComponent>()->Enable(bBreakIt);
}

/// KungFuLevelComponent::SetPlayLevelMusic
void KungFuLevelComponent::SetPlayLevelMusic(const int idx, const bool bPlay) {
	if (bPlay) {
		m_LevelMusic[idx].PlaySoundAtPosition(Vec3::zero);
	} else {
		m_LevelMusic[idx].StopSound();
	}
}

/// KungFuLevelComponent::ShowBLM
void KungFuLevelComponent::ShowBLM(const bool bShow) {
	if (m_BLM.GetEntity() == nullptr) {
		return;
	}

	if (bShow) {
		m_BLM.GetEntity()->enable_all_components();
	} else {
		m_BLM.GetEntity()->disable_all_components();
	}
}

/// KungFuLevelComponent::ShowCredits
void KungFuLevelComponent::ShowCredits(const bool bShow) {
	GameEntity* const pEnt = m_Credits.GetEntity();
	if (pEnt == nullptr) {
		return;
	}

	if (bShow) {
		pEnt->enable_all_components();
	} else {
		pEnt->disable_all_components();
	}
}

/// KungFuLevelComponent::ShowCredits
void KungFuLevelComponent::ShowHealthBar(const bool bShow) {
	if (m_pHealthBarUI == nullptr) {
		return;
	}

	if (bShow) {
		m_pHealthBarUI->Enable(true);
		m_pCannonBallUI->GetOwner()->enable_all_components();
	} else {
		m_pHealthBarUI->Enable(false);
		m_pCannonBallUI->GetOwner()->disable_all_components();
	}
}

/// KungFuLevelComponent::GetPlayerTravelDistance
float KungFuLevelComponent::GetPlayerTravelDistance() {

	return (g_pBlaiseGame->GetPlayer()->owner_position() - KungFuGame::kSheepStartPos).length();
}

/// KungFuLevelComponent::GetSnolafFromPool
KungFuSnolafComponent* KungFuLevelComponent::GetSnolafFromPool() {
	GameEntity* const pSnolaf = m_SnolafPool.back();
	pSnolaf->component<KungFuSnolafComponent>()->ResetFromPool();
	m_SnolafPool.pop_back();//g_pGame->CreateEntity( m_SnolafPrefab.GetEntity() );
	return pSnolaf->component<KungFuSnolafComponent>();
}

/// KungFuLevelComponent::ReturnSnolafToPool
void KungFuLevelComponent::ReturnSnolafToPool(KungFuSnolafComponent* const pSnolafComp) {

	GameEntity* const pSnolaf = pSnolafComp->GetOwner();
	pSnolaf->disable_all_components();

	m_SnolafPool.push_back(pSnolaf);

	if (pSnolafComp == m_EndSnolafs[0]) {
		m_EndSnolafs[0] = nullptr;
	} else if (pSnolafComp == m_EndSnolafs[1]) {
		m_EndSnolafs[1] = nullptr;
	}

	//	blk::log("Snolaf returned.  Pool size = %d", m_SnolafPool.size() );
}

void KungFuLevelComponent::RemoveSheep() {
	g_pGame->RemoveGameEntity(m_pSheep->GetOwner());
	m_pSheep = nullptr;
}

/// KungFuLevelComponent::UpdateDebugAndCheats
void KungFuLevelComponent::UpdateDebugAndCheats() {
	const kbInput_t& input = g_pInputManager->get_input();

	if (input.IsNonCharKeyPressedOrDown(kbInput_t::LCtrl)) {
		if (input.IsKeyPressedOrDown('D')) {
			DealAttackInfo_t<KungFuGame::eAttackType> damageInfo;
			damageInfo.m_BaseDamage = 999999.0f;
			damageInfo.m_pAttacker = nullptr;
			damageInfo.m_Radius = 10.0f;
			damageInfo.m_AttackType = KungFuGame::DebugDeath;

			m_pSheep->take_damage(damageInfo);
			g_pBlaiseGame->GetMainCamera()->SetTarget(nullptr, -1.0f);
		}

		if (input.IsKeyPressedOrDown('C')) {
			m_pSheep->m_CannonBallMeter = 2.0f;
			KungFuLevelComponent::Get()->UpdateCannonBallMeter(m_pSheep->m_CannonBallMeter, false);
		}

		if (input.WasKeyJustPressed('S')) {
			static float lastPlayTime = 0.0f;
			if (g_GlobalTimer.TimeElapsedSeconds() - lastPlayTime > 2.0f) {
				m_pSheep->PlayShakeNBakeFX();
				lastPlayTime = g_GlobalTimer.TimeElapsedSeconds();
			}
		}
	}

	static bool bOldGrass = false;
	static kbString BGName("BG Trees");
	if (g_CullGrass.GetBool() != bOldGrass) {
		bOldGrass = g_CullGrass.GetBool();
		g_bCullGrass = bOldGrass;

		for (int i = 0; i < g_pBlaiseGame->GetGameEntities().size(); i++) {
			GameEntity* const pTargetEnt = g_pBlaiseGame->GetGameEntities()[i];
			if (pTargetEnt->name() == BGName) {
				if (g_bCullGrass) {
					pTargetEnt->enable_all_components();
				} else {
					pTargetEnt->disable_all_components();
				}
			}
/*
			TerrainComponent* const pTerrain = pTargetEnt->component<TerrainComponent>();
			if (pTerrain != nullptr) {
				pTerrain->RegenerateTerrain();
			}*/
		}
	}
}

/// KungFuSheepDirector::KungFuSheepDirector
KungFuSheepDirector::KungFuSheepDirector() :
	m_pHealthBarUI(nullptr),
	m_pCannonBallUI(nullptr),
	m_pMainMenuUI(nullptr),
	m_pPauseMenuUI(nullptr),
	m_NumHuggers(0),
	m_NumPrehuggers(0) {
}

/// KungFuSheepDirector::~KungFuSheepDirector
KungFuSheepDirector::~KungFuSheepDirector() {

}

/// KungFuSheepDirector::InitializeStateMachine_Internal
void KungFuSheepDirector::InitializeStateMachine_Internal() {

	kbLevelDirector::InitializeStateMachine_Internal();

	m_pHealthBarUI = nullptr;
	m_pMainMenuUI = nullptr;
	m_pPauseMenuUI = nullptr;
	m_pCannonBallUI = nullptr;

	m_NumHuggers = 0;
	m_NumPrehuggers = 0;
}

/// KungFuSheepDirector::ShutdownStateMachine_Internal
void KungFuSheepDirector::ShutdownStateMachine_Internal() {

	kbLevelDirector::ShutdownStateMachine_Internal();
	if (m_pHealthBarUI != nullptr) {

		m_pMainMenuUI->UnregisterEventListener(this);
		m_pPauseMenuUI->UnregisterEventListener(this);
	}
}

/// KungFuSheepDirector::CollectUIElements
void KungFuSheepDirector::CollectUIElements() {

	m_pHealthBarUI = nullptr;
	m_pCannonBallUI = nullptr;
	m_pMainMenuUI = nullptr;
	m_pPauseMenuUI = nullptr;

	for (int i = 0; i < g_pBlaiseGame->GetGameEntities().size(); i++) {
		GameEntity* const pTargetEnt = g_pBlaiseGame->GetGameEntities()[i];
		if (m_pHealthBarUI == nullptr) {
			m_pHealthBarUI = pTargetEnt->component<CannonHealthBarUIComponent>();
		}

		if (m_pCannonBallUI == nullptr) {
			m_pCannonBallUI = pTargetEnt->component<CannonBallUIComponent>();
		}

		if (m_pMainMenuUI == nullptr) {
			m_pMainMenuUI = pTargetEnt->component<CannonBallMainMenuComponent>();
			if (m_pMainMenuUI) {
				m_pMainMenuUI->RegisterEventListener(this);
			}
		}

		if (m_pPauseMenuUI == nullptr) {
			m_pPauseMenuUI = pTargetEnt->component<CannonBallPauseMenuUIComponent>();
			if (m_pPauseMenuUI) {
				m_pPauseMenuUI->RegisterEventListener(this);
			}
		}
	}
}

/// KungFuSheepDirector::UpdateStateMachine
void KungFuSheepDirector::UpdateStateMachine() {
	kbLevelDirector::UpdateStateMachine();

	if (m_pHealthBarUI == nullptr) {
		CollectUIElements();

		if (GetCurrentState() == KungFuGame::MainMenu) {
			m_pHealthBarUI->GetOwner()->disable_all_components();
			m_pCannonBallUI->GetOwner()->disable_all_components();
		}
	}
	// TODO - Optimize
	m_NumHuggers = 0;
	m_NumPrehuggers = 0;
	for (int i = 0; i < g_pBlaiseGame->GetGameEntities().size(); i++) {

		GameEntity* const pEnt = g_pBlaiseGame->GetGameEntities()[i];
		if (pEnt->GetActorComponent() == nullptr) {
			continue;
		}

		KungFuSnolafComponent* const pSnolaf = pEnt->GetActorComponent()->GetAs<KungFuSnolafComponent>();
		if (pSnolaf == nullptr || pSnolaf->IsEnabled() == false) {
			continue;
		}

		if (pSnolaf->GetState() == KungFuSnolafState::Hug) {
			m_NumHuggers++;
		} else if (pSnolaf->GetState() == KungFuSnolafState::Prehug) {
			m_NumPrehuggers++;
		}
	}
}

/// KungFuSheepDirector::DoAttack
AttackHitInfo_t KungFuSheepDirector::DoAttack(const DealAttackInfo_t<KungFuGame::eAttackType> attackInfo) {

	AttackHitInfo_t attackHitInfo;
	if (this->m_CurrentState < 0 || m_CurrentState >= KungFuGame::NumStates) {
		return attackHitInfo;
	}

	return m_States[m_CurrentState]->DoAttack(attackInfo);
}

/// KungFuSheepDirector::StateChangeCB
void KungFuSheepDirector::StateChangeCB(const KungFuGame::eKungFuGame_State previousState, const KungFuGame::eKungFuGame_State nextState) {

	// TODO - Optimize
	if (m_pHealthBarUI == nullptr) {
		CollectUIElements();
	}

	if (g_SkipCheat == KungFuGame::Skip_MainMenuAndIntro || g_SkipCheat == KungFuGame::Skip_ToEnd || g_SkipCheat == KungFuGame::Skip_NoEnemies) {

		m_pHealthBarUI->GetOwner()->enable_all_components();
		m_pCannonBallUI->GetOwner()->enable_all_components();
		m_pMainMenuUI->GetOwner()->disable_all_components();

		if (nextState == KungFuGame::MainMenu) {
			for (int i = 0; i < g_pBlaiseGame->GetGameEntities().size(); i++) {

				GameEntity* const pTargetEnt = g_pBlaiseGame->GetGameEntities()[i];
				if (pTargetEnt->component<KungFuSheepComponent>()) {
					continue;
				}

				if (pTargetEnt->component<KungFuSnolafComponent>()) {
					pTargetEnt->disable_all_components();
					g_pGame->GetLevelComponent<KungFuLevelComponent>()->ReturnSnolafToPool(pTargetEnt->component<KungFuSnolafComponent>());
				}
			}
			KungFuLevelComponent::Get()->SetPlayLevelMusic(1, false);
		}

		return;
	}

	if (nextState == KungFuGame::Gameplay) {
		if (m_pHealthBarUI != nullptr) {
			m_pHealthBarUI->GetOwner()->enable_all_components();
			m_pCannonBallUI->GetOwner()->enable_all_components();
		}
	} else if (nextState == KungFuGame::MainMenu) {
		if (m_pHealthBarUI != nullptr) {
			m_pHealthBarUI->GetOwner()->disable_all_components();
			m_pCannonBallUI->GetOwner()->disable_all_components();

			m_pMainMenuUI->GetOwner()->disable_all_components();
			m_pMainMenuUI->GetOwner()->enable_all_components();
		}

		for (int i = 0; i < g_pBlaiseGame->GetGameEntities().size(); i++) {

			GameEntity* const pTargetEnt = g_pBlaiseGame->GetGameEntities()[i];
			if (pTargetEnt->component<KungFuSheepComponent>()) {
				continue;
			}

			if (pTargetEnt->component<KungFuSnolafComponent>()) {
				pTargetEnt->disable_all_components();
				g_pGame->GetLevelComponent<KungFuLevelComponent>()->ReturnSnolafToPool(pTargetEnt->component<KungFuSnolafComponent>());
			}
		}

		KungFuLevelComponent::Get()->SetPlayLevelMusic(1, false);

	} else if (nextState == KungFuGame::Intro) {
		if (m_pMainMenuUI != nullptr) {
			m_pMainMenuUI->SetAnimationFrame(1);
		}
	} else if (previousState == KungFuGame::MainMenu && nextState == KungFuGame::Paused) {
		if (m_pMainMenuUI != nullptr) {
			m_pMainMenuUI->GetOwner()->disable_all_components();
		}
	}
}

/// KungFuSheepDirector::WidgetEventCB
void KungFuSheepDirector::WidgetEventCB(kbUIWidgetComponent* const pWidget, const kbInput_t* const pInput) {

	m_States[m_CurrentState]->WidgetEventCB(pWidget, pInput);
}
