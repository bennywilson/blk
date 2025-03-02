/// ParticleComponent.cpp
///
/// 2016-2025 blk 1.0

#include "blk_core.h"
#include "blk_containers.h"
#include "Matrix.h"
#include "kbRenderer_defs.h"
#include "kbGameEntityHeader.h"
#include "kbGame.h"
#include "kbRenderer.h"
#include "renderer.h"


KB_DEFINE_COMPONENT(ParticleComponent)

static const uint NumParticleBufferVerts = 10000;
static const uint NumMeshVerts = 10000;

/// kbParticle_t::kbParticle_t
kbParticle_t::kbParticle_t() {
}

/// kbParticle_t::~kbParticle_t
kbParticle_t::~kbParticle_t() {
}

/// kbParticle_t::Shutdown
void kbParticle_t::Shutdown() {
}

/// ParticleComponent::Initialize
void ParticleComponent::Constructor() {
	m_MaxParticlesToEmit = -1;
	m_TotalDuration = -1.0f;
	m_StartDelay = 0.0f;

	m_MinParticleSpawnRate = 1.0f;
	m_MaxParticleSpawnRate = 2.0f;
	m_MinParticleStartVelocity.set(-2.0f, 5.0f, -2.0f);
	m_MaxParticleStartVelocity.set(2.0f, 5.0f, 2.0f);
	m_MinParticleEndVelocity.set(0.0f, 0.0f, 0.0f);
	m_MaxParticleEndVelocity.set(0.0f, 0.0f, 0.0f);
	m_MinStartRotationRate = 0;
	m_MaxStartRotationRate = 0;
	m_MinEndRotationRate = 0;
	m_MaxEndRotationRate = 0;

	m_MinStart3DRotation = Vec3::zero;
	m_MaxStart3DRotation = Vec3::zero;

	m_MinStart3DOffset = Vec3::zero;
	m_MaxStart3DOffset = Vec3::zero;

	m_MinParticleStartSize.set(3.0f, 3.0f, 3.0f);
	m_MaxParticleStartSize.set(3.0f, 3.0f, 3.0f);
	m_MinParticleEndSize.set(3.0f, 3.0f, 3.0f);
	m_MaxParticleEndSize.set(3.0f, 3.0f, 3.0f);
	m_ParticleMinDuration = 3.0f;
	m_ParticleMaxDuration = 3.0f;
	m_ParticleStartColor.set(1.0f, 1.0f, 1.0f, 1.0f);
	m_ParticleEndColor.set(1.0f, 1.0f, 1.0f, 1.0f);
	m_MinBurstCount = 0;
	m_MaxBurstCount = 0;
	m_BurstCount = 0;
	m_StartDelayRemaining = 0;
	m_NumEmittedParticles = 0;
	m_ParticleBillboardType = BT_FaceCamera;
	m_gravity.set(0.0f, 0.0f, 0.0f);
	m_render_order_bias = 0.0f;
	m_DebugPlayEntity = false;

	m_LeftOverTime = 0.0f;
	m_vertex_buffer = nullptr;
	m_index_buffer = nullptr;

	m_buffer_to_fill = -1;
	m_buffer_to_render = -1;

	m_bIsSpawning = true;

	m_bIsPooled = false;
	m_ParticleTemplate = nullptr;
}

/// ParticleComponent::~ParticleComponent
ParticleComponent::~ParticleComponent() {
	StopParticleSystem();

	// Dx12
	for (int i = 0; i < NumParticleBuffers; i++) {
		m_models[i].Release();
	}
}

/// ParticleComponent::StopParticleSystem
void ParticleComponent::StopParticleSystem() {

	blk::error_check(g_pRenderer->IsRenderingSynced() == true, "ParticleComponent::StopParticleSystem() - Shutting down particle component even though rendering is not synced");

	if (IsModelEmitter()) {
		return;
	}

#ifdef DX11_PARTICLES
	g_pRenderer->RemoveParticle(m_render_object);
#endif

	if (g_renderer) {
		g_renderer->remove_render_component(this);
	}

	// Dx12
	for (u32 i = 0; i < NumParticleBuffers; i++) {
		if (m_models[i].IsVertexBufferMapped()) {
			m_models[i].UnmapVertexBuffer(0);
		}

		if (m_models[i].IsIndexBufferMapped()) {
			m_models[i].UnmapIndexBuffer();		// todo : don't need to map/remap index buffer
		}
	}
	m_buffer_to_fill = -1;
	m_buffer_to_render = -1;
	m_Particles.clear();
	m_LeftOverTime = 0.0f;

	//m_ParticleBillboardType = BT_FaceCamera;
}

