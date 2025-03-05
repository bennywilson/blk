/// ParticleComponent.cpp
///
/// 2016-2025 blk 1.0
#include "blk_core.h"
#include "blk_containers.h"
#include "Matrix.h"
#include "kbRenderer_defs.h"
#include "kbGameEntityHeader.h"
#include "kbRenderer.h"
#include "renderer.h"

using namespace std;

KB_DEFINE_COMPONENT(ParticleComponent)

static const uint NumParticleBufferVerts = 10000;
static const uint NumMeshVerts = 10000;

/// Particle_t::shut_down
void Particle_t::shut_down() {
	if (m_model != nullptr) {
		g_renderer->remove_render_component(m_model);
	}

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
	stop_system();

	// Dx12
	for (i32 i = 0; i < NumParticleBuffers; i++) {
		m_sprites[i].Release();
	}
}

/// ParticleComponent::stop_system
void ParticleComponent::stop_system() {
	if (is_model_emitter()) {
		return;
	}

	if (g_renderer) {
		g_renderer->remove_render_component(this);
	}

	// Dx12
	for (u32 i = 0; i < NumParticleBuffers; i++) {
		if (m_sprites[i].IsVertexBufferMapped()) {
			m_sprites[i].UnmapVertexBuffer(0);
		}

		if (m_sprites[i].IsIndexBufferMapped()) {
			m_sprites[i].UnmapIndexBuffer();		// todo : don't need to map/remap index buffer
		}
	}
	m_buffer_to_fill = -1;
	m_buffer_to_render = -1;
	m_Particles.clear();
	m_left_over_time = 0.0f;

	//m_billboard_type = BT_FaceCamera;
}

