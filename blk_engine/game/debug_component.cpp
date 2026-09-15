/// debug_component.cpp
///
/// 2018 blk

#include "blk_core.h"
#include "Matrix.h"
#include "Quaternion.h"
#include "entity_header.h"
#include "debug_component.h"

/// DebugSphereCollision::Constructor
void DebugSphereCollision::Constructor() {
	m_pCollisionModel = (Model*)g_ResourceManager.resource( "../../blk_engine/assets/Models/UnitSphere.ms3d", true, true );

	m_render_object.m_casts_shadow = false;
	m_render_object.m_bIsSkinnedModel = false;
	m_render_object.m_pComponent = this;
	m_render_object.m_model = m_pCollisionModel;
	m_render_object.m_render_pass = RP_Lighting;
}

/// DebugSphereCollision::enable_internal
void DebugSphereCollision::enable_internal( const bool bEnable ) {
	Super::enable_internal( bEnable );

	m_render_object.m_model = m_pCollisionModel;
	/*if ( bEnable ) {
		g_pRenderer->AddRenderObject( m_render_object );
	} else {
		g_pRenderer->RemoveRenderObject( m_render_object );
	}*/
}

/// DebugSphereCollision::update_internal
void DebugSphereCollision::update_internal( const float DeltaTime ) {
	Super::update_internal( DeltaTime );

	m_render_object.m_position = GetOwner()->position();
	m_render_object.m_rotation = GetOwner()->rotation();
	m_render_object.m_Scale = GetOwner()->scale() * LevelComponent::GetGlobalModelScale();

	m_render_object.m_model = m_pCollisionModel;
	//g_pRenderer->UpdateRenderObject( m_render_object );
}