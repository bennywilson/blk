/// kbDebugComponents.cpp
///
/// 2018 blk

#include "blk_core.h"
#include "Matrix.h"
#include "Quaternion.h"
#include "entity_header.h"
#include "debug_component.h"

/// kbDebugSphereCollision::Constructor
void kbDebugSphereCollision::Constructor() {
	m_pCollisionModel = (kbModel*)g_ResourceManager.resource( "../../blk_engine/assets/Models/UnitSphere.ms3d", true, true );

	m_render_object.m_casts_shadow = false;
	m_render_object.m_bIsSkinnedModel = false;
	m_render_object.m_pComponent = this;
	m_render_object.m_model = m_pCollisionModel;
	m_render_object.m_render_pass = RP_Lighting;
}

/// kbDebugSphereCollision::enable_internal
void kbDebugSphereCollision::enable_internal( const bool bEnable ) {
	Super::enable_internal( bEnable );

	m_render_object.m_model = m_pCollisionModel;
	/*if ( bEnable ) {
		g_pRenderer->AddRenderObject( m_render_object );
	} else {
		g_pRenderer->RemoveRenderObject( m_render_object );
	}*/
}

/// kbDebugSphereCollision::update_internal
void kbDebugSphereCollision::update_internal( const float DeltaTime ) {
	Super::update_internal( DeltaTime );

	m_render_object.m_position = GetOwner()->position();
	m_render_object.m_rotation = GetOwner()->rotation();
	m_render_object.m_Scale = GetOwner()->scale() * kbLevelComponent::GetGlobalModelScale();

	m_render_object.m_model = m_pCollisionModel;
	//g_pRenderer->UpdateRenderObject( m_render_object );
}