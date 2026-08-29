/// TerrainComponent.cpp
///
/// 2016 blk

#include "blk_core.h"
#include "Matrix.h"
#include "Quaternion.h"
#include "entity_header.h"
#include "terrain_component.h"
#include "game.h"
#include "renderer_dx12.h"

KB_DEFINE_COMPONENT(TerrainComponent)

static float g_TerrainLOD = 1.0f;
bool g_bCullGrass = false;

struct patchVertLayout {
	Vec3 position;
	Vec2 uv;
	byte patchIndices[4];
};

struct debugNormal {
	Vec3 normal;
	Vec3 position;
};
std::vector<debugNormal> terrainNormals;

/// grassRenderObject_t::Initialize
void kbGrass::grassRenderObject_t::Initialize(const Vec3& ownerPosition) {
	blk::error_check(m_model == nullptr && m_pComponent == nullptr, "grassRenderObject_t::Initialize() - m_model or m_pComponent is not NULL");

	m_model = new kbModel();
	m_pComponent = new kbGameComponent();

	m_render_object.m_pComponent = m_pComponent;
	m_render_object.m_model = m_model;
	m_render_object.m_render_pass = ERenderPass::RP_Lighting;
	m_render_object.m_position = ownerPosition;
	m_render_object.m_rotation.set(0.0f, 0.0f, 0.0f, 1.0f);
	m_render_object.m_Scale.set(1.0f, 1.0f, 1.0f);
	//	m_render_object.m_EntityId
	//	m_render_object.m_MatrixList
	m_render_object.m_casts_shadow = false;
}

/// grassRenderObject_t::Shutdown
void kbGrass::grassRenderObject_t::Shutdown() {
	blk::error_check(m_model != nullptr && m_pComponent != nullptr, "grassRenderObject_t::Initialize() - m_model or m_pComponent is not NULL");

	delete m_pComponent;
	m_pComponent = nullptr;

	delete m_model;
	m_model = nullptr;
}

/// kbGrass::Constructor
void kbGrass::Constructor() {

	m_pGrassShader = nullptr;

	m_grassCellsPerTerrainSide = 1;
	m_grassCellLength = 0;

	m_PatchStartCullDistance = 200.0f;
	m_PatchEndCullDistance = 300.0f;

	m_PatchesPerCellSide = 3;

	m_BladeMinWidth = 1.0f;
	m_BladeMaxWidth = 2.0f;

	m_BladeMinHeight = 5.0f;
	m_BladeMaxHeight = 10.0f;

	m_MaxBladeJitterOffset = 0.0f;
	m_MaxPatchJitterOffset = 0.0f;

	m_pOwningTerrainComponent = nullptr;

	m_bUpdateMaterial = false;
	m_bUpdatePointCloud = false;

	m_FakeAODarkness = 0.25f;
	m_FakeAOPower = 2.0f;
	m_FakeAOClipPlaneFadeStartDist = 0.0f;
}

/// kbGrass::~kbGrass
kbGrass::~kbGrass() {
	for (int i = 0; i < m_grassRenderObjects.size(); i++) {
		m_grassRenderObjects[i].Shutdown();
	}
}

/// kbGrass::EditorChange
void kbGrass::editor_change(const std::string& propertyName) {
	Super::editor_change(propertyName);

	if (m_grassCellsPerTerrainSide < 0) {
		blk::warn("kbGrass::editor_change() - Grass Cells Per Terrain Side must be greater than 0");
		m_grassCellsPerTerrainSide = 1;
	}

	const std::string propertiesThatRegenGrass[7] = { "PatchStartCullDistance", "PatchEndCullDistance",
		"PatchesPerCellSide", "MaxPatchJitterOffset", "MaxBladeJitterOffset", "MinPatchJitterOffset", "GrassCellsPerTerrainSide" };

	for (int i = 0; i < 7; i++) {
		if (propertyName == propertiesThatRegenGrass[i]) {
			m_bUpdatePointCloud = true;
		}
	}
	m_bUpdateMaterial = true;
}

/// kbGrass::RenderSync
void kbGrass::render_sync() {
	Super::render_sync();

	if (m_bUpdateMaterial || m_bUpdatePointCloud) {
		RefreshGrass();
	}
}

/// kbGrass::enable_internal
void kbGrass::enable_internal(const bool isEnabled) {
	Super::enable_internal(isEnabled);

	/*if (isEnabled) {

		for (int i = 0; i < m_grassRenderObjects.size(); i++) {
			g_pRenderer->AddRenderObject(m_grassRenderObjects[i].m_render_object);
		}

	} else {

		for (int i = 0; i < m_grassRenderObjects.size(); i++) {
			g_pRenderer->RemoveRenderObject(m_grassRenderObjects[i].m_render_object);
		}

	}*/
}