/// ParticleComponent::update_internal
void ParticleComponent::update_internal(const float DeltaTime) {
	Super::update_internal(DeltaTime);

	if (m_StartDelay > 0) {
		m_StartDelay -= DeltaTime;
		if (m_StartDelay < 0) {
			m_TimeAlive = 0.0f;
			if (m_MaxBurstCount > 0) {
				m_BurstCount = m_MinBurstCount;
				if (m_MaxBurstCount > m_MinBurstCount) {
					m_BurstCount += rand() % (m_MaxBurstCount - m_MinBurstCount);
				}
			}
		} else {
			return;
		}
	}

	const float eps = 0.00000001f;
	/*if ( IsModelEmitter() == false && ( m_pVertexBuffer == nullptr || m_pIndexBuffer == nullptr ) ) {
		return;
	}*/

	if (m_MaxBurstCount <= 0 && (m_MaxParticleSpawnRate <= eps || m_MinParticleSpawnRate < eps || m_MaxParticleSpawnRate < m_MinParticleSpawnRate || m_ParticleMinDuration <= eps)) {
		return;
	}

	Vec3 currentCameraPosition;
	Quat4 currentCameraRotation;
	g_pRenderer->GetRenderViewTransform(nullptr, currentCameraPosition, currentCameraRotation);

	int iVertex = 0;
	int curVBPosition = 0;
	const Vec3 scale = GetScale();
	const Vec3 direction = GetOrientation().to_mat4()[2].ToVec3();
	byte iBillboardType = 0;
	switch (m_ParticleBillboardType) {
		case EBillboardType::BT_FaceCamera: iBillboardType = 0; break;
		case EBillboardType::BT_AxialBillboard: iBillboardType = 1; break;
		case EBillboardType::BT_AlignAlongVelocity: iBillboardType = 1; break;
		default: blk::warn("ParticleComponent::update_internal() - Invalid billboard type specified"); break;
	}

	kbParticleVertex* pDstVerts = nullptr;

	for (int i = (int)m_Particles.size() - 1; i >= 0; i--) {
		kbParticle_t& particle = m_Particles[i];

		if (particle.m_life_left >= 0.0f) {
			particle.m_life_left -= DeltaTime;

			if (particle.m_life_left <= 0.0f) {
				particle.Shutdown();
				blk::std_remove_idx_swap(m_Particles, i);
				continue;
			}
		}
	}

	m_render_object.m_VertBufferIndexCount = (uint)m_Particles.size() * 6;

	for (int i = (int)m_Particles.size() - 1; i >= 0; i--) {
		kbParticle_t& particle = m_Particles[i];
		const float normalizedTime = (particle.m_total_life - particle.m_life_left) / particle.m_total_life;
		Vec3 curVelocity = Vec3::zero;

		if (m_velocityOverLifeTimeCurve.size() == 0) {
			curVelocity = kbLerp(particle.m_start_velocity, particle.m_end_velocity, normalizedTime);
		} else {
			const float velCurve = kbAnimEvent::Evaluate(m_velocityOverLifeTimeCurve, normalizedTime);
			curVelocity = particle.m_start_velocity * velCurve;
		}

		curVelocity += m_gravity * (particle.m_total_life - particle.m_life_left);

		particle.m_position = particle.m_position + curVelocity * DeltaTime;

		const float curRotationRate = kbLerp(particle.m_start_rotation, particle.m_end_rotation, normalizedTime);
		particle.m_rotation += curRotationRate * DeltaTime;

		Vec3 curSize = Vec3::zero;
		if (m_SizeOverLifeTimeCurve.size() == 0) {
			curSize = kbLerp(particle.m_start_size * scale.x, particle.m_end_size * scale.y, normalizedTime);
		} else {
			Vec3 eval = kbVectorAnimEvent::Evaluate(m_SizeOverLifeTimeCurve, normalizedTime).ToVec3();
			curSize.x = eval.x * particle.m_start_size.x * scale.x;
			curSize.y = eval.y * particle.m_start_size.y * scale.y;
			curSize.z = eval.z * particle.m_start_size.z * scale.z;
		}

		Vec4 curColor = Vec4::zero;
		if (m_ColorOverLifeTimeCurve.size() == 0) {
			curColor = kbLerp(m_ParticleStartColor, m_ParticleEndColor, normalizedTime);
		} else {
			curColor = kbVectorAnimEvent::Evaluate(m_ColorOverLifeTimeCurve, normalizedTime);
		}

		if (m_AlphaOverLifeTimeCurve.size() == 0) {
			curColor.w = kbLerp(m_ParticleStartColor.w, m_ParticleEndColor.w, normalizedTime);
		} else {
			curColor.w = kbAnimEvent::Evaluate(m_AlphaOverLifeTimeCurve, normalizedTime);
		}

		byte byteColor[4] = { (byte)kbClamp(curColor.x * 255.0f, 0.0f, 255.0f), (byte)kbClamp(curColor.y * 255.0f, 0.0f, 255.0f), (byte)kbClamp(curColor.z * 255.0f, 0.0f, 255.0f), (byte)kbClamp(curColor.w * 255.0f, 0.0f, 255.0f) };

		if (g_renderer != nullptr) {
			const u32 idx = iVertex;
			m_vertex_buffer[idx + 0].position = particle.m_position;
			m_vertex_buffer[idx + 1].position = particle.m_position;
			m_vertex_buffer[idx + 2].position = particle.m_position;
			m_vertex_buffer[idx + 3].position = particle.m_position;

			m_vertex_buffer[idx + 0].uv.set(0.0f, 0.0f);
			m_vertex_buffer[idx + 1].uv.set(1.0f, 0.0f);
			m_vertex_buffer[idx + 2].uv.set(1.0f, 1.0f);
			m_vertex_buffer[idx + 3].uv.set(0.0f, 1.0f);

			memcpy(&m_vertex_buffer[idx + 0].color, byteColor, sizeof(byteColor));
			memcpy(&m_vertex_buffer[idx + 1].color, byteColor, sizeof(byteColor));
			memcpy(&m_vertex_buffer[idx + 2].color, byteColor, sizeof(byteColor));
			memcpy(&m_vertex_buffer[idx + 3].color, byteColor, sizeof(byteColor));

			m_vertex_buffer[idx + 0].rotation = particle.m_rotation;
			m_vertex_buffer[idx + 1].rotation = particle.m_rotation;
			m_vertex_buffer[idx + 2].rotation = particle.m_rotation;
			m_vertex_buffer[idx + 3].rotation = particle.m_rotation;

			m_vertex_buffer[idx + 0].scale = abs(curSize.x);
			m_vertex_buffer[idx + 1].scale = abs(curSize.x);
			m_vertex_buffer[idx + 2].scale = abs(curSize.x);
			m_vertex_buffer[idx + 3].scale = abs(curSize.x);
		}

#ifdef DX11_PARTICLES
		pDstVerts[iVertex + 0].position = particle.m_position;
		pDstVerts[iVertex + 1].position = particle.m_position;
		pDstVerts[iVertex + 2].position = particle.m_position;
		pDstVerts[iVertex + 3].position = particle.m_position;

		pDstVerts[iVertex + 0].uv.set(0.0f, 0.0f);
		pDstVerts[iVertex + 1].uv.set(1.0f, 0.0f);
		pDstVerts[iVertex + 2].uv.set(1.0f, 1.0f);
		pDstVerts[iVertex + 3].uv.set(0.0f, 1.0f);


		pDstVerts[iVertex + 0].size = Vec2(-curSize.x, curSize.y);
		pDstVerts[iVertex + 1].size = Vec2(curSize.x, curSize.y);
		pDstVerts[iVertex + 2].size = Vec2(curSize.x, -curSize.y);
		pDstVerts[iVertex + 3].size = Vec2(-curSize.x, -curSize.y);

		memcpy(&pDstVerts[iVertex + 0].color, byteColor, sizeof(byteColor));
		memcpy(&pDstVerts[iVertex + 1].color, byteColor, sizeof(byteColor));
		memcpy(&pDstVerts[iVertex + 2].color, byteColor, sizeof(byteColor));
		memcpy(&pDstVerts[iVertex + 3].color, byteColor, sizeof(byteColor));

		if (m_ParticleBillboardType == EBillboardType::BT_AlignAlongVelocity) {
			Vec3 alignVec = Vec3::up;
			if (curVelocity.length_sqr() > 0.01f) {
				alignVec = curVelocity.normalize_safe();
				pDstVerts[iVertex + 0].direction = alignVec;
				pDstVerts[iVertex + 1].direction = alignVec;
				pDstVerts[iVertex + 2].direction = alignVec;
				pDstVerts[iVertex + 3].direction = alignVec;
			}
		} else {
			pDstVerts[iVertex + 0].direction = direction;
			pDstVerts[iVertex + 1].direction = direction;
			pDstVerts[iVertex + 2].direction = direction;
			pDstVerts[iVertex + 3].direction = direction;
		}

		pDstVerts[iVertex + 0].rotation = particle.m_rotation;
		pDstVerts[iVertex + 1].rotation = particle.m_rotation;
		pDstVerts[iVertex + 2].rotation = particle.m_rotation;
		pDstVerts[iVertex + 3].rotation = particle.m_rotation;

		pDstVerts[iVertex + 0].billboardType[0] = iBillboardType;
		pDstVerts[iVertex + 1].billboardType[0] = iBillboardType;
		pDstVerts[iVertex + 2].billboardType[0] = iBillboardType;
		pDstVerts[iVertex + 3].billboardType[0] = iBillboardType;

		pDstVerts[iVertex + 0].billboardType[1] = (byte)kbClamp(particle.m_randoms[0] * 255.0f, 0.0f, 255.0f);
		pDstVerts[iVertex + 1].billboardType[1] = pDstVerts[iVertex + 0].billboardType[1];
		pDstVerts[iVertex + 2].billboardType[1] = pDstVerts[iVertex + 0].billboardType[1];
		pDstVerts[iVertex + 3].billboardType[1] = pDstVerts[iVertex + 0].billboardType[1];

		pDstVerts[iVertex + 0].billboardType[2] = (byte)kbClamp(particle.m_randoms[1] * 255.0f, 0.0f, 255.0f);
		pDstVerts[iVertex + 1].billboardType[2] = pDstVerts[iVertex + 0].billboardType[2];
		pDstVerts[iVertex + 2].billboardType[2] = pDstVerts[iVertex + 0].billboardType[2];
		pDstVerts[iVertex + 3].billboardType[2] = pDstVerts[iVertex + 0].billboardType[2];

		pDstVerts[iVertex + 0].billboardType[3] = (byte)kbClamp(particle.m_randoms[2] * 255.0f, 0.0f, 255.0f);
		pDstVerts[iVertex + 1].billboardType[3] = pDstVerts[iVertex + 0].billboardType[3];
		pDstVerts[iVertex + 2].billboardType[3] = pDstVerts[iVertex + 0].billboardType[3];
		pDstVerts[iVertex + 3].billboardType[3] = pDstVerts[iVertex + 0].billboardType[3];
#endif

		iVertex += 4;
		curVBPosition++;
	}

	m_TimeAlive += DeltaTime;
	if (m_TotalDuration > 0.0f && m_TimeAlive > m_TotalDuration && m_BurstCount <= 0) {
		return;
	}

	const float invMinSpawnRate = (m_MinParticleSpawnRate > 0.0f) ? (1.0f / m_MinParticleSpawnRate) : (0.0f);
	const float invMaxSpawnRate = (m_MaxParticleSpawnRate > 0.0f) ? (1.0f / m_MaxParticleSpawnRate) : (0.0f);
	float TimeLeft = DeltaTime - m_LeftOverTime;
	int currentListEnd = (int)m_Particles.size();
	float NextSpawn = 0.0f;

	Mat4 ownerMatrix = GetOwner()->GetOrientation().to_mat4();

	// Spawn particles
	Vec3 MyPosition = GetPosition();
	while (m_bIsSpawning && ((m_MaxParticleSpawnRate > 0 && TimeLeft >= NextSpawn) || m_BurstCount > 0) && (m_MaxParticlesToEmit <= 0 || m_NumEmittedParticles < m_MaxParticlesToEmit)) {
		if (m_MinStart3DOffset.compare(Vec3::zero) == false || m_MaxStart3DOffset.compare(Vec3::zero) == false) {
			const Vec3 startingOffset = Vec3Rand(m_MinStart3DOffset, m_MaxStart3DOffset);
			MyPosition += startingOffset;
		}

		kbParticle_t newParticle;
		newParticle.m_start_velocity = Vec3Rand(m_MinParticleStartVelocity, m_MaxParticleStartVelocity) * ownerMatrix;
		newParticle.m_end_velocity = Vec3Rand(m_MinParticleEndVelocity, m_MaxParticleEndVelocity) * ownerMatrix;

		newParticle.m_position = MyPosition + newParticle.m_start_velocity * TimeLeft;
		newParticle.m_life_left = m_ParticleMinDuration + (kbfrand() * (m_ParticleMaxDuration - m_ParticleMinDuration));
		newParticle.m_total_life = newParticle.m_life_left;

		const float startSizeRand = kbfrand();
		newParticle.m_start_size.x = m_MinParticleStartSize.x + (startSizeRand * (m_MaxParticleStartSize.x - m_MinParticleStartSize.x));
		newParticle.m_start_size.y = m_MinParticleStartSize.y + (startSizeRand * (m_MaxParticleStartSize.y - m_MinParticleStartSize.y));
		newParticle.m_start_size.z = m_MinParticleStartSize.z + (startSizeRand * (m_MaxParticleStartSize.z - m_MinParticleStartSize.z));

		const float endSizeRand = kbfrand();
		newParticle.m_end_size.x = m_MinParticleEndSize.x + (endSizeRand * (m_MaxParticleEndSize.x - m_MinParticleEndSize.x));
		newParticle.m_end_size.y = m_MinParticleEndSize.y + (endSizeRand * (m_MaxParticleEndSize.y - m_MinParticleEndSize.y));
		newParticle.m_end_size.z = m_MinParticleEndSize.z + (endSizeRand * (m_MaxParticleEndSize.z - m_MinParticleEndSize.z));

		if (IsModelEmitter()) {
			blk::log("StartSize = %f %f %f", newParticle.m_start_size.x, newParticle.m_start_size.y, newParticle.m_start_size.z);
			blk::log("End = %f %f %f", newParticle.m_end_size.x, newParticle.m_end_size.y, newParticle.m_end_size.z);
		}

		newParticle.m_randoms[0] = kbfrand();
		newParticle.m_randoms[1] = kbfrand();
		newParticle.m_randoms[2] = kbfrand();

		newParticle.m_start_rotation = kbfrand(m_MinStartRotationRate, m_MaxStartRotationRate);
		newParticle.m_end_rotation = kbfrand(m_MinEndRotationRate, m_MaxEndRotationRate);

		if (newParticle.m_start_rotation != 0 || newParticle.m_end_rotation != 0) {
			newParticle.m_rotation = kbfrand() * kbPI;
		} else {
			newParticle.m_rotation = 0;
		}

		if (m_BurstCount > 0) {
			m_BurstCount--;
		} else {
			TimeLeft -= NextSpawn;
			NextSpawn = invMaxSpawnRate + (kbfrand() * (invMinSpawnRate - invMaxSpawnRate));
		}

		m_NumEmittedParticles++;
		m_Particles.push_back(newParticle);
	}


	//blk::log( "Num Indices = %d", m_NumIndicesInCurrentBuffer );
	m_LeftOverTime = NextSpawn - TimeLeft;
}

