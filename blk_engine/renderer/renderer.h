/// renderer.h
///
/// 2025 blk 1.0

#pragma once

#include <set>
#include "Matrix.h"
#include "Quaternion.h"
#include "render_defs.h"
#include "light_component.h"

class RenderComponent;
class RenderBuffer;

///	Renderer
class Renderer {
public:
	Renderer();
	virtual ~Renderer();

	static constexpr uint32_t max_frames() { return 2; }

	void initialize(HWND hwnd, const uint32_t frame_width, const uint32_t frame_height);
	void shut_down();

	virtual bool software_renderer() const {
		return false;
	}

	void render();

	RenderBuffer* create_render_buffer();

	RenderPipeline* load_pipeline(const ERenderPipelineType& type, const std::string& friendly_name, const std::string& path);
	RenderPipeline* get_pipeline(const std::string& friendly_name);

	struct LoadTextureParams {
		std::vector<Vec4>* texture_data = nullptr;
		u32 width = 0;
		u32 height = 0;
		bool cpu_accessible = false;
	};
	virtual u32 load_texture(const std::string& path, LoadTextureParams& params) = 0;

	void set_camera_transform(const Vec3& position, const Quat4& rotation);

	// 
	void add_render_component(const RenderComponent* const);
	void remove_render_component(const RenderComponent* const);

	void add_light_component(const LightComponent* const);
	void remove_light_component(const LightComponent* const);

protected:
	RenderBuffer* get_render_buffer(const size_t& buffer_index) { return m_render_buffers[buffer_index]; }

	// todo make const
	std::set<const RenderComponent*>& render_components() {
		return m_render_components;
	}

	std::set<const LightComponent*>& light_components() {
		return m_light_components;
	}

private:
	virtual void initialize_internal(HWND hwnd, const uint32_t frame_width, const uint32_t frame_height) = 0;
	virtual void shut_down_internal() = 0;

	virtual void add_render_component_internal(const RenderComponent* const) {}
	virtual void remove_render_component_internal(const RenderComponent* const) {}

	virtual void render_custom_internal() {}
	virtual void render_gbuffer_internal() {}
	virtual void render_lights_internal() {}
	virtual void render_transluency_internal() {}
	virtual void render_shadows() {}
	virtual void render_point_clouds() {}

	virtual void present() {};

	virtual RenderPipeline* create_gpu_pipeline(const std::string& friendly_name, const std::string& path) = 0;
	virtual RenderPipeline* create_compute_pipeline(const std::string& friendly_name, const std::string& path) = 0;
	virtual RenderBuffer* create_render_buffer_internal() = 0;

protected:
	u32 m_frame_width;
	u32 m_frame_height;

	/// camera
	Vec3 m_view_position;
	Quat4 m_view_rotation;
	Mat4 m_view_matrix;
	Mat4 m_projection_matrix;
	Mat4 m_view_projection_matrix;
	Mat4 m_inv_view_projection_matrix;


private:
	std::unordered_map<std::string, RenderPipeline*> m_pipelines;
	std::vector<class RenderBuffer*> m_render_buffers;
	std::unordered_map<std::string, u32> m_textures;

	std::set<const RenderComponent*> m_render_components;
	std::set<const LightComponent*> m_light_components;
};

extern Renderer* g_renderer;