/// kbGrass::RefreshGrass
void kbGrass::RefreshGrass() {
}

/// TerrainComponent::Constructor
void TerrainComponent::Constructor() {
	m_height_map = nullptr;
	m_height_scale = 0.3f;
	m_world_width = 256.0f;
	m_vertex_dimensions = 16;
	m_terrain_smooth_filter_width = 1;
	m_debug_force_gen_terrain = false;

	m_splat_map = nullptr;

	m_last_load_time = -1.0f;
}

/// TerrainComponent::TerrainComponent
TerrainComponent::~TerrainComponent() {
	if (m_height_map) {
		m_height_map->release();
		m_height_map = nullptr;
	}

	m_model.release();
}

/// TerrainComponent::PostLoad
void TerrainComponent::post_load() {
	Super::post_load();

	for (int i = 0; i < m_grass.size(); i++) {
		m_grass[i].SetOwningTerrainComponent(this);
	}
}

/// TerrainComponent::EditorChange
void TerrainComponent::editor_change(const std::string& propertyName) {
	Super::editor_change(propertyName);

	const std::string propertiesThatRegenTerrain[5] = { "HeightMap", "HeightScale", "Width", "Dimensions", "SmoothAmount" };

	for (int i = 0; i < 5; i++) {
		if (propertyName == propertiesThatRegenTerrain[i]) {
			generate_terrain();
		}
	}
}

