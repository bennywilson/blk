/// renderer.cpp
///
/// 2025 blk

#include "blk_core.h"
#include "entity_header.h"
#include "render_component.h"
#include "renderer.h"

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
}

/// Renderer::~Renderer
Renderer::~Renderer() {
}

/// Renderer::initialize
void Renderer::initialize(HWND hwnd, const uint32_t frame_width, const uint32_t frame_height) {
	m_frame_width = frame_width;
	m_frame_height = frame_height;

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

/// Renderer::handle_platform_message
bool Renderer::handle_platform_message(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
	return handle_platform_message_internal(hwnd, msg, wparam, lparam);
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

	const RenderCamera camera = make_render_camera(
		m_view_position, m_view_rotation,
		g_fov, m_frame_width / (f32)m_frame_height,
		g_near_clip_plane, g_far_clip_plane
	);

	// Published for cross-thread consumers (the gaussian-splat sort thread);
	// passes below take camera explicitly and should not read this member.
	m_view_matrix = camera.view_matrix;

	render_custom_internal(camera);

	// Single view today (the primary game camera); a later phase adds a
	// second ViewContext here for a portal or an editor viewport.
	const std::vector<ViewContext> views = { ViewContext{ camera } };
	run_render_graph(views);

	present();
}

/// Renderer::frame_pass_topology
///
/// gbuffer writes Color/Normal/Specular/SceneDepth; shadow_cascades writes
/// ShadowDepth; shadow_composite projects it into Lighting; lights/
/// point_clouds/translucency all accumulate into SceneColor (see the
/// SceneColor comment in renderer_dx12.h for why); post_process reads
/// SceneColor back out as the frame's final composite source.
const std::vector<RenderPassDecl>& Renderer::frame_pass_topology() {
	static const std::vector<RenderPassDecl> topology = {
		{ "gbuffer", true, {}, {
			{ EFrameResource::Color, EGraphResourceState::RenderTarget },
			{ EFrameResource::Normal, EGraphResourceState::RenderTarget },
			{ EFrameResource::Specular, EGraphResourceState::RenderTarget },
			{ EFrameResource::SceneDepth, EGraphResourceState::RenderTarget },
		} },
		{ "shadow_cascades", false, {}, {
			{ EFrameResource::ShadowDepth, EGraphResourceState::DepthWrite },
		} },
		{ "shadow_composite", false, {}, {
			{ EFrameResource::Lighting, EGraphResourceState::RenderTarget },
		} },
		{ "lights", true, {}, {
			{ EFrameResource::SceneColor, EGraphResourceState::RenderTarget },
		} },
		{ "point_clouds", false, {}, {
			{ EFrameResource::SceneColor, EGraphResourceState::RenderTarget },
		} },
		{ "translucency", true, {}, {
			{ EFrameResource::SceneColor, EGraphResourceState::RenderTarget },
		} },
		{ "post_process", false, {
			{ EFrameResource::SceneColor, EGraphResourceState::CopySource },
		}, {} },
		// Dear ImGui overlay. No declared reads/writes -- the back buffer
		// isn't a graph-tracked EFrameResource (see render_post_process's
		// own hand-managed transition), so this pass brackets its own
		// barriers the same way. A backend with no
		// get_pass_execute("ui_overlay", ...) override (Vulkan, software, or
		// D3D12 itself when g_imgui_enabled is off) simply skips it.
		{ "ui_overlay", false, {}, {} },
	};
	return topology;
}

/// Renderer::run_render_graph
void Renderer::run_render_graph(const std::vector<ViewContext>& views) {
	begin_frame_resources();

	RenderGraph graph;
	for (const RenderPassDecl& decl : frame_pass_topology()) {
		const size_t view_count = decl.per_view ? views.size() : 1;
		for (size_t view_index = 0; view_index < view_count; view_index++) {
			const RenderGraph::ExecuteFn execute = get_pass_execute(decl.name, views, view_index);
			if (!execute) {
				continue;
			}

			std::vector<PassIO> reads;
			reads.reserve(decl.reads.size());
			for (const ResourceRef& ref : decl.reads) {
				if (GraphResource* const resource = resolve_graph_resource(ref.resource)) {
					reads.push_back({ resource, ref.state });
				}
			}

			std::vector<PassIO> writes;
			writes.reserve(decl.writes.size());
			for (const ResourceRef& ref : decl.writes) {
				if (GraphResource* const resource = resolve_graph_resource(ref.resource)) {
					writes.push_back({ resource, ref.state });
				}
			}

			graph.add_pass(decl.name, std::move(reads), std::move(writes), execute);
		}
	}

	graph.execute(
		[this](const std::vector<GraphTransition>& transitions) { emit_barriers(transitions); },
		[this](const char* const name) { push_debug_marker(name); },
		[this]() { pop_debug_marker(); }
	);
}
