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
	m_max_particles_to_emit = -1;
	m_total_duration = -1.0f;
	m_start_delay = 0.0f;

	m_min_spawn_rate = 1.0f;
	m_max_particle_spawn_rate = 2.0f;
	m_min_start_velocity.set(-2.0f, 5.0f, -2.0f);
	m_max_start_velocity.set(2.0f, 5.0f, 2.0f);
	m_min_end_velocity.set(0.0f, 0.0f, 0.0f);
	m_max_end_velocity.set(0.0f, 0.0f, 0.0f);
	m_min_start_rotation_rate = 0;
	m_max_start_rotation_rate = 0;
	m_min_end_rotation_rate = 0;
	m_max_end_rotation_rate = 0;

	m_min_start_3d_rotation = Vec3::zero;
	m_max_start_3d_rotation = Vec3::zero;

	m_min_start_3d_offset = Vec3::zero;
	m_max_start_3d_offset = Vec3::zero;

	m_min_start_size.set(3.0f, 3.0f, 3.0f);
	m_max_start_size.set(3.0f, 3.0f, 3.0f);
	m_min_end_size.set(3.0f, 3.0f, 3.0f);
	m_max_end_size.set(3.0f, 3.0f, 3.0f);
	m_min_duration = 3.0f;
	m_max_duration = 3.0f;
	m_start_color.set(1.0f, 1.0f, 1.0f, 1.0f);
	m_end_color.set(1.0f, 1.0f, 1.0f, 1.0f);
	m_min_burst_count = 0;
	m_max_burst_count = 0;
	m_burst_count = 0;
	m_start_delay_remaining = 0;
	m_num_particles_emitted = 0;
	m_billboard_type = BT_FaceCamera;
	m_gravity.set(0.0f, 0.0f, 0.0f);
	m_render_order_bias = 0.0f;
	m_debug_play_entity = false;

	m_left_over_time = 0.0f;
	m_vertex_buffer = nullptr;
	m_index_buffer = nullptr;

	m_buffer_to_fill = -1;
	m_buffer_to_render = -1;

	m_is_spawning = true;

	m_is_pooled = false;
	m_template = nullptr;
}

/// ParticleComponent::~ParticleComponent
ParticleComponent::~ParticleComponent() {
	stop_particle_system();

	// Dx12
	for (int i = 0; i < NumParticleBuffers; i++) {
		m_models[i].Release();
	}
}