/// ParticleComponent::EditorChange
void ParticleComponent::editor_change(const std::string& propertyName) {
	Super::editor_change(propertyName);

	// Editor Hack!
	if (propertyName == "Materials") {
		for (int i = 0; i < this->m_materials.size(); i++) {
			m_materials[i].SetOwningComponent(this);
		}
	} else if (propertyName == "DebugPlayEntity") {
		kbGameEntity* const pEnt = GetOwner();
		const int numComp = (int)pEnt->NumComponents();
		for (int i = 0; i < numComp; i++) {
			ParticleComponent* const pParticle = pEnt->GetComponent(i)->GetAs<ParticleComponent>();
			if (pParticle == nullptr) {
				continue;
			}

			pParticle->Enable(!pParticle->IsEnabled());
		}
	}
}

/// ParticleComponent::RenderSync
void ParticleComponent::render_sync() {
	Super::render_sync();

	if (g_UseEditor && IsEnabled() == true && (m_TotalDuration > 0.0f && m_TimeAlive > m_TotalDuration && m_BurstCount <= 0)) {
		StopParticleSystem();
		Enable(false);
		Enable(true);
		return;
	}

	if (IsEnabled() == false || (m_TotalDuration > 0.0f && m_TimeAlive > m_TotalDuration && m_render_object.m_VertBufferIndexCount == 0)) {
		StopParticleSystem();
		Enable(false);
		return;
	}

	if (IsModelEmitter()) {
		return;
	}

	if (g_renderer != nullptr) {
		if (m_models[0].NumVertices() == 0) {
			for (u32 i = 0; i < NumParticleBuffers; i++) {
				m_models[i].create_dynamic(NumParticleBufferVerts, NumParticleBufferVerts);
				m_vertex_buffer = (ParticleVertex*)m_models[i].map_vertex_buffer();
				for (u32 idx = 0; idx < NumParticleBufferVerts; idx++) {
					m_vertex_buffer[idx].position.set(0.0f, 0.0f, 0.0f);
					m_vertex_buffer[idx].uv.set(0.f, 0.f);
				}
				m_models[i].unmap_vertex_buffer();

				m_index_buffer = (u16*)m_models[i].map_index_buffer();
				for (u32 index_buf = 0, vertex_buf = 0; index_buf < NumParticleBufferVerts; index_buf += 6, vertex_buf += 4) {
					m_index_buffer[index_buf + 0] = vertex_buf + 2;
					m_index_buffer[index_buf + 1] = vertex_buf + 1;
					m_index_buffer[index_buf + 2] = vertex_buf + 0;
					m_index_buffer[index_buf + 3] = vertex_buf + 3;
					m_index_buffer[index_buf + 4] = vertex_buf + 2;
					m_index_buffer[index_buf + 5] = vertex_buf + 0;
				}

				m_models[i].unmap_index_buffer();
			}
		}
	}

	m_render_object.m_pComponent = this;
	m_render_object.m_render_pass = RP_Translucent;
	m_render_object.m_position = GetPosition();
	m_render_object.m_Orientation = Quat4(0.0f, 0.0f, 0.0f, 1.0f);
	m_render_object.m_render_order_bias = m_render_order_bias;

	// Update materials
	m_render_object.m_Materials.clear();
	for (int i = 0; i < m_materials.size(); i++) {
		kbMaterialComponent& matComp = m_materials[i];

		kbShaderParamOverrides_t newShaderParams;
		newShaderParams.m_shader = matComp.get_shader();

		const auto& srcShaderParams = matComp.shader_params();
		for (int j = 0; j < srcShaderParams.size(); j++) {
			if (srcShaderParams[j].texture() != nullptr) {
				newShaderParams.SetTexture(srcShaderParams[j].param_name().stl_str(), srcShaderParams[j].texture());
			} else if (srcShaderParams[j].render_texture() != nullptr) {
				newShaderParams.SetTexture(srcShaderParams[j].param_name().stl_str(), srcShaderParams[j].render_texture());
			} else {
				newShaderParams.SetVec4(srcShaderParams[j].param_name().stl_str(), srcShaderParams[j].vector());
			}
		}

		m_render_object.m_Materials.push_back(newShaderParams);
	}

	m_buffer_to_render = m_buffer_to_fill;
	if (m_buffer_to_fill == -1) {
		m_buffer_to_fill = 0;
	} else {
#ifdef DX11_PARTICLES
		g_pRenderer->RemoveParticle(m_render_object);
#endif

		if (g_renderer != nullptr) {
			m_models[m_buffer_to_fill].unmap_vertex_buffer();
			//m_models[m_buffer_to_fill].unmap_index_buffer();
		}
	}

#ifdef DX11_PARTICLES
	//	m_render_object.m_model = &m_ParticleBuffer[m_CurrentParticleBuffer];
	if (m_render_object.m_VertBufferIndexCount > 0) {
		g_pRenderer->AddParticle(m_render_object);
	}
#endif

	m_buffer_to_fill++;
	if (m_buffer_to_fill >= NumParticleBuffers) {
		m_buffer_to_fill = 0;
	}

	if (g_renderer != nullptr) {
		m_vertex_buffer = (ParticleVertex*)m_models[m_buffer_to_fill].map_vertex_buffer();
		memset(m_vertex_buffer, 0, sizeof(vertexLayout)* m_models[m_buffer_to_fill].NumVertices());
		//m_index_buffer = (u16*)m_models[m_buffer_to_fill].map_index_buffer();
	}

	if (g_renderer != nullptr) {
		g_renderer->add_render_component(this);
	}
	}

