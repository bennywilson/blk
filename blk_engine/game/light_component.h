/// LightComponent.h
///
/// 2016 blk

#pragma once

#include "component.h"

/// LightComponent
class LightComponent : public GameComponent {
	BLK_DECLARE_COMPONENT(LightComponent, GameComponent);

public:
	virtual	~LightComponent();

	virtual void post_load() override;

	virtual void render_sync();

	virtual void editor_change(const std::string& propertyName) override;

	void set_color(const Color& newColor) { m_color = newColor; }
	void set_color(const f32 R, const f32 G, f32 B, f32 A) { m_color.set(R, G, B, A); }

	f32 brightness() const { return m_brightness; }
	const Color& GetColor() const { return m_color; }
	virtual f32	radius() const { return 0.f; }
	virtual f32	length() const { return 0.f; }

	bool casts_shadow() const { return m_casts_shadow; }

	const std::vector<MaterialComponent>& materials() const { return m_materials; }

protected:
	void refresh_materials();

	virtual void enable_internal(const bool isEnabled) override;
	virtual void update_internal(const f32 DeltaTime) override;

	std::vector<MaterialComponent> m_materials;

	Color	m_color;
	f32 m_brightness;
	bool m_casts_shadow;
	bool m_bShaderParamsDirty;
};

/// PointLightComponent
class PointLightComponent : public LightComponent {
	BLK_DECLARE_COMPONENT(PointLightComponent, LightComponent);

public:
	virtual f32 radius() const override { return m_radius; }
	virtual void update_internal(const f32 dt) override;

protected:
	f32 m_radius;
	Vec3 vel;
};

/// CylindricalLightComponent
class CylindricalLightComponent : public PointLightComponent {
	BLK_DECLARE_COMPONENT(CylindricalLightComponent, PointLightComponent);

public:
	virtual f32	length() const override { return m_length; }

protected:
	f32	m_length;
};

/// DirectionalLightComponent
class DirectionalLightComponent : public LightComponent {
	BLK_DECLARE_COMPONENT(DirectionalLightComponent, LightComponent);

public:
	virtual	~DirectionalLightComponent();

	virtual void editor_change(const std::string& propertyName) override;
	const std::vector<f32>& cascade_start_distances() const { return m_cascade_start_distances; }

protected:
	std::vector<f32> m_cascade_start_distances;
};

/// LightShaftsComponent
class LightShaftsComponent : public GameComponent {
	BLK_DECLARE_COMPONENT(LightShaftsComponent, GameComponent);

public:
	virtual ~LightShaftsComponent();

	Texture* texture() const { return m_Texture; }
	const Color& GetColor() const { return m_Color; }
	f32 GetBaseWidth() const { return m_BaseWidth; }
	f32 GetBaseHeight() const { return m_BaseHeight; }
	f32 GetIterationWidth() const { return m_IterationWidth; }
	f32 GetIterationHeight() const { return m_IterationHeight; }
	int	GetNumIterations() const { return m_NumIterations; }
	bool IsDirectional() const { return m_Directional; }

	void SetColor(const Color& newColor);

protected:
	virtual void enable_internal(const bool isEnabled) override;
	virtual void update_internal(const float DeltaTime) override;

	Texture* m_Texture;
	Color	m_Color;
	f32 m_BaseWidth;
	f32 m_BaseHeight;
	f32 m_IterationWidth;
	f32 m_IterationHeight;
	i32 m_NumIterations;
	bool m_Directional;
};

/// FogComponent
class FogComponent : public GameComponent {
	BLK_DECLARE_COMPONENT(FogComponent, GameComponent);

public:
	void SetColor(const Color& newColor) { m_Color = newColor; }

protected:
	virtual void update_internal(const float DeltaTime) override;

	Color	m_Color;
	f32 m_StartDistance;
	f32 m_EndDistance;
};
