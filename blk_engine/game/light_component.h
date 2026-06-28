/// LightComponent.h
///
/// 2016-2025 blk 1.0

#pragma once

#include "component.h"

/// LightComponent
class LightComponent : public kbGameComponent {
	KB_DECLARE_COMPONENT(LightComponent, kbGameComponent);

public:
	virtual	~LightComponent();

	virtual void post_load() override;

	virtual void render_sync();

	virtual void editor_change(const std::string& propertyName) override;

	void set_color(const kbColor& newColor) { m_color = newColor; }
	void set_color(const f32 R, const f32 G, f32 B, f32 A) { m_color.set(R, G, B, A); }

	f32 brightness() const { return m_brightness; }
	const kbColor& GetColor() const { return m_color; }
	virtual f32	radius() const { return 0.f; }
	virtual f32	length() const { return 0.f; }

	bool casts_shadow() const { return m_casts_shadow; }

	const std::vector<kbMaterialComponent>& materials() const { return m_materials; }

protected:
	void refresh_materials();

	virtual void enable_internal(const bool isEnabled) override;
	virtual void update_internal(const f32 DeltaTime) override;

	std::vector<kbMaterialComponent> m_materials;

	kbColor	m_color;
	f32 m_brightness;
	bool m_casts_shadow;
	bool m_bShaderParamsDirty;
};

/// kbPointLightComponent
class kbPointLightComponent : public LightComponent {
	KB_DECLARE_COMPONENT(kbPointLightComponent, LightComponent);

public:
	virtual f32 radius() const override { return m_radius; }
	virtual void update_internal(const f32 dt) override;

protected:
	f32 m_radius;
	Vec3 vel;
};

/// kbCylindricalLightComponent
class kbCylindricalLightComponent : public kbPointLightComponent {
	KB_DECLARE_COMPONENT(kbCylindricalLightComponent, kbPointLightComponent);

public:
	virtual f32	length() const override { return m_length; }

protected:
	f32	m_length;
};

/// kbDirectionalLightComponent
class kbDirectionalLightComponent : public LightComponent {
	KB_DECLARE_COMPONENT(kbDirectionalLightComponent, LightComponent);

public:
	virtual	~kbDirectionalLightComponent();

	virtual void editor_change(const std::string& propertyName) override;
	const std::vector<f32>& cascade_start_distances() const { return m_cascade_start_distances; }

protected:
	std::vector<f32> m_cascade_start_distances;
};

/// kbLightShaftsComponent
class kbLightShaftsComponent : public kbGameComponent {
	KB_DECLARE_COMPONENT(kbLightShaftsComponent, kbGameComponent);

public:
	virtual ~kbLightShaftsComponent();

	Texture* texture() const { return m_Texture; }
	const kbColor& GetColor() const { return m_Color; }
	f32 GetBaseWidth() const { return m_BaseWidth; }
	f32 GetBaseHeight() const { return m_BaseHeight; }
	f32 GetIterationWidth() const { return m_IterationWidth; }
	f32 GetIterationHeight() const { return m_IterationHeight; }
	int	GetNumIterations() const { return m_NumIterations; }
	bool IsDirectional() const { return m_Directional; }

	void SetColor(const kbColor& newColor);

protected:
	virtual void enable_internal(const bool isEnabled) override;
	virtual void update_internal(const float DeltaTime) override;

	Texture* m_Texture;
	kbColor	m_Color;
	f32 m_BaseWidth;
	f32 m_BaseHeight;
	f32 m_IterationWidth;
	f32 m_IterationHeight;
	i32 m_NumIterations;
	bool m_Directional;
};

/// kbFogComponent
class kbFogComponent : public kbGameComponent {
	KB_DECLARE_COMPONENT(kbFogComponent, kbGameComponent);

public:
	void SetColor(const kbColor& newColor) { m_Color = newColor; }

protected:
	virtual void update_internal(const float DeltaTime) override;

	kbColor	m_Color;
	f32 m_StartDistance;
	f32 m_EndDistance;
};