/// ParticleComponent::update_internal
void ParticleComponent::update_internal(const f32 DeltaTime) {
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

	if (is_model_emitter()  && m_model_emitter.size() == 0) {
		return;
	}

	const f32 eps = 0.00000001f;
	if (m_max_burst_count <= 0 && (m_max_particle_spawn_rate <= eps || m_min_spawn_rate < eps || m_max_particle_spawn_rate < m_min_spawn_rate || m_min_duration <= eps)) {
		return;
	}

	Vec3 currentCameraPosition;
	Quat4 currentCameraRotation;
	g_pRenderer->GetRenderViewTransform(nullptr, currentCameraPosition, currentCameraRotation);

	int iVertex = 0;
	int curVBPosition = 0;
	const Vec3 scale = GetScale();
	const Vec3 direction = rotation().to_mat4()[2].ToVec3();
	u8 iBillboardType = 0;
	switch (m_billboard_type) {
		case EBillboardType::BT_FaceCamera: iBillboardType = 0; break;
		case EBillboardType::BT_AxialBillboard: iBillboardType = 1; break;
		case EBillboardType::BT_AlignAlongVelocity: iBillboardType = 1; break;
		default: blk::warn("ParticleComponent::update_internal() - Invalid billboard type specified"); break;
	}

	kbParticleVertex* pDstVerts = nullptr;

	for (int i = (int)m_Particles.size() - 1; i >= 0; i--) {
		Particle_t& particle = m_Particles[i];

		if (particle.m_life_left >= 0.0f) {
			particle.m_life_left -= DeltaTime;

			if (particle.m_life_left <= 0.0f) {
				particle.shut_down();
				blk::std_remove_idx_swap(m_Particles, i);
				continue;
			}
		}
	}

	m_render_object.m_VertBufferIndexCount = (uint)m_Particles.size() * 6;

	for (int i = (int)m_Particles.size() - 1; i >= 0; i--) {
		Particle_t& particle = m_Particles[i];
		const f32 normalizedTime = (particle.m_total_life - particle.m_life_left) / particle.m_total_life;
		Vec3 curVelocity = Vec3::zero;

		if (m_velocity_over_life_curve.size() == 0) {
			curVelocity = kbLerp(particle.m_start_velocity, particle.m_end_velocity, normalizedTime);
		} else {
			const f32 velCurve = kbAnimEvent::Evaluate(m_velocity_over_life_curve, normalizedTime);
			curVelocity = particle.m_start_velocity * velCurve;
		}

		curVelocity += m_gravity * (particle.m_total_life - particle.m_life_left);

		particle.m_position = particle.m_position + curVelocity * DeltaTime;

		const f32 curRotationRate = kbLerp(particle.m_start_rotation, particle.m_end_rotation, normalizedTime);
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

		if (particle.m_model != nullptr) {
			kbStaticModelComponent* const model = particle.m_model;
			model->SetPosition(particle.m_position);
			model->set_material_param_vec4(0, "time", Vec4(normalizedTime, 0.0f, 0.0f, 0.0f));

		} else {
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

		iVertex += 4;
		curVBPosition++;
	}

	m_time_alive += DeltaTime;
	if (m_total_duration > 0.0f && m_time_alive > m_total_duration && m_burst_count <= 0) {
		return;
	}

	const f32 inv_min_spawn_rate = (m_min_spawn_rate > 0.0f) ? (1.0f / m_min_spawn_rate) : (0.0f);
	const f32 inv_max_spawn_rate = (m_max_particle_spawn_rate > 0.0f) ? (1.0f / m_max_particle_spawn_rate) : (0.0f);
	f32 time_left = DeltaTime - m_left_over_time;
	f32 next_spawn = 0.0f;

	Vec3 particle_position = GetPosition();
	const Mat4 owner_rotation = GetOwner()->rotation().to_mat4();

	// Spawn particles
	while (m_is_spawning && ((m_max_particle_spawn_rate > 0 && time_left >= next_spawn) || m_burst_count > 0) && (m_max_particles_to_emit <= 0 || m_num_particles_emitted < m_max_particles_to_emit)) {
		if (m_min_start_3d_offset.compare(Vec3::zero) == false || m_max_start_3d_offset.compare(Vec3::zero) == false) {
			const Vec3 startingOffset = Vec3Rand(m_min_start_3d_offset, m_max_start_3d_offset);
			particle_position += startingOffset;
		}

		Particle_t new_particle;
		new_particle.m_start_velocity = Vec3Rand(m_min_start_velocity, m_max_start_velocity) * owner_rotation;
		new_particle.m_end_velocity = Vec3Rand(m_min_end_velocity, m_max_end_velocity) * owner_rotation;

		new_particle.m_position = particle_position + new_particle.m_start_velocity * time_left;
		new_particle.m_life_left = m_min_duration + kbfrand() * (m_max_duration - m_min_duration);
		new_particle.m_total_life = new_particle.m_life_left;

		new_particle.m_start_size = kbLerp(m_min_start_size, m_max_start_size, kbfrand());
		new_particle.m_end_size = kbLerp(m_min_end_size, m_max_end_size, kbfrand());

		new_particle.m_randoms[0] = kbfrand();
		new_particle.m_randoms[1] = kbfrand();
		new_particle.m_randoms[2] = kbfrand();

		new_particle.m_start_rotation = kbfrand(m_min_start_rotation_rate, m_max_start_rotation_rate);
		new_particle.m_end_rotation = kbfrand(m_min_end_rotation_rate, m_max_end_rotation_rate);

		if (new_particle.m_start_rotation != 0 || new_particle.m_end_rotation != 0) {
			new_particle.m_rotation = kbfrand() * kbPI;
		} else {
			new_particle.m_rotation = 0;
		}

		if (is_model_emitter()) {
			kbStaticModelComponent* model_particle = new kbStaticModelComponent();
			model_particle->set_model(m_model_emitter[0].model());
			model_particle->set_material_param_vec4(0, "time", Vec4(0.0f, 0.0f, 0.0f, 0.0f));

			new_particle.m_model = model_particle;
			new_particle.m_model->SetPosition(new_particle.m_position);

			Vec4 rotation_3d = Vec4Rand(m_min_start_3d_rotation, m_max_start_3d_rotation);
			const Quat4 rotation = Quat4(rotation_3d.x, rotation_3d.y, rotation_3d.z, rotation_3d.w).normalize_safe();
			new_particle.m_model->SetOrientation(rotation);

			g_renderer->add_render_component(model_particle);
		}

		if (m_burst_count > 0) {
			m_burst_count--;
		} else {
			time_left -= next_spawn;
			next_spawn = inv_max_spawn_rate + (kbfrand() * (inv_min_spawn_rate - inv_max_spawn_rate));
		}

		m_num_particles_emitted++;
		m_Particles.push_back(new_particle);
	}

	m_left_over_time = next_spawn - time_left;
}

/// ParticleComponent::editor_change
void ParticleComponent::editor_change(const std::string& property_name) {
	Super::editor_change(property_name);

	// Editor Hack!
	if (property_name == "Materials") {
		for (int i = 0; i < this->m_materials.size(); i++) {
			m_materials[i].SetOwningComponent(this);
		}
	} else if (property_name == "DebugPlayEntity") {
		kbGameEntity* const owner = GetOwner();
		const size_t num_components = (int)owner->NumComponents();
		for (i32 i = 0; i < num_components; i++) {
			ParticleComponent* const particle = owner->GetComponent(i)->GetAs<ParticleComponent>();
			if (particle == nullptr) {
				continue;
			}

			particle->Enable(!particle->IsEnabled());
		}
	}
}

/// ParticleComponent::render_sync
void ParticleComponent::render_sync() {
	Super::render_sync();

	if (g_UseEditor && IsEnabled() == true && (m_total_duration > 0.0f && m_time_alive > m_total_duration && m_burst_count <= 0)) {
		stop_system();
		Enable(false);
		Enable(true);
		return;
	}

	if (IsEnabled() == false || (m_total_duration > 0.0f && m_time_alive > m_total_duration && m_render_object.m_VertBufferIndexCount == 0)) {
		stop_system();
		Enable(false);
		return;
	}

	if (is_model_emitter()) {
		return;
	}

	if (g_renderer != nullptr) {
		if (m_sprites[0].NumVertices() == 0) {
			for (u32 i = 0; i < NumParticleBuffers; i++) {
				kbModel& sprites = m_sprites[i];
				sprites.create_dynamic(NumParticleBufferVerts, NumParticleBufferVerts);

				m_vertex_buffer = (ParticleVertex*)sprites.map_vertex_buffer();
				for (u32 idx = 0; idx < NumParticleBufferVerts; idx++) {
					m_vertex_buffer[idx].position.set(0.0f, 0.0f, 0.0f);
					m_vertex_buffer[idx].uv.set(0.f, 0.f);
				}
				sprites.unmap_vertex_buffer();

				m_index_buffer = (u16*)sprites.map_index_buffer();
				for (u32 index_buf = 0, vertex_buf = 0; index_buf < NumParticleBufferVerts; index_buf += 6, vertex_buf += 4) {
					m_index_buffer[index_buf + 0] = vertex_buf + 2;
					m_index_buffer[index_buf + 1] = vertex_buf + 1;
					m_index_buffer[index_buf + 2] = vertex_buf + 0;
					m_index_buffer[index_buf + 3] = vertex_buf + 3;
					m_index_buffer[index_buf + 4] = vertex_buf + 2;
					m_index_buffer[index_buf + 5] = vertex_buf + 0;
				}
				sprites.unmap_index_buffer();
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
		if (g_renderer != nullptr) {
			m_sprites[m_buffer_to_fill].unmap_vertex_buffer();
		}
	}

	m_buffer_to_fill++;
	if (m_buffer_to_fill >= NumParticleBuffers) {
		m_buffer_to_fill = 0;
	}

	if (g_renderer != nullptr) {
		m_vertex_buffer = (ParticleVertex*)m_sprites[m_buffer_to_fill].map_vertex_buffer();
		memset(m_vertex_buffer, 0, sizeof(vertexLayout) * m_sprites[m_buffer_to_fill].NumVertices());
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

/*		for (int i = 0; i < m_model_emitter.size(); i++) {
			m_model_emitter[i].Init();
		}*/

	} else {
		if (g_renderer != nullptr) {
			g_renderer->remove_render_component(this);
		}

		for (auto& particle : m_Particles) {
			if (particle.m_model != nullptr) {
				g_renderer->remove_render_component(particle.m_model);
			}
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
		const kbMaterialComponent& mat_comp = m_materials[i];

		kbShaderParamOverrides_t dst_params;
		dst_params.m_shader = mat_comp.get_shader();

		const auto& src_params = mat_comp.shader_params();
		for (int j = 0; j < src_params.size(); j++) {
			if (src_params[j].texture() != nullptr) {
				dst_params.SetTexture(src_params[j].param_name().stl_str(), src_params[j].texture());
			} else if (src_params[j].render_texture() != nullptr) {
				dst_params.SetTexture(src_params[j].param_name().stl_str(), src_params[j].render_texture());
			} else {
				dst_params.SetVec4(src_params[j].param_name().stl_str(), src_params[j].vector());
			}
		}

		m_ShaderParams.push_back(dst_params);
	}
}
