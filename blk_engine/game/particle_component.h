/// ParticleComponent.h
///
/// 2016 blk
#pragma once
#include "model.h"

class RenderComponent;

/// EBillboardTypews
enum EBillboardType {
	BT_FaceCamera,
	BT_AxialBillboard,
	BT_AlignAlongVelocity
};

/// Particle_t
struct Particle_t {
	Particle_t& operator=(const Particle_t&) = default;

	void shut_down();

	Vec3 m_position = Vec3::zero;
	f32 m_rotation = 0.f;
	Vec3 m_start_size = Vec3(1.f, 1.f, 1.f);
	Vec3 m_end_size = Vec3(1.f, 1.f, 1.f);
	f32 m_life_left = 0.f;
	f32 m_total_life = 1.f;
	Vec3 m_start_velocity = Vec3::zero;
	Vec3 m_end_velocity = Vec3::zero;
	f32 m_start_rotation = 0.f;
	f32 m_end_rotation = 0.f;
	f32 m_randoms[3] = { 0.f, 0.f, 0.f };
	Vec3 m_rotation_axis = Vec3::zero;
	StaticModelComponent* m_model = nullptr;
};

/// ModelEmitter
class ModelEmitter : public GameComponent {
	BLK_DECLARE_COMPONENT(ModelEmitter, GameComponent);

public:
	void Init();

	const Model* model() const { return m_model; }
	const std::vector<ShaderParamOverrides_t> GetShaderParamOverrides() const { return m_ShaderParams; }

private:
	BLK_PROPERTY()
	Model* m_model;

	BLK_PROPERTY()
	std::vector<MaterialComponent> m_materials;

	std::vector<ShaderParamOverrides_t> m_ShaderParams;
};


/// ParticleComponent
class ParticleComponent : public RenderComponent {
	BLK_DECLARE_COMPONENT(ParticleComponent, RenderComponent);

public:
	virtual ~ParticleComponent();

	virtual void editor_change(const std::string& propertyName);

	virtual void render_sync();

	void stop_system();

	void enable_new_spawns(const bool bEnable);

	// Hack wasn't picking up from the package file
	void set_billboard_type(const EBillboardType inBBType) { m_billboard_type = inBBType; }

	bool is_model_emitter() const { return m_model_emitter.size() > 0 && m_model_emitter[0].model() != nullptr; }

	const Model* get_model() const {
		if (m_buffer_to_render != -1) {
			return &m_sprites[m_buffer_to_render];
		} else {
			return nullptr;
		}
	}

protected:
	virtual void enable_internal(const bool isEnabled) override;
	virtual void update_internal(const float DeltaTime) override;

private:
	// Editable
	// Shadows RenderComponent::m_materials, so it needs a key of its own: two fields
	// sharing one key would both load into the base's member.
	BLK_PROPERTY(SerializedAs = "MaterialList")
	std::vector<MaterialComponent> m_materials;

	BLK_PROPERTY()
	f32 m_total_duration;

	BLK_PROPERTY()
	i32 m_max_particles_to_emit;

	BLK_PROPERTY()
	f32 m_start_delay;

	BLK_PROPERTY(MinVal = 0)
	f32 m_min_spawn_rate;    // Particles per second

	BLK_PROPERTY(MinVal = 0)
	f32 m_max_spawn_rate;    // Particles per second

	BLK_PROPERTY()
	Vec3 m_min_start_velocity;

	BLK_PROPERTY()
	Vec3 m_max_start_velocity;

	BLK_PROPERTY()
	std::vector<AnimEvent> m_velocity_over_life_curve;

	BLK_PROPERTY()
	Vec3 m_min_end_velocity;

	BLK_PROPERTY()
	Vec3 m_max_end_velocity;

	BLK_PROPERTY()
	f32 m_min_start_rotation_rate;

	BLK_PROPERTY()
	f32 m_max_start_rotation_rate;

	BLK_PROPERTY()
	f32 m_min_end_rotation_rate;

	BLK_PROPERTY()
	f32 m_max_end_rotation_rate;

	// Vec4: spawn_particle() reads all four components as a quaternion, and that's what levels store.
	BLK_PROPERTY()
	Vec4 m_min_start_3d_rotation;

	BLK_PROPERTY()
	Vec4 m_max_start_3d_rotation;

	BLK_PROPERTY()
	Vec3 m_min_start_3d_offset;

	BLK_PROPERTY()
	Vec3 m_max_start_3d_offset;

	BLK_PROPERTY()
	Vec3 m_min_start_size;

	BLK_PROPERTY()
	Vec3 m_max_start_size;

	BLK_PROPERTY()
	Vec3 m_min_end_size;

	BLK_PROPERTY()
	Vec3 m_max_end_size;

	BLK_PROPERTY()
	f32 m_min_duration;

	BLK_PROPERTY()
	f32 m_max_duration;

	BLK_PROPERTY()
	Vec4 m_start_color;

	BLK_PROPERTY()
	Vec4 m_end_color;

	BLK_PROPERTY()
	std::vector<VectorAnimEvent> m_size_over_life_curve;

	BLK_PROPERTY()
	std::vector<VectorAnimEvent> m_rotation_over_life_curve;

	BLK_PROPERTY()
	std::vector<VectorAnimEvent> m_color_over_life_curve;

	BLK_PROPERTY()
	std::vector<AnimEvent> m_alpha_over_life_curve;

	BLK_PROPERTY()
	Vec3 m_gravity;

	BLK_PROPERTY()
	i32 m_min_burst_count;

	BLK_PROPERTY()
	i32 m_max_burst_count;

	BLK_PROPERTY()
	EBillboardType m_billboard_type;

	BLK_PROPERTY()
	std::vector<StaticModelComponent> m_model_emitter;
	// Shadows RenderComponent::m_render_order_bias and isn't reflected: it shared the
	// base's key, so saved values have only ever reached the base's member anyway.
	f32 m_render_order_bias;

	BLK_PROPERTY()
	bool m_debug_play_entity;

	// Non-editable
	f32 m_left_over_time;
	f32 m_time_alive;
	i32 m_burst_count;
	f32 m_start_delay_remaining;
	i32 m_num_particles_emitted;

	RenderObject m_render_object;
	std::vector<Particle_t> m_Particles;

	static const int NumParticleBuffers = 3;
	Model m_sprites[NumParticleBuffers];
	ParticleVertex* m_vertex_buffer;
	u16* m_index_buffer;

	u32 m_buffer_to_fill;
	u32 m_buffer_to_render;

	const ParticleComponent* m_template;
	bool m_is_pooled;
	bool m_is_spawning;
};