/// ParticleComponent::enable_internal
void ParticleComponent::enable_internal(const bool isEnabled) {
	Super::enable_internal(isEnabled);

	if (isEnabled) {
		m_bIsSpawning = true;
		m_NumEmittedParticles = 0;

		if (m_StartDelay > 0) {
			m_StartDelayRemaining = m_StartDelay;
		} else {
			m_TimeAlive = 0.0f;
			if (m_MaxBurstCount > 0) {
				m_BurstCount = m_MinBurstCount;
				if (m_MaxBurstCount > m_MinBurstCount) {
					m_BurstCount += rand() % (m_MaxBurstCount - m_MinBurstCount);
				}
			}
		}

		for (int i = 0; i < m_ModelEmitter.size(); i++) {
			m_ModelEmitter[i].Init();
		}

			} else {
#ifdef DX11_PARTICLES
		g_pRenderer->RemoveParticle(m_render_object);
#endif

		if (g_renderer != nullptr) {
			g_renderer->remove_render_component(this);
		}

		m_Particles.clear();
		m_LeftOverTime = 0.0f;
	}
		}

/// ParticleComponent::EnableNewSpawns
void ParticleComponent::EnableNewSpawns(const bool bEnable) {
	if (m_bIsSpawning == bEnable) {
		return;
	}

	m_bIsSpawning = bEnable;
	m_LeftOverTime = 0.0f;
}

/// ParticleComponent::Constructor
void kbModelEmitter::Constructor() {
	m_model = nullptr;

}

/// kbModelEmitter::Init
void kbModelEmitter::Init() {

	for (int i = 0; i < m_materials.size(); i++) {
		const kbMaterialComponent& matComp = m_materials[i];

		kbShaderParamOverrides_t newShaderParams;
		newShaderParams.m_shader = matComp.get_shader();

		const auto& srcShaderParams = matComp.shader_params();
		for (int j = 0; j < srcShaderParams.size(); j++) {
			if (srcShaderParams[j].texture() != nullptr) {
				newShaderParams.SetTexture(srcShaderParams[j].param_name().stl_str(), srcShaderParams[j].texture());
			} else if (srcShaderParams[j].render_texture() != nullptr) {

				newShaderParams.SetTexture(srcShaderParams[j].param_name().stl_str(), srcShaderParams[j].render_texture());
			} else {
				newShaderParams.SetVec4(srcShaderParams[j].param_name().stl_str(), srcShaderParams[j].vector());
			}
		}

		m_ShaderParams.push_back(newShaderParams);
	}
}
