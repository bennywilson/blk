/// render_graph.cpp
///
/// 2026 blk 1.0

#include "render_graph.h"

/// RenderGraph::add_pass
void RenderGraph::add_pass(const char* name, std::vector<PassIO> reads, std::vector<PassIO> writes, ExecuteFn execute) {
	m_passes.push_back(Pass{ name, std::move(reads), std::move(writes), std::move(execute) });
}

/// RenderGraph::execute
void RenderGraph::execute(const TransitionsFn& transitions_fn) {
	for (Pass& pass : m_passes) {
		std::vector<GraphTransition> entry_transitions;
		auto collect_entry = [&](std::vector<PassIO>& ios) {
			for (PassIO& io : ios) {
				if (io.resource->current_state != io.state) {
					entry_transitions.push_back({ io.resource, io.resource->current_state, io.state });
					io.resource->current_state = io.state;
				}
			}
		};
		collect_entry(pass.reads);
		collect_entry(pass.writes);

		if (!entry_transitions.empty()) {
			transitions_fn(entry_transitions);
		}

		pass.execute();

		// Every resource this pass reads/writes reverts to Common before the next 
		// pass. A GraphResource is reconstructed fresh at the start of every frame,
		// so anything left in a non-Common state here would desync from the real
		// resource's state by next frame (e.g. a CopySource-only read, like
		// post_process's SceneColor read, would otherwise never revert).
		std::vector<GraphTransition> exit_transitions;
		auto collect_exit = [&](std::vector<PassIO>& ios) {
			for (PassIO& io : ios) {
				if (io.resource->current_state != EGraphResourceState::Common) {
					exit_transitions.push_back({ io.resource, io.resource->current_state, EGraphResourceState::Common });
					io.resource->current_state = EGraphResourceState::Common;
				}
			}
		};
		collect_exit(pass.reads);
		collect_exit(pass.writes);

		if (!exit_transitions.empty()) {
			transitions_fn(exit_transitions);
		}
	}

	m_passes.clear();
}
