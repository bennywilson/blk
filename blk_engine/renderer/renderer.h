/// renderer.h
///
/// 2025 blk

#pragma once

#include <set>
#include <functional>
#include "Matrix.h"
#include "Quaternion.h"
#include "render_defs.h"
#include "render_graph.h"
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

	// Forwards a raw Win32 message to the active backend's platform/UI input
	// handling (Phase 3, Milestone 1: Dear ImGui's Win32 WndProc hook, only
	// wired up in Renderer_Dx12). Lets callers outside blk_engine (blaise's
	// WndProc) stay unaware that ImGui exists -- same seam as render()/
	// initialize(). Returns true if the caller's own WndProc should treat
	// the message as consumed.
	bool handle_platform_message(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);

	// Phase 3, Milestone 2: lets whoever owns the editor (kbEditor) register
	// its ImGui panel drawing without the renderer needing to know kbEditor
	// exists -- same one-way editor/->renderer/ dependency direction as
	// everywhere else. Renderer_Dx12::render_ui_overlay() calls this each
	// frame the "ui_overlay" pass runs; falls back to the ImGui demo window
	// when nothing is registered.
	void set_ui_draw_callback(std::function<void()> cb) { m_ui_draw_callback = std::move(cb); }

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

	// Phase 3 (viewport click-to-select). Asks the backend to read back the
	// entity id its gbuffer wrote at one pixel. Coordinates are BACKBUFFER
	// pixels (frame_width x frame_height), not window-client pixels -- with the
	// swapchain fixed at 1920x1080 against a smaller client rect those differ,
	// and ImGui's io.DisplayFramebufferScale is exactly the ratio between them.
	//
	// Split request/take rather than returning an id directly: the read can
	// only happen once the frame carrying the copy has been submitted, so the
	// answer belongs to a later call. try_take_entity_id_pick() returns true at
	// most once per request and clears the result; out_entity_id is
	// invalid_entity_id() when the pixel held no entity. Both no-op on a
	// backend with no picking support (software, Vulkan), which simply never
	// produces a result.
	static constexpr u32 invalid_entity_id() { return UINT32_MAX; }
	virtual void request_entity_id_pick(const u32 backbuffer_x, const u32 backbuffer_y) {}
	virtual bool try_take_entity_id_pick(u32& out_entity_id) { return false; }

	// The dimensions everything is actually rendered at -- the swapchain's
	// fixed size, which is NOT the window's client size (the present stretches
	// one to the other). Editor code needs these to reproduce the projection
	// render() used, and previously had to hardcode 1920x1080 to do it.
	u32 frame_width() const { return m_frame_width; }
	u32 frame_height() const { return m_frame_height; }

	// Aspect ratio render() builds its projection with. Kept as its own
	// accessor because it is the subtle one: a screen-space overlay (the
	// editor gizmo) must project with THIS, not with the aspect of the
	// rectangle it draws into, or it won't line up with the rendered scene
	// while the swapchain is stretching.
	f32 render_aspect_ratio() const { return (m_frame_height > 0) ? (m_frame_width / (f32)m_frame_height) : 1.0f; }

protected:
	RenderBuffer* get_render_buffer(const size_t& buffer_index) { return m_render_buffers[buffer_index]; }

	// todo make const
	std::set<const RenderComponent*>& render_components() {
		return m_render_components;
	}

	std::set<const LightComponent*>& light_components() {
		return m_light_components;
	}

	std::function<void()> m_ui_draw_callback;

private:
	virtual void initialize_internal(HWND hwnd, const uint32_t frame_width, const uint32_t frame_height) = 0;
	virtual void shut_down_internal() = 0;

	virtual void add_render_component_internal(const RenderComponent* const) {}
	virtual void remove_render_component_internal(const RenderComponent* const) {}

	// See the public handle_platform_message() wrapper. No-op by default.
	virtual bool handle_platform_message_internal(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) { return false; }

	// Temp whole-frame custom draw (currently the software rasterizer path only)
	// not part of the render graph, no per-view iteration, no barriers.
	virtual void render_custom_internal(const RenderCamera& camera) {}

	// Runs frame_pass_topology() against this backend: for each declared
	// pass (and associated ViewContext=o), asks get_pass_execute() for
	// an ExecuteFn and resolve_graph_resource() for its declared reads/
	// writes, then hands the assembled RenderGraph to emit_barriers(). A
	// backend that returns nullptr from get_pass_execute() for a given pass
	// name opts that pass out entirely for this frame
	void run_render_graph(const std::vector<ViewContext>& views);

	// Refreshes any per-frame native resource state (e.g. which double-
	// buffer copy is active) before resolve_graph_resource() is asked to
	// resolve this frame's passes. No-op by default.
	virtual void begin_frame_resources() {}

	// Resolves a logical frame resource to this backend's per-frame
	// GraphResource. Returning nullptr (the default) means the backend has
	// no such resource; any pass read/write that resolves to nullptr is
	// silently dropped from that pass's declared IO.
	virtual GraphResource* resolve_graph_resource(EFrameResource target) { return nullptr; }

	// Returns the execution callback for a named pass (see
	// frame_pass_topology()), or nullptr to opt out of that pass.
	virtual RenderGraph::ExecuteFn get_pass_execute(const std::string& pass_name, const std::vector<ViewContext>& views, size_t view_index) { return nullptr; }

	// Translates a batch of graph-derived transitions into real barriers.
	// No-op by default.
	virtual void emit_barriers(const std::vector<GraphTransition>& transitions) {}

	// Brackets each pass with a PIX/RenderDoc debug event region named after
	// its frame_pass_topology() entry (see run_render_graph()), so a capture's
	// event browser groups draws under "gbuffer"/"shadow_cascades"/"lights"/
	// etc. instead of one undifferentiated draw list. No-op by default.
	virtual void push_debug_marker(const char* const name) {}
	virtual void pop_debug_marker() {}

	// The graph's pass list/order and each pass's resource dependencies,
	// shared by every backend -- see run_render_graph().
	static const std::vector<RenderPassDecl>& frame_pass_topology();

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

	// Published once per frame from the primary RenderCamera built in
	// render(). Passes take an explicit RenderCamera parameter and should not
	// read this; it exists for cross-thread consumers (the gaussian-splat
	// sort thread) that need the current view matrix outside the render call
	// chain.
	Mat4 m_view_matrix;


private:
	std::unordered_map<std::string, RenderPipeline*> m_pipelines;
	std::vector<class RenderBuffer*> m_render_buffers;
	std::unordered_map<std::string, u32> m_textures;

	std::set<const RenderComponent*> m_render_components;
	std::set<const LightComponent*> m_light_components;
};

extern Renderer* g_renderer;
