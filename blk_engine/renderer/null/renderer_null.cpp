/// renderer_null.cpp
///
/// 2026 blk

#include "blk_core.h"
#include "entity_header.h"
#include "renderer_null.h"

#if defined(__EMSCRIPTEN__)
	#include <emscripten/emscripten.h>
#endif

/// NullPipeline
///
/// `RenderPipeline` is abstract, so `load_pipeline()` needs something concrete
/// to hand back. Returning nullptr instead would make every call site log an
/// "Unable to load pipeline" warning, which would bury the spike's real output.
namespace {
	class NullPipeline : public RenderPipeline {
	public:
		void release() override {}
	};

	/// NullRenderBuffer
	///
	/// Backed by CPU memory, because `RenderBuffer::write_vertex_buffer()` copies
	/// straight into `map()` - the base class's `nullptr` would crash the first
	/// model load. It also makes `log_stats()` an honest count of what a real
	/// backend would have been asked to upload.
	class NullRenderBuffer : public RenderBuffer {
	public:
		u8* map() override { return m_storage.data(); }

		void release() override {
			m_storage.clear();
			m_storage.shrink_to_fit();
		}

	private:
		void create_internal() override { m_storage.resize(size_bytes()); }

		std::vector<u8> m_storage;
	};
}

/// Renderer_Null::initialize_internal
void Renderer_Null::initialize_internal(HWND hwnd, const uint32_t frame_width, const uint32_t frame_height) {
	blk::log("Renderer_Null - initialized at %ux%u (no GPU device, no passes)", frame_width, frame_height);
}

/// Renderer_Null::shut_down_internal
void Renderer_Null::shut_down_internal() {
	blk::log("Renderer_Null - shut down after %u frames", m_frame_index);
}

/// Renderer_Null::present
///
/// The whole point of the spike: prove the core reaches this function every
/// frame. On the web that means touching the real canvas, so the result is
/// something you can see rather than a number in a log.
void Renderer_Null::present() {
	m_frame_index++;

	// A slow hue cycle, so a stalled loop looks different from a running one.
	const f32 t = m_frame_index * 0.01f;
	const int r = (int)(127.5f * (1.0f + sinf(t)));
	const int g = (int)(127.5f * (1.0f + sinf(t + 2.094f)));
	const int b = (int)(127.5f * (1.0f + sinf(t + 4.189f)));

#if defined(__EMSCRIPTEN__)
	MAIN_THREAD_EM_ASM({
		// Guard `document` itself, not just the element: the same build is run
		// under node to check the frame loop advances without a browser's
		// requestAnimationFrame throttling, and there is no DOM there at all.
		if (typeof document === 'undefined') {
			return;
		}

		const canvas = document.getElementById('canvas');
		if (!canvas) {
			return;
		}

		const context = canvas.getContext('2d');
		if (!context) {
			return;
		}

		context.fillStyle = 'rgb(' + $0 + ',' + $1 + ',' + $2 + ')';
		context.fillRect(0, 0, canvas.width, canvas.height);

		context.fillStyle = '#fff';
		context.font = '16px monospace';
		context.fillText('blk_engine - null renderer - frame ' + $3, 16, 28);
	},
		r, g, b, m_frame_index);
#else
	(void)r;
	(void)g;
	(void)b;
#endif

	// Cheap liveness trace for the native/headless case, where there is no
	// canvas to look at. Once a second at 60fps.
	if ((m_frame_index % 60) == 0) {
		log_stats();
	}
}

/// Renderer_Null::log_stats
void Renderer_Null::log_stats() {
	size_t buffer_bytes = 0;
	for (const RenderBuffer* const buffer : m_buffers) {
		buffer_bytes += buffer->size_bytes();
	}

	blk::log("Renderer_Null - frame %u | %zu render components, %zu lights | %zu buffers (%.1f MB) | %u textures, %u pipelines requested",
		m_frame_index,
		render_components().size(),
		light_components().size(),
		m_buffers.size(),
		buffer_bytes / (1024.0 * 1024.0),
		m_texture_requests,
		m_pipeline_requests);
}

/// Renderer_Null::load_texture
u32 Renderer_Null::load_texture(const std::string& path, LoadTextureParams& params) {
	m_texture_requests++;
	return 0;
}

/// Renderer_Null::create_gpu_pipeline
RenderPipeline* Renderer_Null::create_gpu_pipeline(const std::string& friendly_name, const std::string& path) {
	m_pipeline_requests++;
	return new NullPipeline();
}

/// Renderer_Null::create_compute_pipeline
RenderPipeline* Renderer_Null::create_compute_pipeline(const std::string& friendly_name, const std::string& path) {
	m_pipeline_requests++;
	return new NullPipeline();
}

/// Renderer_Null::create_render_buffer_internal
RenderBuffer* Renderer_Null::create_render_buffer_internal() {
	RenderBuffer* const buffer = new NullRenderBuffer();
	m_buffers.push_back(buffer);
	return buffer;
}
