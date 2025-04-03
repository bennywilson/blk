/// TerrainComponent.cpp
///
/// 2016-2025 blk 1.0

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

/// TerrainComponent::SetTerrainLOD
void TerrainComponent::SetTerrainLOD(const float lod) {
	g_TerrainLOD = lod;

	if (g_pGame == nullptr) {
		return;
	}

	const std::vector<GameEntity*>& gameEnts = g_pGame->GetGameEntities();
	for (int i = 0; i < gameEnts.size(); i++) {

		GameEntity* const pEnt = gameEnts[i];
		for (int iComp = 0; iComp < pEnt->num_components(); iComp++) {
			TerrainComponent* const pTerrain = pEnt->component(iComp)->GetAs<TerrainComponent>();
			if (pTerrain == nullptr) {
				continue;
			}

			pTerrain->RegenerateTerrain();
		}
	}

}

struct patchVertLayout {
	Vec3 position;
	Vec2 uv;
	byte patchIndices[4];
};

struct debugNormal
{
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

	m_GrassCellsPerTerrainSide = 1;
	m_GrassCellLength = 0;

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
	for (int i = 0; i < m_GrassRenderObjects.size(); i++) {
		m_GrassRenderObjects[i].Shutdown();
	}
}

/// kbGrass::EditorChange
void kbGrass::editor_change(const std::string& propertyName) {
	Super::editor_change(propertyName);

	if (m_GrassCellsPerTerrainSide < 0) {
		blk::warn("kbGrass::editor_change() - Grass Cells Per Terrain Side must be greater than 0");
		m_GrassCellsPerTerrainSide = 1;
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

		for (int i = 0; i < m_GrassRenderObjects.size(); i++) {
			g_pRenderer->AddRenderObject(m_GrassRenderObjects[i].m_render_object);
		}

	} else {

		for (int i = 0; i < m_GrassRenderObjects.size(); i++) {
			g_pRenderer->RemoveRenderObject(m_GrassRenderObjects[i].m_render_object);
		}

	}*/
}

