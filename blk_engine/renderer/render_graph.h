/// render_graph.h
///
/// 2026 blk

#pragma once
#include <vector>
#include <functional>
#include <string>

// Backend-agnostic on purpose: A backend  owns the actual resource pointers as
// opaque `native_handle`s and supplies the TransitionsFn that turns a batch of
// GraphTransitions into real barriers.
enum class EGraphResourceState {
	// COMMON/PRESENT: the frame-start default, and the state every declared
	// read/write reverts to once its pass returns.
	Common,		
	RenderTarget,
	DepthWrite,
	CopySource,
	CopyDest,
};

/// GraphResource
///
/// One resource tracked across a single frame's graph execution. Constructed
/// fresh each frame (current_state always starts at Common, matching this
/// engine's existing PRESENT/COMMON-as-neutral-state convention) — the graph
/// does not own native_handle's lifetime.
struct GraphResource {
	void* native_handle = nullptr;
	EGraphResourceState current_state = EGraphResourceState::Common;
};

struct GraphTransition {
	GraphResource* resource;
	EGraphResourceState from;
	EGraphResourceState to;
};

struct PassIO {
	GraphResource* resource;
	EGraphResourceState state;
};

/// EFrameResource
///
/// Logical name for a resource the shared frame topology (see
/// Renderer::frame_pass_topology()) can declare a pass reads or writes.
/// Each backend maps these to whatever it actually owns in 
// Renderer::resolve_graph_resource().
enum class EFrameResource {
	Color,
	Normal,
	Specular,
	SceneDepth,
	Lighting,
	SceneColor,
	// Per-pixel entity id the gbuffer pass writes for viewport picking. A
	// backend with no picking support just returns nullptr for it from
	// resolve_graph_resource(), and run_render_graph() drops the reference.
	EntityId,
	ShadowDepth,
};

struct ResourceRef {
	EFrameResource resource;
	EGraphResourceState state;
};

/// RenderPassDecl
///
/// One entry in the frame's shared pass topology: a pass name, whether it
/// runs once per ViewContext or once for the whole frame, and its resource
/// dependencies. Backends never build this list themselves -- they only
/// answer Renderer::get_pass_execute()/resolve_graph_resource() for the
/// names and EFrameResource values declared here, and may leave any of them
/// unimplemented (Renderer::run_render_graph() skips a pass entirely when
/// get_pass_execute() returns nullptr for it).
struct RenderPassDecl {
	const char* name;
	bool per_view;
	std::vector<ResourceRef> reads;
	std::vector<ResourceRef> writes;
};

/// RenderGraph
///
/// v1: no scheduling, no transient-resource aliasing. Passes run in
/// insertion order (today's fixed sequence is already a valid order) and
/// resources are the engine's existing persistent per-frame-index targets.
/// The only thing this buys over hand-placed barriers: passes declare what
/// they touch, and the graph derives + batches the transitions instead of
/// each pass hand-writing its own ResourceBarrier calls.
class RenderGraph {
public:
	using ExecuteFn = std::function<void()>;

	// Most reads in this engine need no barrier at all: D3D12's implicit
	// COMMON promotion already covers an SRV read from a resource left at
	// Common, so a declared read at EGraphResourceState::Common is a no-op.
	// A read needing a different state (e.g. post_process's CopySource read
	// of SceneColor) gets a real transition in, and is reverted to Common again
	// once its pass returns.
	using TransitionsFn = std::function<void(const std::vector<GraphTransition>& transitions)>;

	// Brackets each pass with a PIX/RenderDoc debug event region named after
	// the pass (see Renderer::push_debug_marker()/pop_debug_marker()).
	// Optional -- a default-constructed std::function is a safe no-op.
	using BeginMarkerFn = std::function<void(const char* name)>;
	using EndMarkerFn = std::function<void()>;

	void add_pass(const char* name, std::vector<PassIO> reads, std::vector<PassIO> writes, ExecuteFn execute);

	// Runs passes in insertion order, then clears them. Before each pass,
	// batches transitions for any declared read/write not already in its
	// needed state; after, batches transitions of everything back to Common,
	// since a fresh GraphResource next frame always starts assuming Common.
	void execute(const TransitionsFn& transitions_fn, const BeginMarkerFn& begin_marker = {}, const EndMarkerFn& end_marker = {});

private:
	struct Pass {
		std::string name;
		std::vector<PassIO> reads;
		std::vector<PassIO> writes;
		ExecuteFn execute;
	};
	std::vector<Pass> m_passes;
};
