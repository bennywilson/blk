/// kbLevelComponent.cpp
///
/// 2019 blk

#include "blk_core.h"
#include "entity_header.h"
#include "level_component.h"

static const kbLevelComponent * g_pLevelComponent = nullptr;

/// kbLevelComponent::Constructor
void kbLevelComponent::Constructor() {
	m_LevelType = LevelType_2D;
	m_GlobalModelScale = 1.0f;
	m_EditorIconScale = 1.0f;
	m_GlobalVolumeScale = 1.0f;

	// First one loaded owns the global scales below, and only it may clear
	// them. A map is supposed to hold a single level entity (kbEditor::LoadMap
	// drops extras), but a stale map can still carry more than one -- letting
	// each one claim and then null this pointer would leave the survivor's
	// settings unreachable and every scale silently reading back as 1.
	if (!g_pLevelComponent) {
		g_pLevelComponent = this;
	}
}

/// kbLevelComponent::~kbLevelComponent
kbLevelComponent::~kbLevelComponent() {
	if (g_pLevelComponent == this) {
		g_pLevelComponent = nullptr;
	}
}

/// kbLevelComponent::enable_internal
void kbLevelComponent::enable_internal( const bool bEnable ) {
	Super::enable_internal( bEnable );

/*if ( bEnable ) {
		g_pRenderer->SetWorldAndEditorIconScale( m_GlobalModelScale, m_EditorIconScale );
		g_pLevelComponent = this;	
	} else {
		g_pRenderer->SetWorldAndEditorIconScale( 1.0f, 1.0f );
		g_pLevelComponent = nullptr;
	}*/
}

/// kbLevelComponent::EditorChange
void kbLevelComponent::editor_change( const std::string & propertyName ) {
	Super::editor_change( propertyName );

/*	if ( propertyName == "WorldScale" || propertyName == "IconScale" ) {
		g_pRenderer->SetWorldAndEditorIconScale( m_GlobalModelScale , m_EditorIconScale );
	}*/
}

/// kbLevelComponent::GetGlobalModelScale
float kbLevelComponent::GetGlobalModelScale() {

	if ( g_pLevelComponent == nullptr ) {
		return 1;
	}

	return g_pLevelComponent->m_GlobalModelScale;
}

/// kbLevelComponent::GetEditorIconScale
float kbLevelComponent::GetEditorIconScale() {

	if ( g_pLevelComponent == nullptr ) {
		return 1;
	}

	return g_pLevelComponent->m_EditorIconScale;
}

/// kbLevelComponent::GetGlobalVolumeScale
float kbLevelComponent::GetGlobalVolumeScale() {

	if ( g_pLevelComponent == nullptr ) {
		return 1;
	}

	return g_pLevelComponent->m_GlobalVolumeScale;
}

/// kbCinematicAction
void kbCinematicAction::Constructor() {
	m_fCineParam = 0.0f;
}

/// kbCinematicComponent::~kbCinematicComponent
kbCinematicComponent::~kbCinematicComponent() {

}

/// kbCinematicComponent::Constructor
void kbCinematicComponent::Constructor() {
}

/// kbCinematicComponent::enable_internal
void kbCinematicComponent::enable_internal( const bool bEnable ) {

	Super::enable_internal( true );
}

/// kbCinematicComponent::update_internal
void kbCinematicComponent::update_internal( const float dt ) {
	
	Super::update_internal( dt );

}
