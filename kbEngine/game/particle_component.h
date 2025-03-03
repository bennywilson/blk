/// ParticleComponent.h
///
/// 2016-2025 blk 1.0
#pragma once
#include "kbModel.h"

/// EBillboardTypews
enum EBillboardType {
	BT_FaceCamera,
	BT_AxialBillboard,
	BT_AlignAlongVelocity
};

/// kbParticle_t
struct kbParticle_t {
	kbParticle_t();
	kbParticle_t(const kbParticle_t&) = default;
	~kbParticle_t();

	kbParticle_t& operator=(const kbParticle_t&) = default;

	void Shutdown();

	Vec3 m_position;
	float m_rotation;
	Vec3 m_start_size;
	Vec3 m_end_size;
	float m_life_left;
	float m_total_life;
	Vec3 m_start_velocity;
	Vec3 m_end_velocity;
	float m_start_rotation;
	float m_end_rotation;
	float m_randoms[3];
	Vec3 m_rotation_axis;
};

/// kbModelEmitter
class kbModelEmitter : public kbGameComponent {
	KB_DECLARE_COMPONENT(kbModelEmitter, kbGameComponent);

public:
	const kbModel* model() const { return m_model; }
	const std::vector<kbShaderParamOverrides_t>	GetShaderParamOverrides() const { return m_ShaderParams; }

	void Init();

private:
	kbModel* m_model;
	std::vector<kbMaterialComponent> m_materials;

	std::vector<kbShaderParamOverrides_t> m_ShaderParams;
};


/// ParticleComponent
class ParticleComponent : public RenderComponent {
	KB_DECLARE_COMPONENT(ParticleComponent, RenderComponent);

public:
	virtual	~ParticleComponent();

	virtual void editor_change(const std::string& propertyName);

	virtual void render_sync();

	void stop_particle_system();

	void enable_new_spawns(const bool bEnable);

	// Hack wasn't picking up from the package file
	void set_billboard_type(const EBillboardType inBBType) { m_billboard_type = inBBType; }

	bool is_model_emitter() const {return m_model_emitter.size() > 0 && m_model_emitter[0].model() != nullptr; }

	const kbModel* get_model() const {
		if (m_buffer_to_render != -1) {
			return &m_models[m_buffer_to_render];
		} else {
			return nullptr;
		}
	}

protected:
	virtual void enable_internal(const bool isEnabled) override;
	virtual void update_internal(const float DeltaTime) override;

private:
	// Editable
	std::vector<kbMaterialComponent> m_materials;
	f32 m_total_duration;
	i32	m_max_particles_to_emit;
	f32 m_start_delay;
	f32 m_min_spawn_rate;				// Particles per second
	f32 m_max_particle_spawn_rate;				// Particles per second
	Vec3 m_min_start_velocity;
	Vec3 m_max_start_velocity;
	std::vector<kbAnimEvent> m_velocity_over_life_curve;
	Vec3 m_min_end_velocity;
	Vec3 m_max_end_velocity;
	f32 m_min_start_rotation_rate;
	f32 m_max_start_rotation_rate;
	f32 m_min_end_rotation_rate;
	f32 m_max_end_rotation_rate;
	Vec3 m_min_start_3d_rotation;
	Vec3 m_max_start_3d_rotation;
	Vec3 m_min_start_3d_offset;
	Vec3 m_max_start_3d_offset;
	Vec3 m_min_start_size;
	Vec3 m_max_start_size;
	Vec3 m_min_end_size;
	Vec3 m_max_end_size;
	f32 m_min_duration;
	f32 m_max_duration;
	Vec4 m_start_color;
	Vec4 m_end_color;
	std::vector<kbVectorAnimEvent> m_size_over_life_curve;
	std::vector<kbVectorAnimEvent> m_rotation_over_life_curve;
	std::vector<kbVectorAnimEvent> m_color_over_life_curve;
	std::vector<kbAnimEvent> m_alpha_over_life_curve;
	Vec3 m_gravity;
	i32	m_min_burst_count;
	i32	m_max_burst_count;
	EBillboardType m_billboard_type;
	std::vector<kbModelEmitter>	m_model_emitter;
	f32	m_render_order_bias;
	bool m_debug_play_entity;

	// Non-editable
	f32 m_left_over_time;
	f32	m_time_alive;
	i32	m_burst_count;
	f32	m_start_delay_remaining;
	i32	m_num_particles_emitted;

	kbRenderObject m_render_object;
	std::vector<kbParticle_t> m_Particles;

	// Dx12
	static const int NumParticleBuffers = 3;
	kbModel m_models[NumParticleBuffers];
	ParticleVertex* m_vertex_buffer;
	u16* m_index_buffer;
	//

	u32 m_buffer_to_fill;
	u32 m_buffer_to_render;

	friend class kbParticleManager;
	const ParticleComponent* m_template;
	bool m_is_pooled;
	bool m_is_spawning;
};