/// TerrainComponent::generate_terrain
void TerrainComponent::generate_terrain() {
	blk::error_check(
		m_height_map != nullptr,
		"TerrainComponent::GenerateTerrain() - No height map file found for terrain component on entity %s", GetOwner()->name().c_str()
	);

	terrainNormals.clear();

	u32 tex_width, tex_height;
	const std::vector<Vec4>& height_data = m_height_map->cpu_texture(tex_width, tex_height);

	// Build terrain here
	const i32 numVerts = m_vertex_dimensions * m_vertex_dimensions;
	const u32 numIndices = (m_vertex_dimensions - 1) * (m_vertex_dimensions - 1) * 6;
	const f32 HalfTerrainWidth = m_world_width * 0.5f;
	const f32 stepSize = m_world_width / (f32)tex_width;
	const f32 cellWidth = m_world_width / (f32)m_vertex_dimensions;

	m_model.create_dynamic(numVerts, numIndices);

	// Vertex Buffer
	vertexLayout* const pVerts = (vertexLayout*)m_model.map_vertex_buffer();
	std::vector<Vec3> cpuVerts;
	cpuVerts.resize((size_t)m_vertex_dimensions * m_vertex_dimensions);

	i32 curr_vert = 0;
	for (i32 start_y = 0; start_y < m_vertex_dimensions; start_y++) {
		for (i32 start_x = 0; start_x < m_vertex_dimensions; start_x++) {
			f32 divisor = 0.0f;
			f32 height = 0.0f;
			const i32 blurSampleSize = max(m_terrain_smooth_filter_width, 1);
			for (i32 tempY = 0; tempY < blurSampleSize; tempY++) {
				if (tempY + start_y >= m_vertex_dimensions) {
					break;
				}

				for (i32 tempX = 0; tempX < blurSampleSize; tempX++) {
					if (tempX + start_x >= m_vertex_dimensions) {
						break;
					}

					const f32 u = (f32)(start_x + tempX) / (f32)m_vertex_dimensions;
					const f32 v = (f32)(start_y + tempY) / (f32)m_vertex_dimensions;
					const i32 textureIndex = static_cast<i32>((v * tex_width * tex_width) + (u * tex_width));

					divisor += 1.0f;
					height += (f32)height_data[textureIndex].r;
				}
			}

			height *= m_height_scale / divisor;
			cpuVerts[curr_vert] = Vec3(-HalfTerrainWidth + ((start_x + 1) * cellWidth), height, -HalfTerrainWidth + ((start_y + 1) * cellWidth));

			pVerts[curr_vert].Clear();
			pVerts[curr_vert].position = cpuVerts[curr_vert];
			pVerts[curr_vert].uv.set((f32)(start_x) / (f32)m_vertex_dimensions, (f32)(start_y) / (f32)m_vertex_dimensions);
			pVerts[curr_vert].SetColor(Vec4(1.0f, 1.0f, 1.0f, 1.0f));
			pVerts[curr_vert].SetNormal(Vec4(0.0f, 1.0f, 0.0f, 0.0f));
			curr_vert++;
		}
	}

	for (i32 startY = 0; startY < m_vertex_dimensions; startY++) {
		for (i32 startX = 0; startX < m_vertex_dimensions; startX++) {
			i32 currentIndex = (startY * m_vertex_dimensions) + startX;

			Vec3 xVec, zVec;

			if (startX < m_vertex_dimensions - 1) {
				xVec = pVerts[currentIndex + 1].position - pVerts[currentIndex].position;
			} else {
				xVec = pVerts[currentIndex].position - pVerts[currentIndex - 1].position;
			}

			if (startY < m_vertex_dimensions - 1) {
				zVec = pVerts[currentIndex].position - pVerts[currentIndex + m_vertex_dimensions].position;
			} else {
				zVec = pVerts[currentIndex - m_vertex_dimensions].position - pVerts[currentIndex].position;
			}

			xVec.normalize_self();
			zVec.normalize_self();
			pVerts[currentIndex].SetNormal(xVec.cross(zVec).normalize_safe());
		/*	Vec3 finalVec = xVec.cross(zVec).normalize_safe();

			xVec = finalVec.cross(zVec).normalize_safe();
			zVec = xVec.cross(finalVec).normalize_safe();

			pVerts[currentIndex].SetTangent(-zVec);
			pVerts[currentIndex].SetBitangent(xVec);

			debugNormal newNormal;
			newNormal.normal = xVec;
			newNormal.position = pVerts[currentIndex].position + GetOwner()->position();
			terrainNormals.push_back(newNormal);

			newNormal.normal = zVec;
			newNormal.position = pVerts[currentIndex].position + GetOwner()->position();
			terrainNormals.push_back(newNormal);

			newNormal.normal = finalVec;
			newNormal.position = pVerts[currentIndex].position + GetOwner()->position();
			terrainNormals.push_back(newNormal);*/
		}
	}

	m_model.unmap_vertex_buffer();


	// Index Buffer

	u16* indices = (u16*)m_model.map_index_buffer();
	i32 next_write_idx = 0;
	for (i32 y = 0; y < m_vertex_dimensions - 1; y++) {
		for (i32 x = 0; x < m_vertex_dimensions - 1; x++) {
			const u32 cur_idx = (y * m_vertex_dimensions) + x;
			indices[next_write_idx + 2] = cur_idx;
			indices[next_write_idx + 1] = cur_idx + 1;
			indices[next_write_idx + 0] = cur_idx + m_vertex_dimensions;

			indices[next_write_idx + 5] = cur_idx + 1;
			indices[next_write_idx + 4] = cur_idx + 1 + m_vertex_dimensions;
			indices[next_write_idx + 3] = cur_idx + m_vertex_dimensions;
			next_write_idx += 6;
		}
	}

	m_model.unmap_index_buffer();
	// Update collision
	i32 collisionPatchSize = 8;

	std::vector<kbCollisionComponent::customTriangle_t> terrainCollision;
	terrainCollision.resize((size_t)((m_vertex_dimensions / collisionPatchSize) * (m_vertex_dimensions / collisionPatchSize)) * 2);

	size_t triIdx = 0;
	for (i32 y = 0; y < m_vertex_dimensions - collisionPatchSize; y += collisionPatchSize) {
		for (i32 x = 0; x < m_vertex_dimensions - collisionPatchSize; x += collisionPatchSize, triIdx += 2) {
			const size_t currentIndex = ((size_t)y * m_vertex_dimensions) + x;
			terrainCollision[triIdx + 0].m_Vertex1 = cpuVerts[currentIndex];
			terrainCollision[triIdx + 0].m_Vertex2 = cpuVerts[currentIndex + collisionPatchSize];
			terrainCollision[triIdx + 0].m_Vertex3 = cpuVerts[currentIndex + ((size_t)collisionPatchSize * m_vertex_dimensions)];

			terrainCollision[triIdx + 1].m_Vertex1 = cpuVerts[currentIndex + collisionPatchSize];
			terrainCollision[triIdx + 1].m_Vertex2 = cpuVerts[currentIndex + collisionPatchSize + ((size_t)m_vertex_dimensions * collisionPatchSize)];
			terrainCollision[triIdx + 1].m_Vertex3 = cpuVerts[currentIndex + ((size_t)collisionPatchSize * m_vertex_dimensions)];
		}
	}
}

/// TerrainComponent::enable_internal
void TerrainComponent::enable_internal(const bool isEnabled) {
	if (m_model.NumVertices() == 0) {
		generate_terrain();
	}

	if (m_last_load_time == -1.0f && m_height_map != nullptr) {
		m_last_load_time = m_height_map->last_load_time();
	}

	if (isEnabled) {
		if (g_renderer) {
			g_renderer->add_render_component(this);
		}

	} else {
		g_renderer->remove_render_component(this);
	}
}


/// kbGrassZone::Constructor
void kbGrassZone::Constructor() {
	m_Center.set(0.0f, 0.0f, 0.0f);
	m_Extents.set(100.0f, 100.0f, 100.0f);
}