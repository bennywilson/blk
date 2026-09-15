/// LightComponent.cpp
///
/// 2016 blk

#include "blk_core.h"
#include "Matrix.h"
#include "Quaternion.h"
#include "bounds.h"
#include "entity_header.h"
#include "renderer.h"

BLK_DEFINE_COMPONENT(LightComponent)
BLK_DEFINE_COMPONENT(DirectionalLightComponent)

/// LightComponent::Constructor
void LightComponent::Constructor() {
	m_color = Color::white;
	m_casts_shadow = false;
	m_brightness = 1;
	m_bShaderParamsDirty = false;
}

/// LightComponent::~LightComponent
LightComponent::~LightComponent() {
}

/// LightComponent::post_load
void LightComponent::post_load() {
	Super::post_load();
}

/// LightComponent::editor_change
void LightComponent::editor_change(const std::string& propertyName) {
	Super::editor_change(propertyName);

	if (IsEnabled()) {
		m_bShaderParamsDirty = true;
	}

	// Editor Hack
	if (propertyName == "Materials") {
		for (i32 i = 0; i < m_materials.size(); i++) {
			m_materials[i].SetOwningComponent(this);
		}
	}
}

/// LightComponent::render_sync
void LightComponent::render_sync() {
	Super::render_sync();

	if (m_bShaderParamsDirty) {
		refresh_materials();
		m_bShaderParamsDirty = false;
	}
}

/// LightComponent::enable_internal
void LightComponent::enable_internal(const bool is_enabled) {
	Super::enable_internal(is_enabled);

	if (g_renderer != nullptr) {
		if (is_enabled) {
			m_bShaderParamsDirty = true;

			g_renderer->add_light_component(this);
		} else {
			g_renderer->remove_light_component(this);
		}
	}
}

/// LightComponent:RefreshMaterials
void LightComponent::refresh_materials() {

	//m_render_object.m_Materials.clear();
	/*{//for ( int i = 0; i < m_materials.size(); i++ ) {
		MaterialComponent & matComp = m_Material;
	
		ShaderParamOverrides_t newShaderParams;
		newShaderParams.m_shader = matComp.get_shader();
	
		auto srcShaderParams = matComp.shader_params();
		for ( int j = 0; j < srcShaderParams.size(); j++ ) {
			if ( srcShaderParams[j].Texture() != nullptr ) {
				newShaderParams.SetTexture( srcShaderParams[j].param_name().stl_str(), srcShaderParams[j].Texture() );
			} else if ( srcShaderParams[j].RenderTexture() != nullptr ) {
	
				newShaderParams.SetTexture( srcShaderParams[j].param_name().stl_str(), srcShaderParams[j].RenderTexture() );
			} else {
				newShaderParams.SetVec4( srcShaderParams[j].param_name().stl_str(), srcShaderParams[j].GetVector() );
			}
		}
	
		m_render_object.m_Materials.push_back( newShaderParams );
	}

	if ( IsEnabled() && m_render_object.m_pComponent != nullptr && bRefreshRenderObejct ) {
		g_pRenderer->UpdateRenderObject( m_render_object );
	}

	/*if ( m_pOverrideShader == nullptr ) {
		return;
	}

	for ( int i = 0; i < m_OverrideShaderParamList.size(); i++ ) {
		const ShaderParamComponent & curParam = m_OverrideShaderParamList[i];
		if ( curParam.param_name().stl_str().empty() ) {
			continue;
		}

		if ( curParam.Texture() != nullptr ) {
			m_OverrideShaderParams.SetTexture( curParam.param_name().stl_str(), curParam.Texture() );
		} else {
			m_OverrideShaderParams.SetVec4( curParam.param_name().stl_str(), curParam.GetVector() );
		}	
	}*/
}

/// LightComponent:update_internal
void LightComponent::update_internal(const f32 dt) {
	Super::update_internal(dt);

	if (GetLifeTimeRemaining() >= 0) {
		// Hack fade for grenade
		m_brightness = blk::clamp(GetLifeTimeRemaining() / GetStartingLifeTime(), 0.0f, 1.0f);
		//g_pRenderer->UpdateLight( this, GetOwner()->position(), GetOwner()->rotation() );
		return;
	}

	if (this->IsA(DirectionalLightComponent::GetType())) {
		ShaderParamOverrides_t shaderParam;
		shaderParam.SetVec4("sunDir", GetOwner()->rotation().to_mat4()[2] * -1.0f);
		//g_pRenderer->SetGlobalShaderParam( shaderParam );
	}

	if (IsDirty()) {
		//g_pRenderer->UpdateLight( this, GetOwner()->position(), GetOwner()->rotation() );
	}
}

