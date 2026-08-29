/// terrain_component.h
///
/// 2016 blk

#pragma once

#include "model.h"

///	kbGrass
class kbGrass : public kbGameComponent {
	friend class TerrainComponent;
	KB_DECLARE_COMPONENT(kbGrass, kbGameComponent);

public:
	~kbGrass();

	virtual void editor_change(const std::string& propertyName) override;
	virtual void render_sync() override;

protected:
	virtual void enable_internal(const bool isEnabled) override;

private:
	void SetOwningTerrainComponent(TerrainComponent* const pTerrain) { m_pOwningTerrainComponent = pTerrain; m_bUpdateMaterial = true; m_bUpdatePointCloud = true; }

	void RefreshGrass();

	kbShader* m_pGrassShader;
	int	m_grassCellsPerTerrainSide;

	std::vector<kbShaderParamComponent> m_ShaderParamList;

	f32 m_PatchStartCullDistance;
	f32 m_PatchEndCullDistance;

	i32	m_PatchesPerCellSide;

	f32 m_BladeMinWidth;
	f32 m_BladeMaxWidth;

	f32 m_BladeMinHeight;
	f32 m_BladeMaxHeight;

	f32 m_MaxPatchJitterOffset;
	f32 m_MaxBladeJitterOffset;

	f32 m_FakeAODarkness;
	f32 m_FakeAOPower;
	f32 m_FakeAOClipPlaneFadeStartDist;

private:
	// Editor
	f32 m_grassCellLength;

	struct grassRenderObject_t {
		grassRenderObject_t() : m_model(nullptr), m_pComponent(nullptr) { }

		void Initialize(const Vec3& ownerPosition);
		void Shutdown();

		kbModel* m_model;
		kbGameComponent* m_pComponent;
		kbRenderObject m_render_object;
	};
	std::vector<grassRenderObject_t> m_grassRenderObjects;

	kbShaderParamOverrides_t m_grassShaderOverrides;

	// Runtime
	TerrainComponent* m_pOwningTerrainComponent;

	bool m_bUpdatePointCloud;
	bool m_bUpdateMaterial;
};

/// kbGrassZone
class kbGrassZone : public kbGameComponent {
	KB_DECLARE_COMPONENT(kbGrassZone, kbGameComponent);

public:
	Vec3 GetCenter() const { return m_Center; }
	Vec3 GetExtents() const { return m_Extents; }

private:
	Vec3 m_Center;
	Vec3 m_Extents;
};


/// TerrainComponent
class TerrainComponent : public RenderComponent {
	KB_DECLARE_COMPONENT(TerrainComponent, RenderComponent);

public:
	~TerrainComponent();

	virtual void post_load() override;

	virtual void editor_change(const std::string& propertyName) override;

	const kbModel& model() const {
		return m_model;
	}

	const Texture* splat_map() const {
		return m_splat_map;
	}
protected:
	virtual void enable_internal(const bool isEnabled) override;
	void generate_terrain();

protected:
	// Editor properties
	Texture* m_height_map;
	f32 m_height_scale;
	f32 m_world_width;
	i32 m_vertex_dimensions;
	i32	m_terrain_smooth_filter_width;

	Texture* m_splat_map;
	std::vector<kbGrass> m_grass;
	std::vector<kbGrassZone> m_grass_zones;

	bool m_debug_force_gen_terrain;

	// Non-editor
	kbModel	m_model;
	f32 m_last_load_time;
};

extern bool g_bCullGrass;