/// kbGrass::RefreshGrass
void kbGrass::RefreshGrass() {
/*
	const float startRefreshGrassTime = g_GlobalTimer.TimeElapsedSeconds();

	std::vector<Vec4> bladeOffsets;

	const float PatchesPerCellSide = kbClamp((float)m_PatchesPerCellSide, 1.0f, 99999999.0f);

	//float grassCellHalfSize = ( m_DistanceBetweenPatches / 2.0f ) * 0.95f;
	for (int i = 0; i < 64; i++) {

		Mat4 matrix = Mat4::identity;
		const float angle = 2.0f * kbfrand() * kbPI;
		float cosPIOver2 = cos(angle);
		float sinPIOver2 = sin(angle);
		matrix[0][0] = cosPIOver2;
		matrix[2][0] = -sinPIOver2;
		matrix[0][2] = sinPIOver2;
		matrix[2][2] = cosPIOver2;

		Vec4 startVec(0.0f, 0.0f, 1.0f, 0.0f);
		startVec = startVec.transform_point(matrix);

		Vec4 offset;
		offset.x = startVec.x;
		offset.y = startVec.z;
		offset.z = m_MaxBladeJitterOffset * kbfrand();
		offset.w = m_MaxBladeJitterOffset * kbfrand();
		bladeOffsets.push_back(offset);
	}

	m_GrassCellLength = m_pOwningTerrainComponent->GetTerrainWidth() / (float)m_GrassCellsPerTerrainSide;
	const float patchLen = m_GrassCellLength / (float)PatchesPerCellSide;

	m_GrassShaderOverrides.m_ParamOverrides.clear();
	if (m_pGrassShader != nullptr) {
		m_GrassShaderOverrides.m_shader = m_pGrassShader;
	} else {
		m_GrassShaderOverrides.m_shader = (kbShader*)g_ResourceManager.GetResource("./assets/Shaders/Environment/grass.kbshader", true, true);
	}

	m_GrassShaderOverrides.SetTexture("heightMap", m_pOwningTerrainComponent->GetHeightMap());
	m_GrassShaderOverrides.SetVec4List("bladeOffsets", bladeOffsets);
	m_GrassShaderOverrides.SetVec4("GrassData0", Vec4(m_PatchStartCullDistance, 1.0f / (m_PatchEndCullDistance - m_PatchStartCullDistance), m_BladeMinHeight, m_BladeMaxHeight));
	m_GrassShaderOverrides.SetVec4("GrassData1", Vec4(m_pOwningTerrainComponent->GetHeightScale(), m_pOwningTerrainComponent->GetOwner()->position().y, patchLen, 0.0f));
	m_GrassShaderOverrides.SetVec4("fakeAOData", Vec4(m_FakeAODarkness, m_FakeAOPower, m_FakeAOClipPlaneFadeStartDist, 0.0f));

	for (int i = 0; i < m_ShaderParamList.size(); i++) {
		if (m_ShaderParamList[i].param_name().stl_str().empty()) {
			continue;
		}

		if (m_ShaderParamList[i].texture() != nullptr) {
			m_GrassShaderOverrides.SetTexture(m_ShaderParamList[i].param_name().stl_str(), m_ShaderParamList[i].texture());
		} else {
			m_GrassShaderOverrides.SetVec4(m_ShaderParamList[i].param_name().stl_str(), m_ShaderParamList[i].vector());
		}
	}

	const Vec2 collisionMapPos = Vec2(m_pOwningTerrainComponent->GetOwner()->position().x, m_pOwningTerrainComponent->GetOwner()->position().z);
	m_GrassShaderOverrides.SetVec4("collisionMapCenter", Vec4(collisionMapPos.x, collisionMapPos.y, m_pOwningTerrainComponent->GetTerrainWidth() * 0.5f, 1.0f / (m_pOwningTerrainComponent->GetTerrainWidth() * 0.5f)));

	struct pixelData {
		byte r;
		byte g;
		byte b;
		byte a;
	};
	static const kbString skGrassMaskMap("grassMaskMap");
	const pixelData* pGrassMaskMap = nullptr;
	uint grassMaskWidth = 0, grassMaskHeight = 0;

	for (size_t i = 0; i < m_ShaderParamList.size(); i++) {
		kbShaderParamComponent& curParam = m_ShaderParamList[i];
		if (curParam.texture() == nullptr) {
			continue;
		}

		if (curParam.param_name() != skGrassMaskMap) {
			continue;
		}

		Texture* const pTex = const_cast<Texture*>(curParam.texture());		// cpu_texture() is not a const function as it modifies internal state
		pGrassMaskMap = (pixelData*)pTex->cpu_texture(grassMaskWidth, grassMaskHeight);
		break;
	}


	if (m_bUpdatePointCloud) {
		for (int i = 0; i < m_GrassRenderObjects.size(); i++) {
			g_pRenderer->RemoveRenderObject(m_GrassRenderObjects[i].m_render_object);
			m_GrassRenderObjects[i].Shutdown();
		}
		m_GrassRenderObjects.clear();

		m_GrassRenderObjects.insert(m_GrassRenderObjects.begin(), (size_t)(m_GrassCellsPerTerrainSide * m_GrassCellsPerTerrainSide), grassRenderObject_t());
		const float halfCellLen = m_GrassCellLength * 0.5f;
		const float halfCellLenSqr = sqrt(halfCellLen * halfCellLen);

		const float halfTerrainWidth = m_pOwningTerrainComponent->GetTerrainWidth() * 0.5f;
		const Vec3 terrainMin = -Vec3(halfTerrainWidth, 0.0f, halfTerrainWidth);

		const Mat4 ownerRot = m_pOwningTerrainComponent->GetOwner()->rotation().to_mat4();
		const Vec3 ownerPos = m_pOwningTerrainComponent->GetOwner()->position();
		const auto& grassZones = m_pOwningTerrainComponent->GetGrassZones();

		int cellIdx = 0;
		for (int yCell = 0; yCell < m_GrassCellsPerTerrainSide; yCell++) {
			for (int xCell = 0; xCell < m_GrassCellsPerTerrainSide; xCell++, cellIdx++) {

				grassRenderObject_t& renderObj = m_GrassRenderObjects[cellIdx];
				renderObj.Initialize(m_pOwningTerrainComponent->GetOwner()->position());
				renderObj.m_render_object.m_CullDistance = m_PatchEndCullDistance + halfCellLenSqr;

				const Vec3 cellStart = terrainMin + Vec3(m_GrassCellLength * xCell, 0.0f, m_GrassCellLength * yCell);
				const Vec3 cellCenter = cellStart + Vec3(m_GrassCellLength * 0.5f, 0.0f, m_GrassCellLength * 0.5f);
				const Vec3 halfCell = Vec3(m_GrassCellLength * 0.5f, 0.0f, m_GrassCellLength * 0.5f);

				patchVertLayout* pVerts = nullptr;
				bool bCreatedPointCloud = false;

				int iVert = 0;
				for (int startY = 0; startY < PatchesPerCellSide; startY++) {
					for (int startX = 0; startX < PatchesPerCellSide; startX++) {

						const Vec3 patchJitterOffset = Vec3(m_MaxPatchJitterOffset * kbfrand(), 0.0f, m_MaxPatchJitterOffset * kbfrand());
						const Vec3 globalPointPos = cellStart + Vec3(patchLen * startX, 0.0f, patchLen * startY) + patchJitterOffset;
						const float curU = kbSaturate((globalPointPos.x - terrainMin.x) / m_pOwningTerrainComponent->GetTerrainWidth());
						const float curV = kbSaturate((globalPointPos.z - terrainMin.z) / m_pOwningTerrainComponent->GetTerrainWidth());

						if (g_bCullGrass) {
							bool bSkipIt = true;
							const Vec3 pointWorldPos = ownerRot.transform_point(globalPointPos) + ownerPos;
							for (int i = 0; i < grassZones.size(); i++) {

								Vec3 boundsCenter = ownerRot.transform_point(grassZones[i].GetCenter()) + ownerPos;
								Vec3 boundsExtent = grassZones[i].GetExtents();

								const Vec3 boundsMin = boundsCenter - boundsExtent;
								const Vec3 boundsMax = boundsCenter + boundsExtent;
								const kbBounds grassBounds = kbBounds(boundsMin, boundsMax);
								if (grassBounds.ContainsPoint(pointWorldPos)) {
									bSkipIt = false;
									break;
								}
							}

							if (bSkipIt) {
								continue;
							}
						}

						if (pGrassMaskMap != nullptr) {

							const int textureIndex = static_cast<int>(((int)(curV * grassMaskWidth) * grassMaskWidth) + (curU * grassMaskWidth));
							if (pGrassMaskMap[textureIndex].g == 0) {
								continue;
							}
						}

						if (bCreatedPointCloud == false) {
							renderObj.m_model->CreatePointCloud(m_PatchesPerCellSide * m_PatchesPerCellSide, "./assets/Shaders/Environment/grass.kbshader", CullMode_None, sizeof(patchVertLayout));

							pVerts = (patchVertLayout*)renderObj.m_model->MapVertexBuffer();
							bCreatedPointCloud = true;
						}

						Vec3 localPointPos = patchJitterOffset + Vec3(patchLen * startX, 0.0f, patchLen * startY) - halfCell;
						pVerts[iVert].position = localPointPos;

						pVerts[iVert].uv.set(curU, curV);
						pVerts[iVert].patchIndices[0] = rand() % 60;		// Randomized blade jitters
						pVerts[iVert].patchIndices[1] = pVerts[iVert].patchIndices[2] = pVerts[iVert].patchIndices[3] = pVerts[iVert].patchIndices[0];
						const float randVal = kbfrand();

						pVerts[iVert].patchIndices[2] = 0;		// Unused I believe
						iVert++;
					}
				}
				if (bCreatedPointCloud) {
					renderObj.m_model->UnmapVertexBuffer(iVert);
					Mat4 rotMat = m_pOwningTerrainComponent->owner_rotation().to_mat4();
					m_GrassRenderObjects[cellIdx].m_render_object.m_position = cellCenter * rotMat + m_pOwningTerrainComponent->owner_position();
					m_GrassRenderObjects[cellIdx].m_render_object.m_Scale = m_pOwningTerrainComponent->owner_scale();
					m_GrassRenderObjects[cellIdx].m_render_object.m_Orientation = m_pOwningTerrainComponent->owner_rotation();

					auto& renderObjMatList = m_GrassRenderObjects[cellIdx].m_render_object.m_Materials;
					renderObjMatList.clear();
					renderObjMatList.push_back(m_GrassShaderOverrides);
					g_pRenderer->AddRenderObject(m_GrassRenderObjects[cellIdx].m_render_object);
				}
			}
		}
	} else {

		for (int i = 0; i < m_GrassRenderObjects.size(); i++) {
			auto& renderObjMatList = m_GrassRenderObjects[i].m_render_object.m_Materials;
			renderObjMatList.clear();
			renderObjMatList.push_back(m_GrassShaderOverrides);
			g_pRenderer->UpdateRenderObject(m_GrassRenderObjects[i].m_render_object);
		}
	}

	m_bUpdateMaterial = false;
	m_bUpdatePointCloud = false;

	blk::log("Refreshing grass took %f seconds.", g_GlobalTimer.TimeElapsedSeconds() - startRefreshGrassTime);*/
}

