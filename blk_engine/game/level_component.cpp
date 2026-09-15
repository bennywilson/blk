/// level_component.cpp
///
/// 2019 blk

#include "blk_core.h"
#include "entity_header.h"
#include "level_component.h"

static const LevelComponent* g_pLevelComponent = nullptr;

/// LevelComponent::Constructor
void LevelComponent::Constructor() {
	m_LevelType = LevelType_2D;
	m_GlobalModelScale = 1.0f;
	m_EditorIconScale = 1.0f;
	m_GlobalVolumeScale = 1.0f;

	// First one loaded owns the global scales below, and only it may clear
	// them. A map is supposed to hold a single level entity (Editor::LoadMap
	// drops extras), but a stale map can still carry more than one -- letting
	// each one claim and then null this pointer would leave the survivor's
	// settings unreachable and every scale silently reading back as 1.
	if (!g_pLevelComponent) {
		g_pLevelComponent = this;
	}
}

/// LevelComponent::~LevelComponent
LevelComponent::~LevelComponent() {
	if (g_pLevelComponent == this) {
		g_pLevelComponent = nullptr;
	}
}

/// LevelComponent::enable_internal
void LevelComponent::enable_internal(const bool bEnable) {
	Super::enable_internal(bEnable);

/*if ( bEnable ) {
		g_pRenderer->SetWorldAndEditorIconScale( m_GlobalModelScale, m_EditorIconScale );
		g_pLevelComponent = this;	
	} else {
		g_pRenderer->SetWorldAndEditorIconScale( 1.0f, 1.0f );
		g_pLevelComponent = nullptr;
	}*/
}

/// LevelComponent::EditorChange
void LevelComponent::editor_change(const std::string& propertyName) {
	Super::editor_change(propertyName);

/*	if ( propertyName == "WorldScale" || propertyName == "IconScale" ) {
		g_pRenderer->SetWorldAndEditorIconScale( m_GlobalModelScale , m_EditorIconScale );
	}*/
}

/// LevelComponent::GetGlobalModelScale
float LevelComponent::GetGlobalModelScale() {

	if (g_pLevelComponent == nullptr) {
		return 1;
	}

	return g_pLevelComponent->m_GlobalModelScale;
}

/// LevelComponent::GetEditorIconScale
float LevelComponent::GetEditorIconScale() {

	if (g_pLevelComponent == nullptr) {
		return 1;
	}

	return g_pLevelComponent->m_EditorIconScale;
}

/// LevelComponent::GetGlobalVolumeScale
float LevelComponent::GetGlobalVolumeScale() {

	if (g_pLevelComponent == nullptr) {
		return 1;
	}

	return g_pLevelComponent->m_GlobalVolumeScale;
}

/// CinematicAction
void CinematicAction::Constructor() {
	m_fCineParam = 0.0f;
}

/// CinematicComponent::~CinematicComponent
CinematicComponent::~CinematicComponent() {
}

/// CinematicComponent::Constructor
void CinematicComponent::Constructor() {
}

/// CinematicComponent::enable_internal
void CinematicComponent::enable_internal(const bool bEnable) {

	Super::enable_internal(true);
}

/// CinematicComponent::update_internal
void CinematicComponent::update_internal(const float dt) {

	Super::update_internal(dt);
}
