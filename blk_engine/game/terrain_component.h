/// terrain_component.h
///
/// 2016 blk

#pragma once

#include "model.h"

///	Grass
class Grass : public GameComponent {
	friend class TerrainComponent;
	BLK_DECLARE_COMPONENT(Grass, GameComponent);

public:
	~Grass();

	virtual void editor_change(const std::string& propertyName) override;
	virtual void render_sync() override;

protected:
	virtual void enable_internal(const bool isEnabled) override;

private:
	void SetOwningTerrainComponent(TerrainComponent* const pTerrain) {
		m_pOwningTerrainComponent = pTerrain;
		m_bUpdateMaterial = true;
		m_bUpdatePointCloud = true;
	}

	void RefreshGrass();

	BLK_PROPERTY()
	Shader* m_pGrassShader;

	BLK_PROPERTY(MinVal = 1)
	int m_grassCellsPerTerrainSide;

	BLK_PROPERTY()
	std::vector<ShaderParamComponent> m_ShaderParams;

	BLK_PROPERTY()
	f32 m_PatchStartCullDistance;

	BLK_PROPERTY()
	f32 m_PatchEndCullDistance;

	BLK_PROPERTY()
	i32 m_PatchesPerCellSide;

	BLK_PROPERTY()
	f32 m_BladeMinWidth;

	BLK_PROPERTY()
	f32 m_BladeMaxWidth;

	BLK_PROPERTY()
	f32 m_BladeMinHeight;

	BLK_PROPERTY()
	f32 m_BladeMaxHeight;

	BLK_PROPERTY()
	f32 m_MaxPatchJitterOffset;

	BLK_PROPERTY()
	f32 m_MaxBladeJitterOffset;

	BLK_PROPERTY()
	f32 m_FakeAODarkness;

	BLK_PROPERTY()
	f32 m_FakeAOPower;

	BLK_PROPERTY()
	f32 m_FakeAOClipPlaneFadeStartDist;

private:
	// Editor
	f32 m_grassCellLength;

	struct grassRenderObject_t {
		grassRenderObject_t() :
			m_model(nullptr), m_pComponent(nullptr) {}

		void Initialize(const Vec3& ownerPosition);
		void Shutdown();

		Model* m_model;
		GameComponent* m_pComponent;
		RenderObject m_render_object;
	};
	std::vector<grassRenderObject_t> m_grassRenderObjects;

	ShaderParamOverrides_t m_grassShaderOverrides;

	// Runtime
	TerrainComponent* m_pOwningTerrainComponent;

	bool m_bUpdatePointCloud;
	bool m_bUpdateMaterial;
};

/// GrassZone
class GrassZone : public GameComponent {
	BLK_DECLARE_COMPONENT(GrassZone, GameComponent);

public:
	Vec3 GetCenter() const { return m_Center; }
	Vec3 GetExtents() const { return m_Extents; }

private:
	BLK_PROPERTY()
	Vec3 m_Center;

	BLK_PROPERTY()
	Vec3 m_Extents;
};


/// TerrainComponent
class TerrainComponent : public RenderComponent {
	BLK_DECLARE_COMPONENT(TerrainComponent, RenderComponent);

public:
	~TerrainComponent();

	virtual void post_load() override;

	virtual void editor_change(const std::string& propertyName) override;

	const Model& model() const {
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
	BLK_PROPERTY()
	Texture* m_height_map;

	BLK_PROPERTY()
	f32 m_height_scale;

	BLK_PROPERTY()
	f32 m_world_width;

	BLK_PROPERTY()
	i32 m_vertex_dimensions;

	BLK_PROPERTY()
	i32 m_terrain_smooth_filter_width;

	BLK_PROPERTY()
	Texture* m_splat_map;

	BLK_PROPERTY()
	std::vector<Grass> m_grass;

	BLK_PROPERTY()
	std::vector<GrassZone> m_grass_zones;

	BLK_PROPERTY()
	bool m_debug_force_gen_terrain;

	// Non-editor
	Model m_model;
	f32 m_last_load_time;
};

extern bool g_bCullGrass;