/// TerrainComponent::Constructor
void TerrainComponent::Constructor() {
	m_height_map = nullptr;
	m_HeightScale = 0.3f;
	m_TerrainWidth = 256.0f;
	m_TerrainDimensions = 16;
	m_TerrainSmoothAmount = 1;
	m_bDebugForceRegenTerrain = false;

	m_pSplatMap = nullptr;

	m_LastHeightMapLoadTime = -1.0f;
	m_bRegenerateTerrain = false;
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

	if (m_height_map != nullptr) {
		m_bRegenerateTerrain = true;
	}

	for (int i = 0; i < m_Grass.size(); i++) {
		m_Grass[i].SetOwningTerrainComponent(this);
	}
}

/// TerrainComponent::EditorChange
void TerrainComponent::editor_change(const std::string& propertyName) {
	Super::editor_change(propertyName);

	GenerateTerrain();
	/*const std::string propertiesThatRegenTerrain[5] = { "HeightMap", "HeightScale", "Width", "Dimensions", "SmoothAmount" };

	for (int i = 0; i < 5; i++) {
		if (propertyName == propertiesThatRegenTerrain[i]) {
			m_bRegenerateTerrain = true;
		}
	}

	if (m_bRegenerateTerrain) {
		return;
	}

	refresh_materials();*/
/*
	if (IsEnabled()) {
		g_pRenderer->UpdateRenderObject(m_render_object);
	}*/
}

