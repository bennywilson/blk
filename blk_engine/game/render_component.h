/// render_component.h
///
// 2016 blk

#pragma once

#include "component.h"
#include "render_defs.h"

class Texture;
class Shader;
class RenderTexture;

/// ShaderParamComponent
class ShaderParamComponent : public GameComponent {
	BLK_DECLARE_COMPONENT(ShaderParamComponent, GameComponent);

	friend class MaterialComponent;

public:
	const String& param_name() const { return m_param_name; }
	const Texture* texture() const { return m_texture; }
	const RenderTexture* render_texture() const { return m_render_texture; }
	const Vec4& vector() const { return m_vector; }

	void set_render_texture(RenderTexture* const pTexture) { m_render_texture = pTexture; }
	void set_param_name(const String& newName) { m_param_name = newName; }
	void set_texture(Texture* const pTexture) { m_texture = pTexture; }
	void set_vector(const Vec4& vector) { m_vector = vector; }

private:
	String m_param_name;
	Texture* m_texture;
	RenderTexture* m_render_texture;
	Vec4 m_vector;
};

/// ShaderModifierComponent
class ShaderModifierComponent : public GameComponent {
	BLK_DECLARE_COMPONENT(ShaderModifierComponent, GameComponent);

protected:
	virtual void enable_internal(const bool isEnabled) override;
	virtual void update_internal(const float DeltaTime) override;

	// Editor
	std::vector<VectorAnimEvent> m_ShaderVectorEvents;

	// Runtime
	class RenderComponent* m_pRenderComponent;
	float m_start_time;
	float m_anim_length_sec;
};

/// MaterialComponent
class MaterialComponent : public GameComponent {
	BLK_DECLARE_COMPONENT(MaterialComponent, GameComponent);

public:
	virtual void editor_change(const std::string& propertyName) override;

	const Shader* get_shader() const { return m_shader; }
	const std::vector<ShaderParamComponent>& shader_params() const { return m_shader_params; }
	ECullMode cull_mode_override() const { return m_cull_override; }

	EBlendMode blend_override() const {
		return m_blend_override;
	}

	void set_shader(Shader* const pShader) { m_shader = pShader; }
	void set_shader_param(const ShaderParamComponent& inParam);
	const ShaderParamComponent* shader_param_component(const String& name);

private:

	Shader* m_shader;
	ECullMode m_cull_override;
	EBlendMode m_blend_override = EBlendMode::None;
	std::vector<ShaderParamComponent> m_shader_params;
};

/// RenderComponent
class RenderComponent : public TransformComponent {
	BLK_DECLARE_COMPONENT(RenderComponent, TransformComponent);

public:
	virtual ~RenderComponent();

	virtual void editor_change(const std::string& propertyName) override;
	virtual void post_load() override;

	bool casts_shadow() const { return m_casts_shadow; }

	void set_material_param_vec4(const u32 idx, const String paramName, const Vec4& paramValue);
	void set_material_param_texture(const u32 idx, String paramName, Texture* const pTexture);
	void set_material_param_texture(const u32 idx, String paramName, RenderTexture* const pTexture);
	const ShaderParamComponent* shader_param_component(const int idx, const String& name);

	void refresh_materials(const bool bUpdateRenderObject);

	float render_order_bias() const { return m_render_order_bias; }
	void set_render_order_bias(const float newBias) {
		m_render_order_bias = newBias;
		refresh_materials(true);
	}

	void set_materials(const std::vector<MaterialComponent>& materialList) { m_materials = materialList; }

	enum ERenderPass render_pass() const { return m_render_pass; }
	void set_render_pass(const ERenderPass newPass) { m_render_pass = newPass; }

	const std::vector<MaterialComponent>& materials() const { return m_materials; }
	void copy_materials(const std::vector<MaterialComponent>& matComp) { m_materials = matComp; }

protected:
	ERenderPass m_render_pass;
	float m_render_order_bias;

	std::vector<MaterialComponent> m_materials;

	RenderObject m_render_object;

	bool m_casts_shadow;
};