/// ParticleComponent::StopParticleSystem
void ParticleComponent::stop_particle_system() {

	blk::error_check(g_pRenderer->IsRenderingSynced() == true, "ParticleComponent::stop_particle_system() - Shutting down particle component even though rendering is not synced");

	if (is_model_emitter()) {
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
	m_left_over_time = 0.0f;

	//m_billboard_type = BT_FaceCamera;
}

/// ParticleComponent::update_internal
void ParticleComponent::update_internal(const float DeltaTime) {
	Super::update_internal(DeltaTime);

	if (m_start_delay > 0) {
		m_start_delay -= DeltaTime;
		if (m_start_delay < 0) {
			m_time_alive = 0.0f;
			if (m_max_burst_count > 0) {
				m_burst_count = m_min_burst_count;
				if (m_max_burst_count > m_min_burst_count) {
					m_burst_count += rand() % (m_max_burst_count - m_min_burst_count);
				}
			}
		} else {
			return;
		}
	}

	const float eps = 0.00000001f;
	/*if ( is_model_emitter() == false && ( m_pVertexBuffer == nullptr || m_pIndexBuffer == nullptr ) ) {
		return;
	}*/

	if (m_max_burst_count <= 0 && (m_max_particle_spawn_rate <= eps || m_min_spawn_rate < eps || m_max_particle_spawn_rate < m_min_spawn_rate || m_min_duration <= eps)) {
		return;
	}

	Vec3 currentCameraPosition;
	Quat4 currentCameraRotation;
	g_pRenderer->GetRenderViewTransform(nullptr, currentCameraPosition, currentCameraRotation);

	int iVertex = 0;
	int curVBPosition = 0;
	const Vec3 scale = GetScale();
	const Vec3 direction = GetOrientation().to_mat4()[2].ToVec3();
	u8 iBillboardType = 0;
	switch (m_billboard_type) {
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

		if (m_velocity_over_life_curve.size() == 0) {
			curVelocity = kbLerp(particle.m_start_velocity, particle.m_end_velocity, normalizedTime);
		} else {
			const float velCurve = kbAnimEvent::Evaluate(m_velocity_over_life_curve, normalizedTime);
			curVelocity = particle.m_start_velocity * velCurve;
		}

		curVelocity += m_gravity * (particle.m_total_life - particle.m_life_left);

		particle.m_position = particle.m_position + curVelocity * DeltaTime;

		const float curRotationRate = kbLerp(particle.m_start_rotation, particle.m_end_rotation, normalizedTime);
		particle.m_rotation += curRotationRate * DeltaTime;

		Vec3 curSize = Vec3::zero;
		if (m_size_over_life_curve.size() == 0) {
			curSize = kbLerp(particle.m_start_size * scale.x, particle.m_end_size * scale.y, normalizedTime);
		} else {
			Vec3 eval = kbVectorAnimEvent::Evaluate(m_size_over_life_curve, normalizedTime).ToVec3();
			curSize.x = eval.x * particle.m_start_size.x * scale.x;
			curSize.y = eval.y * particle.m_start_size.y * scale.y;
			curSize.z = eval.z * particle.m_start_size.z * scale.z;
		}

		Vec4 curColor = Vec4::zero;
		if (m_color_over_life_curve.size() == 0) {
			curColor = kbLerp(m_start_color, m_end_color, normalizedTime);
		} else {
			curColor = kbVectorAnimEvent::Evaluate(m_color_over_life_curve, normalizedTime);
		}

		if (m_alpha_over_life_curve.size() == 0) {
			curColor.w = kbLerp(m_start_color.w, m_end_color.w, normalizedTime);
		} else {
			curColor.w = kbAnimEvent::Evaluate(m_alpha_over_life_curve, normalizedTime);
		}

		u8 byteColor[4] = { (u8)kbClamp(curColor.x * 255.0f, 0.0f, 255.0f), (u8)kbClamp(curColor.y * 255.0f, 0.0f, 255.0f), (u8)kbClamp(curColor.z * 255.0f, 0.0f, 255.0f), (u8)kbClamp(curColor.w * 255.0f, 0.0f, 255.0f) };

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

		if (m_billboard_type == EBillboardType::BT_AlignAlongVelocity) {
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

	m_time_alive += DeltaTime;
	if (m_total_duration > 0.0f && m_time_alive > m_total_duration && m_burst_count <= 0) {
		return;
	}

	const float invMinSpawnRate = (m_min_spawn_rate > 0.0f) ? (1.0f / m_min_spawn_rate) : (0.0f);
	const float invMaxSpawnRate = (m_max_particle_spawn_rate > 0.0f) ? (1.0f / m_max_particle_spawn_rate) : (0.0f);
	float TimeLeft = DeltaTime - m_left_over_time;
	int currentListEnd = (int)m_Particles.size();
	float NextSpawn = 0.0f;

	Mat4 ownerMatrix = GetOwner()->GetOrientation().to_mat4();

	// Spawn particles
	Vec3 MyPosition = GetPosition();
	while (m_is_spawning && ((m_max_particle_spawn_rate > 0 && TimeLeft >= NextSpawn) || m_burst_count > 0) && (m_max_particles_to_emit <= 0 || m_num_particles_emitted < m_max_particles_to_emit)) {
		if (m_min_start_3d_offset.compare(Vec3::zero) == false || m_max_start_3d_offset.compare(Vec3::zero) == false) {
			const Vec3 startingOffset = Vec3Rand(m_min_start_3d_offset, m_max_start_3d_offset);
			MyPosition += startingOffset;
		}

		kbParticle_t newParticle;
		newParticle.m_start_velocity = Vec3Rand(m_min_start_velocity, m_max_start_velocity) * ownerMatrix;
		newParticle.m_end_velocity = Vec3Rand(m_min_end_velocity, m_max_end_velocity) * ownerMatrix;

		newParticle.m_position = MyPosition + newParticle.m_start_velocity * TimeLeft;
		newParticle.m_life_left = m_min_duration + (kbfrand() * (m_max_duration - m_min_duration));
		newParticle.m_total_life = newParticle.m_life_left;

		const float startSizeRand = kbfrand();
		newParticle.m_start_size.x = m_min_start_size.x + (startSizeRand * (m_max_start_size.x - m_min_start_size.x));
		newParticle.m_start_size.y = m_min_start_size.y + (startSizeRand * (m_max_start_size.y - m_min_start_size.y));
		newParticle.m_start_size.z = m_min_start_size.z + (startSizeRand * (m_max_start_size.z - m_min_start_size.z));

		const float endSizeRand = kbfrand();
		newParticle.m_end_size.x = m_min_end_size.x + (endSizeRand * (m_max_end_size.x - m_min_end_size.x));
		newParticle.m_end_size.y = m_min_end_size.y + (endSizeRand * (m_max_end_size.y - m_min_end_size.y));
		newParticle.m_end_size.z = m_min_end_size.z + (endSizeRand * (m_max_end_size.z - m_min_end_size.z));

		if (is_model_emitter()) {
			blk::log("StartSize = %f %f %f", newParticle.m_start_size.x, newParticle.m_start_size.y, newParticle.m_start_size.z);
			blk::log("End = %f %f %f", newParticle.m_end_size.x, newParticle.m_end_size.y, newParticle.m_end_size.z);
		}

		newParticle.m_randoms[0] = kbfrand();
		newParticle.m_randoms[1] = kbfrand();
		newParticle.m_randoms[2] = kbfrand();

		newParticle.m_start_rotation = kbfrand(m_min_start_rotation_rate, m_max_start_rotation_rate);
		newParticle.m_end_rotation = kbfrand(m_min_end_rotation_rate, m_max_end_rotation_rate);

		if (newParticle.m_start_rotation != 0 || newParticle.m_end_rotation != 0) {
			newParticle.m_rotation = kbfrand() * kbPI;
		} else {
			newParticle.m_rotation = 0;
		}

		if (m_burst_count > 0) {
			m_burst_count--;
		} else {
			TimeLeft -= NextSpawn;
			NextSpawn = invMaxSpawnRate + (kbfrand() * (invMinSpawnRate - invMaxSpawnRate));
		}

		m_num_particles_emitted++;
		m_Particles.push_back(newParticle);
	}


	//blk::log( "Num Indices = %d", m_NumIndicesInCurrentBuffer );
	m_left_over_time = NextSpawn - TimeLeft;
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

	if (g_UseEditor && IsEnabled() == true && (m_total_duration > 0.0f && m_time_alive > m_total_duration && m_burst_count <= 0)) {
		stop_particle_system();
		Enable(false);
		Enable(true);
		return;
	}

	if (IsEnabled() == false || (m_total_duration > 0.0f && m_time_alive > m_total_duration && m_render_object.m_VertBufferIndexCount == 0)) {
		stop_particle_system();
		Enable(false);
		return;
	}

	if (is_model_emitter()) {
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
		m_is_spawning = true;
		m_num_particles_emitted = 0;

		if (m_start_delay > 0) {
			m_start_delay_remaining = m_start_delay;
		} else {
			m_time_alive = 0.0f;
			if (m_max_burst_count > 0) {
				m_burst_count = m_min_burst_count;
				if (m_max_burst_count > m_min_burst_count) {
					m_burst_count += rand() % (m_max_burst_count - m_min_burst_count);
				}
			}
		}

		for (int i = 0; i < m_model_emitter.size(); i++) {
			m_model_emitter[i].Init();
		}

			} else {
#ifdef DX11_PARTICLES
		g_pRenderer->RemoveParticle(m_render_object);
#endif

		if (g_renderer != nullptr) {
			g_renderer->remove_render_component(this);
		}

		m_Particles.clear();
		m_left_over_time = 0.0f;
	}
		}

/// ParticleComponent::EnableNewSpawns
void ParticleComponent::enable_new_spawns(const bool bEnable) {
	if (m_is_spawning == bEnable) {
		return;
	}

	m_is_spawning = bEnable;
	m_left_over_time = 0.0f;
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