/// TerrainComponent::GenerateTerrain
void TerrainComponent::GenerateTerrain() {
	blk::error_check(
		m_height_map != nullptr,
		"TerrainComponent::GenerateTerrain() - No height map file found for terrain component on entity %s", GetOwner()->name().c_str()
	);
		
	struct pixelData {
		byte r;
		byte g;
		byte b;
		byte a;
	};

	terrainNormals.clear();

	u32 tex_width, tex_height;
	const std::vector<Vec4>& height_data = m_height_map->cpu_texture(tex_width, tex_height);

	// Build terrain here
	const i32 numVerts = m_TerrainDimensions * m_TerrainDimensions;
	const u32 numIndices = (m_TerrainDimensions - 1) * (m_TerrainDimensions - 1) * 6;
	const f32 HalfTerrainWidth = m_TerrainWidth * 0.5f;
	const f32 stepSize = m_TerrainWidth / (f32)tex_width;
	const f32 cellWidth = m_TerrainWidth / (f32)m_TerrainDimensions;

	/*if (m_TerrainModel.NumVertices() > 0) {
		g_pRenderer->RemoveRenderObject(m_render_object);
	}*/

	m_model.create_dynamic(numVerts, numIndices);

	// Vertex Buffer
	vertexLayout* const pVerts = (vertexLayout*)m_model.map_vertex_buffer();
	std::vector<Vec3> cpuVerts;
	cpuVerts.resize((size_t)m_TerrainDimensions * m_TerrainDimensions);

	i32 curr_vert = 0;
	for (i32 start_y = 0; start_y < m_TerrainDimensions; start_y++) {
		for (i32 start_x = 0; start_x < m_TerrainDimensions; start_x++) {
			f32 divisor = 0.0f;
			f32 height = 0.0f;
			const i32 blurSampleSize = max(m_TerrainSmoothAmount, 1);
			for (i32 tempY = 0; tempY < blurSampleSize; tempY++) {
				if (tempY + start_y >= m_TerrainDimensions) {
					break;
				}

				for (i32 tempX = 0; tempX < blurSampleSize; tempX++) {
					if (tempX + start_x >= m_TerrainDimensions) {
						break;
					}

					const f32 u = (f32)(start_x + tempX) / (f32)m_TerrainDimensions;
					const f32 v = (f32)(start_y + tempY) / (f32)m_TerrainDimensions;
					const i32 textureIndex = static_cast<i32>((v * tex_width * tex_width) + (u * tex_width));

					divisor += 1.0f;
					height += (f32)height_data[textureIndex].r;
				}
			}

			height *= m_HeightScale / divisor;
			cpuVerts[curr_vert] = Vec3(-HalfTerrainWidth + ((start_x + 1) * cellWidth), height, -HalfTerrainWidth + ((start_y + 1) * cellWidth));

			pVerts[curr_vert].Clear();
			pVerts[curr_vert].position = cpuVerts[curr_vert];
			pVerts[curr_vert].uv.set((f32)(start_x) / (f32)m_TerrainDimensions, (f32)(start_y) / (f32)m_TerrainDimensions);
			pVerts[curr_vert].SetColor(Vec4(1.0f, 1.0f, 1.0f, 1.0f));
			pVerts[curr_vert].SetNormal(Vec4(0.0f, 1.0f, 0.0f, 0.0f));
			curr_vert++;
		}
	}
	m_model.unmap_vertex_buffer();

	// Index Buffer

	u16* indices = (u16*)m_model.map_index_buffer();
	i32 next_write_idx = 0;
	for (i32 y = 0; y < m_TerrainDimensions - 1; y++) {
		for (i32 x = 0; x < m_TerrainDimensions - 1; x++) {
			const u32 cur_idx = (y * m_TerrainDimensions) + x;
			indices[next_write_idx + 2] = cur_idx;
			indices[next_write_idx + 1] = cur_idx + 1;
			indices[next_write_idx + 0] = cur_idx + m_TerrainDimensions;

			indices[next_write_idx + 5] = cur_idx + 1;
			indices[next_write_idx + 4] = cur_idx + 1 + m_TerrainDimensions;
			indices[next_write_idx + 3] = cur_idx + m_TerrainDimensions;
			next_write_idx += 6;
		}
	}

	m_model.unmap_index_buffer();

	for (i32 startY = 0; startY < m_TerrainDimensions; startY++) {
		for (i32 startX = 0; startX < m_TerrainDimensions; startX++) {
			i32 currentIndex = (startY * m_TerrainDimensions) + startX;

			Vec3 xVec, zVec;

			if (startX < m_TerrainDimensions - 1) {
				xVec = pVerts[currentIndex + 1].position - pVerts[currentIndex].position;
			} else {
				xVec = pVerts[currentIndex].position - pVerts[currentIndex - 1].position;
			}

			if (startY < m_TerrainDimensions - 1) {
				zVec = pVerts[currentIndex].position - pVerts[currentIndex + m_TerrainDimensions].position;
			} else {
				zVec = pVerts[currentIndex - m_TerrainDimensions].position - pVerts[currentIndex].position;
			}

			xVec.normalize_self();
			zVec.normalize_self();
			Vec3 finalVec = xVec.cross(zVec).normalize_safe();

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
			terrainNormals.push_back(newNormal);
		}
	}

	refresh_materials();

	// Update collision
	i32 collisionPatchSize = 8;

	std::vector<kbCollisionComponent::customTriangle_t> terrainCollision;
	terrainCollision.resize((size_t)((m_TerrainDimensions / collisionPatchSize) * (m_TerrainDimensions / collisionPatchSize)) * 2);

	size_t triIdx = 0;
	for (i32 y = 0; y < m_TerrainDimensions - collisionPatchSize; y += collisionPatchSize) {
		for (i32 x = 0; x < m_TerrainDimensions - collisionPatchSize; x += collisionPatchSize, triIdx += 2) {
			const size_t currentIndex = ((size_t)y * m_TerrainDimensions) + x;
			terrainCollision[triIdx + 0].m_Vertex1 = cpuVerts[currentIndex];
			terrainCollision[triIdx + 0].m_Vertex2 = cpuVerts[currentIndex + collisionPatchSize];
			terrainCollision[triIdx + 0].m_Vertex3 = cpuVerts[currentIndex + ((size_t)collisionPatchSize * m_TerrainDimensions)];

			terrainCollision[triIdx + 1].m_Vertex1 = cpuVerts[currentIndex + collisionPatchSize];
			terrainCollision[triIdx + 1].m_Vertex2 = cpuVerts[currentIndex + collisionPatchSize + ((size_t)m_TerrainDimensions * collisionPatchSize)];
			terrainCollision[triIdx + 1].m_Vertex3 = cpuVerts[currentIndex + ((size_t)collisionPatchSize * m_TerrainDimensions)];
		}
	}
}