/// PointLightComponent::Constructor
void PointLightComponent::Constructor() {
	m_radius = 16.0f;

//	const f32 light_speed = blk::frand(50.f, 100.f);
//	vel = Vec3Rand(Vec3(-light_speed, 0.0, -light_speed), Vec3(light_speed, 0.f, light_speed));
}

void PointLightComponent::update_internal(const f32 dt) {

	/*if (m_radius < 10.) {
		m_radius *= blk::frand(10.f, 20.f);
		m_radius *= blk::frand(10.f, 20.f);
	}
	Vec3 new_pos = GetOwner()->position() + vel * dt;
	if (new_pos.x < 0) vel.x = abs(vel.x);
	if (new_pos.x > 400.f) vel.x = -abs(vel.x);
	if (new_pos.z < 0) vel.z = abs(vel.x);
	if (new_pos.z > 400.f) vel.z = -abs(vel.x);

	GetOwner()->set_position(new_pos);*/
}


/// CylindricalLightComponent::Constructor
void CylindricalLightComponent::Constructor() {
	m_length = 32.0f;
}

/// DirectionalLightComponent::Constructor
void DirectionalLightComponent::Constructor() {
}

/// DirectionalLightComponent::~DirectionalLightComponent
DirectionalLightComponent::~DirectionalLightComponent() {
}

/// DirectionalLightComponent::EditorChange
void DirectionalLightComponent::editor_change(const std::string& propertyName) {
	Super::editor_change(propertyName);
	// TODO: clamp shadow splits to 4.  Also ensure that the ordering is correct

/*	{
		ShaderParamOverrides_t shaderParam;
		shaderParam.SetVec4( "sunDir", GetOwner()->rotation().to_mat4()[2] * -1.0f );
		g_pRenderer->SetGlobalShaderParam( shaderParam );
	}*/
}

/// LightShaftsComponent::Constructor
void LightShaftsComponent::Constructor() {
	m_Texture = nullptr;
	m_Color = Color::white;
	m_BaseWidth = m_BaseHeight = 20.0f;
	m_IterationWidth = m_IterationHeight = 1.0f;
	m_NumIterations = 4;
	m_Directional = true;
}

/// LightShaftsComponent::~LightShaftsComponent
LightShaftsComponent::~LightShaftsComponent() {
}

/// LightShaftsComponent::enable_internal
void LightShaftsComponent::enable_internal(const bool isEnabled) {
	Super::enable_internal(isEnabled);

	/*if ( g_pRenderer != nullptr ) {
		if ( isEnabled ) {
			g_pRenderer->AddLightShafts( this, GetOwner()->position(), GetOwner()->rotation() );
		} else {
			g_pRenderer->RemoveLightShafts( this );
		}
	}*/
}

/// LightShaftsComponent::SetColor
void LightShaftsComponent::SetColor(const Color& newColor) {
	m_Color = newColor;
}

/// LightShaftsComponent::update_internal
void LightShaftsComponent::update_internal(const float DeltaTime) {
	Super::update_internal(DeltaTime);

/*	g_pRenderer->UpdateLightShafts( this, GetOwner()->position(), GetOwner()->rotation() );

	ShaderParamOverrides_t shaderParam;
	shaderParam.SetVec4( "lightShaftsDir", GetOwner()->rotation().to_mat4()[2] * -1.0f );
	shaderParam.SetVec4( "lightShaftsColor", m_Color );
	g_pRenderer->SetGlobalShaderParam( shaderParam );*/
}

/// FogComponent::Constructor
void FogComponent::Constructor() {
	m_Color = Color::white;
	m_StartDistance = 2100;
	m_EndDistance = 2200;
}

/// FogComponent::update_internal
void FogComponent::update_internal(const float DT) {
	Super::update_internal(DT);

	//g_pRenderer->UpdateFog( m_Color, m_StartDistance, m_EndDistance );
}
