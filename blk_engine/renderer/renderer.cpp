/// renderer.cpp
///
/// 2025 blk 1.0

#include "blk_core.h"
#include "entity_header.h"
#include "render_component.h"
#include "renderer.h"

// Math-only; no dependency on the D3D12 device/API itself
#include <DirectXMath.h>
using namespace DirectX;

Renderer* g_renderer = nullptr;

extern const f32 g_near_clip_plane = 1.f;
extern const f32 g_far_clip_plane = 20000.f;
extern const f32 g_fov = kbToRadians(80.f);

/// Renderer::Renderer
Renderer::Renderer() :
	m_frame_width(0),
	m_frame_height(0),
	m_view_position(0.f, 0.f, 0.f),
	m_view_rotation(0.f, 0.f, 0.f, 1.f) {
	g_renderer = this;
	m_projection_matrix.make_identity();
}

/// Renderer::~Renderer
Renderer::~Renderer() {
}

/// Renderer::initialize
void Renderer::initialize(HWND hwnd, const uint32_t frame_width, const uint32_t frame_height) {
	m_frame_width = frame_width;
	m_frame_height = frame_height;

	m_projection_matrix.make_identity();
	m_projection_matrix.create_perspective_matrix(
		kbToRadians(75.),
		m_frame_height / (float)m_frame_width,
		0.1f, 10000.f
	);

	initialize_internal(hwnd, frame_width, frame_height);
}

/// Renderer::shut_down
void Renderer::shut_down() {
	for (auto& pipe : m_pipelines) {
		delete pipe.second;
	}
	m_pipelines.clear();

	for (size_t i = 0; i < m_render_buffers.size(); i++) {
		m_render_buffers[i]->release();
		delete m_render_buffers[i];
	}
	m_render_buffers.clear();

	shut_down_internal();
}

/// Renderer::set_camera_transform
void Renderer::set_camera_transform(const Vec3& position, const Quat4& rotation) {
	m_view_position = position;
	m_view_rotation = rotation;
}

/// Renderer::create_render_buffer
RenderBuffer* Renderer::create_render_buffer() {
	RenderBuffer* const buffer = create_render_buffer_internal();
	m_render_buffers.push_back(buffer);
	return buffer;
}

/// Renderer::load_pipeline
RenderPipeline* Renderer::load_pipeline(const ERenderPipelineType& type, const std::string& friendly_name, const std::string& path) {
	RenderPipeline* new_pipeline = nullptr;
	if (type == ERenderPipelineType::Gpu) {
		new_pipeline = create_gpu_pipeline(friendly_name, path);
	} else {
		new_pipeline = create_compute_pipeline(friendly_name, path);
	}

	if (new_pipeline == nullptr) {
		blk::warn("Unable to load pipeline %s", path.c_str());
		return nullptr;
	}
	m_pipelines[friendly_name] = new_pipeline;

	return new_pipeline;
}

/// Renderer::get_pipeline
RenderPipeline* Renderer::get_pipeline(const std::string& name) {
	if (m_pipelines.find(name) == m_pipelines.end()) {
		return nullptr;
	}

	return m_pipelines[name];
}

/// Renderer::add_render_component
void Renderer::add_render_component(const RenderComponent* render_comp) {
	m_render_components.insert(render_comp);
	add_render_component_internal(render_comp);
}

/// Renderer::remove_render_component
void Renderer::remove_render_component(const RenderComponent* const render_comp) {
	m_render_components.erase(render_comp);
	remove_render_component_internal(render_comp);
}

/// Renderer::add_light_component
void Renderer::add_light_component(const LightComponent* const light_comp) {
	m_light_components.insert(light_comp);
}

/// Renderer::remove_light_component
void Renderer::remove_light_component(const LightComponent* const light_comp) {
	m_light_components.erase(light_comp);
}

/// Renderer::render
void Renderer::render() {

	const Mat4 trans = Mat4::make_translation(-m_view_position);
	Mat4 rot = m_view_rotation.to_mat4();
	rot.transpose_self();

	m_view_matrix = trans * rot;

	m_projection_matrix.make_identity();
	m_projection_matrix.create_perspective_matrix(
		g_fov,
		m_frame_width / (f32)m_frame_height,
		g_near_clip_plane,
		g_far_clip_plane
	);

	m_view_projection_matrix = m_view_matrix * m_projection_matrix;
	XMMATRIX inv_vp_matrix = XMMatrixInverse(nullptr, (*(XMMATRIX*)&m_view_projection_matrix));
	m_inv_view_projection_matrix = (*(Mat4*)&inv_vp_matrix);

	render_custom_internal();

	render_gbuffer_internal();
	
	render_shadows();

	render_lights_internal();

	render_point_clouds();

	render_transluency_internal();

	present();
}