/// TerrainComponent::SetCollisionMap
void TerrainComponent::SetCollisionMap(const kbRenderTexture* const pTexture) {
/*	for (int i = 0; i < m_Grass.size(); i++) {

		kbGrass& grass = m_Grass[i];
		grass.m_GrassShaderOverrides.SetTexture("collisionMap", pTexture);
		grass.m_GrassShaderOverrides.SetVec4("collisionMapPixelWorldSize", Vec4(GetTerrainWidth() / pTexture->GetWidth(), 0.0f, 0.0f, 0.0f));

		for (int cellIdx = 0; cellIdx < grass.m_GrassRenderObjects.size(); cellIdx++) {
			auto& grassRenderObj = grass.m_GrassRenderObjects[cellIdx];
			auto& renderObjMatList = grassRenderObj.m_render_object.m_Materials;
			renderObjMatList.clear();
			renderObjMatList.push_back(grass.m_GrassShaderOverrides);
			//g_pRenderer->UpdateRenderObject(grassRenderObj.m_render_object);
		}
	}*/
}

/// TerrainComponent::enable_internal
void TerrainComponent::enable_internal(const bool isEnabled) {
	if (m_model.NumVertices() == 0) {
		return;
	}

	if (m_LastHeightMapLoadTime == -1.0f && m_height_map != nullptr) {
		m_LastHeightMapLoadTime = m_height_map->last_load_time();
	}

	if (isEnabled) {
		refresh_materials();
		if (g_renderer) {
			g_renderer->add_render_component(this);
		}

	} else {
		/*g_pRenderer->RemoveRenderObject(m_render_object);

		for (int i = 0; i < m_Grass.size(); i++) {
			m_Grass[i].Enable(false);
		}*/
	}
}

