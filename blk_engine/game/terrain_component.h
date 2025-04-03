/// terrain_component.h
///
/// 2016-2025 blk 1.0

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
	int	m_GrassCellsPerTerrainSide;

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
	f32 m_GrassCellLength;

	struct grassRenderObject_t {
		grassRenderObject_t() : m_model(nullptr), m_pComponent(nullptr) { }

		void Initialize(const Vec3& ownerPosition);
		void Shutdown();

		kbModel* m_model;
		kbGameComponent* m_pComponent;
		kbRenderObject m_render_object;
	};
	std::vector<grassRenderObject_t> m_GrassRenderObjects;

	kbShaderParamOverrides_t m_GrassShaderOverrides;

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

	void SetHeightMap(Texture* const texture) { m_height_map = texture; }

	virtual void editor_change(const std::string& propertyName) override;

	virtual void render_sync() override;

	Texture* GetHeightMap() const { return m_height_map; }
	float GetHeightScale() const { return m_HeightScale; }
	float GetTerrainWidth() const { return m_TerrainWidth; }

	void SetCollisionMap(const kbRenderTexture* const texture);

	static void	SetTerrainLOD(const f32 lod);

	void RegenerateTerrain() { m_bRegenerateTerrain = true; }

	const std::vector<kbGrassZone> GetGrassZones() const { return m_GrassZones; }

	const kbModel& model() const {
		return m_model;
	}

protected:
	virtual void enable_internal(const bool isEnabled) override;
	virtual void update_internal(const f32 DeltaTime) override;

	void refresh_materials();

private:
	void GenerateTerrain();

protected:
	// Editor properties
	Texture* m_height_map;
	f32 m_HeightScale;
	f32 m_TerrainWidth;
	i32 m_TerrainDimensions;
	i32	m_TerrainSmoothAmount;

	Texture* m_pSplatMap;
	std::vector<kbGrass> m_Grass;
	std::vector<kbGrassZone> m_GrassZones;

	bool m_bDebugForceRegenTerrain;

	// Non-editor
	kbModel	m_model;
	f32 m_LastHeightMapLoadTime;

	f32 m_bRegenerateTerrain;
};

extern bool g_bCullGrass;