/// TerrainComponent::update_internal
void TerrainComponent::update_internal(const float DeltaTime) {
	Super::update_internal(DeltaTime);

	if (m_height_map != nullptr && m_height_map->last_load_time() != m_LastHeightMapLoadTime) {
		m_LastHeightMapLoadTime = m_height_map->last_load_time();
		this->RegenerateTerrain();
	}

	if (m_model.GetMeshes().size() > 0 && (GetOwner()->is_dirty() || m_bDebugForceRegenTerrain == true)) {
		refresh_materials();
		RegenerateTerrain();
	//	g_pRenderer->UpdateRenderObject(m_render_object);
		m_bDebugForceRegenTerrain = false;
	}

	/*
		const Mat4 ownerRot = owner_rotation().to_mat4();
		const Vec3 ownerPos = owner_position();
		for ( int i = 0; i < m_GrassZones.size(); i++ ) {

			Vec3 boundsCenter = ownerRot.transform_point( m_GrassZones[i].GetCenter() ) + ownerPos;
			Vec3 boundsExtent = m_GrassZones[i].GetExtents();

			const Vec3 boundsMin = boundsCenter - boundsExtent;
			const Vec3 boundsMax = boundsCenter + boundsExtent;
			g_pRenderer->DrawBox( kbBounds( boundsMin, boundsMax ), kbColor::red );
		}*/
}

/// TerrainComponent::RenderSync
void TerrainComponent::render_sync() {
	Super::render_sync();

	if (m_bRegenerateTerrain) {
		GenerateTerrain();
		for (int i = 0; i < m_Grass.size(); i++) {
			m_Grass[i].m_bUpdatePointCloud = true;
			m_Grass[i].RefreshGrass();
		}
		m_bRegenerateTerrain = false;
	}

	for (int i = 0; i < m_Grass.size(); i++) {
		m_Grass[i].render_sync();
	}
}

/// TerrainComponent::RefreshMaterials
void TerrainComponent::refresh_materials() {
	m_render_object.m_casts_shadow = false;
	m_render_object.m_bIsSkinnedModel = false;
	m_render_object.m_rotation = GetOwner()->rotation();
	m_render_object.m_position = GetOwner()->position();
	m_render_object.m_EntityId = GetOwner()->GetEntityId();
	m_render_object.m_Scale.set(1.0f, 1.0f, 1.0f);
	m_render_object.m_model = &m_model;
	m_render_object.m_render_pass = RP_Lighting;
	m_render_object.m_pComponent = this;
}

/// kbGrassZone::Constructor
void kbGrassZone::Constructor() {
	m_Center.set(0.0f, 0.0f, 0.0f);
	m_Extents.set(100.0f, 100.0f, 100.0f);
}