/// renderer_webgpu.cpp
///
/// 2026 blk

#include <cmath>
#include "blk_core.h"
#include "entity_header.h"
#include "renderer_webgpu.h"

namespace {
	/// Dawn hands back a pointer and a length, not a C string.
	std::string to_string(const WGPUStringView view) {
		return (view.data != nullptr && view.length > 0) ? std::string(view.data, view.length) : std::string();
	}

	/// Placeholders until the passes land - same reasoning as `Renderer_Null`:
	/// `RenderPipeline` is abstract, and `RenderBuffer::write_vertex_buffer()`
	/// memcpys into `map()`, so it needs real storage behind it.
	class WebGpuPipelineStub : public RenderPipeline {
	public:
		void release() override {}
	};

	class WebGpuBufferStub : public RenderBuffer {
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

	void on_uncaptured_error(WGPUDevice const*, WGPUErrorType type, WGPUStringView message, void*, void*) {
		blk::warn("Renderer_WebGpu - device error %d: %s", (int)type, to_string(message).c_str());
	}

	void on_device_lost(WGPUDevice const*, WGPUDeviceLostReason reason, WGPUStringView message, void*, void*) {
		blk::warn("Renderer_WebGpu - device lost %d: %s", (int)reason, to_string(message).c_str());
	}
}

/// Renderer_WebGpu::~Renderer_WebGpu
Renderer_WebGpu::~Renderer_WebGpu() {
}

/// Renderer_WebGpu::request_adapter
bool Renderer_WebGpu::request_adapter() {
	struct Result {
		WGPUAdapter adapter = nullptr;
		bool done = false;
	} result;

	WGPURequestAdapterOptions options = {};
	options.featureLevel = WGPUFeatureLevel_Core;
	options.compatibleSurface = m_surface;

	WGPURequestAdapterCallbackInfo callback = {};
	callback.mode = WGPUCallbackMode_AllowProcessEvents;
	callback.userdata1 = &result;
	callback.callback = [](WGPURequestAdapterStatus status, WGPUAdapter adapter, WGPUStringView message, void* userdata, void*) {
		Result* const out = (Result*)userdata;
		if (status != WGPURequestAdapterStatus_Success) {
			blk::warn("Renderer_WebGpu - requestAdapter failed: %s", to_string(message).c_str());
		}
		out->adapter = adapter;
		out->done = true;
	};

	wgpuInstanceRequestAdapter(m_instance, &options, callback);
	while (!result.done) {
		wgpuInstanceProcessEvents(m_instance);
	}

	m_adapter = result.adapter;
	return m_adapter != nullptr;
}

/// Renderer_WebGpu::request_device
bool Renderer_WebGpu::request_device() {
	struct Result {
		WGPUDevice device = nullptr;
		bool done = false;
	} result;

	WGPUDeviceDescriptor descriptor = {};
	descriptor.uncapturedErrorCallbackInfo.callback = on_uncaptured_error;
	descriptor.deviceLostCallbackInfo.mode = WGPUCallbackMode_AllowProcessEvents;
	descriptor.deviceLostCallbackInfo.callback = on_device_lost;

	WGPURequestDeviceCallbackInfo callback = {};
	callback.mode = WGPUCallbackMode_AllowProcessEvents;
	callback.userdata1 = &result;
	callback.callback = [](WGPURequestDeviceStatus status, WGPUDevice device, WGPUStringView message, void* userdata, void*) {
		Result* const out = (Result*)userdata;
		if (status != WGPURequestDeviceStatus_Success) {
			blk::warn("Renderer_WebGpu - requestDevice failed: %s", to_string(message).c_str());
		}
		out->device = device;
		out->done = true;
	};

	wgpuAdapterRequestDevice(m_adapter, &descriptor, callback);
	while (!result.done) {
		wgpuInstanceProcessEvents(m_instance);
	}

	m_device = result.device;
	return m_device != nullptr;
}

/// Renderer_WebGpu::initialize_internal
void Renderer_WebGpu::initialize_internal(HWND hwnd, const uint32_t frame_width, const uint32_t frame_height) {
	m_instance = wgpuCreateInstance(nullptr);
	blk::error_check(m_instance != nullptr, "Renderer_WebGpu - wgpuCreateInstance() failed");

	// The surface comes first: the adapter is requested as compatible with it.
	WGPUSurfaceSourceWindowsHWND from_hwnd = {};
	from_hwnd.chain.sType = WGPUSType_SurfaceSourceWindowsHWND;
	from_hwnd.hinstance = GetModuleHandle(nullptr);
	from_hwnd.hwnd = hwnd;

	WGPUSurfaceDescriptor surface_descriptor = {};
	surface_descriptor.nextInChain = &from_hwnd.chain;
	m_surface = wgpuInstanceCreateSurface(m_instance, &surface_descriptor);
	blk::error_check(m_surface != nullptr, "Renderer_WebGpu - wgpuInstanceCreateSurface() failed");

	blk::error_check(request_adapter(), "Renderer_WebGpu - no adapter");

	WGPUAdapterInfo info = {};
	wgpuAdapterGetInfo(m_adapter, &info);
	blk::log("Renderer_WebGpu - adapter '%s' (backend %d)", to_string(info.device).c_str(), (int)info.backendType);

	blk::error_check(request_device(), "Renderer_WebGpu - no device");
	m_queue = wgpuDeviceGetQueue(m_device);

	// Dawn lists the surface's preferred format first.
	WGPUSurfaceCapabilities capabilities = {};
	wgpuSurfaceGetCapabilities(m_surface, m_adapter, &capabilities);
	m_surface_format = (capabilities.formatCount > 0) ? capabilities.formats[0] : WGPUTextureFormat_BGRA8Unorm;

	WGPUSurfaceConfiguration configuration = {};
	configuration.device = m_device;
	configuration.format = m_surface_format;
	configuration.usage = WGPUTextureUsage_RenderAttachment;
	configuration.width = frame_width;
	configuration.height = frame_height;
	configuration.alphaMode = WGPUCompositeAlphaMode_Auto;
	configuration.presentMode = WGPUPresentMode_Fifo;
	wgpuSurfaceConfigure(m_surface, &configuration);

	blk::log("Renderer_WebGpu initialized - %ux%u, surface format %d", frame_width, frame_height, (int)m_surface_format);
}

/// Renderer_WebGpu::shut_down_internal
void Renderer_WebGpu::shut_down_internal() {
	if (m_surface != nullptr) {
		wgpuSurfaceUnconfigure(m_surface);
		wgpuSurfaceRelease(m_surface);
		m_surface = nullptr;
	}

	if (m_queue != nullptr) {
		wgpuQueueRelease(m_queue);
		m_queue = nullptr;
	}

	if (m_device != nullptr) {
		wgpuDeviceRelease(m_device);
		m_device = nullptr;
	}

	if (m_adapter != nullptr) {
		wgpuAdapterRelease(m_adapter);
		m_adapter = nullptr;
	}

	if (m_instance != nullptr) {
		wgpuInstanceRelease(m_instance);
		m_instance = nullptr;
	}

	blk::log("Renderer_WebGpu - shut down after %u frames", m_frame_index);
}

/// Renderer_WebGpu::present
///
/// Scaffolding for the first milestone: acquire the surface texture and clear
/// it. The clear colour cycles so a running frame loop is visible at a glance -
/// that goes away once the graph's passes draw something.
void Renderer_WebGpu::present() {
	if (m_surface == nullptr || m_device == nullptr) {
		return;
	}

	WGPUSurfaceTexture surface_texture = {};
	wgpuSurfaceGetCurrentTexture(m_surface, &surface_texture);
	if (surface_texture.status != WGPUSurfaceGetCurrentTextureStatus_SuccessOptimal &&
		surface_texture.status != WGPUSurfaceGetCurrentTextureStatus_SuccessSuboptimal) {
		blk::warn("Renderer_WebGpu - getCurrentTexture status %d", (int)surface_texture.status);
		return;
	}

	WGPUTextureView view = wgpuTextureCreateView(surface_texture.texture, nullptr);

	const f32 t = m_frame_index * 0.01f;
	WGPURenderPassColorAttachment color_attachment = {};
	color_attachment.view = view;
	color_attachment.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
	color_attachment.loadOp = WGPULoadOp_Clear;
	color_attachment.storeOp = WGPUStoreOp_Store;
	color_attachment.clearValue = { 0.5 + 0.5 * sinf(t), 0.5 + 0.5 * sinf(t + 2.094f), 0.5 + 0.5 * sinf(t + 4.189f), 1.0 };

	WGPURenderPassDescriptor pass_descriptor = {};
	pass_descriptor.colorAttachmentCount = 1;
	pass_descriptor.colorAttachments = &color_attachment;

	WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(m_device, nullptr);
	WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &pass_descriptor);
	wgpuRenderPassEncoderEnd(pass);
	WGPUCommandBuffer commands = wgpuCommandEncoderFinish(encoder, nullptr);
	wgpuQueueSubmit(m_queue, 1, &commands);

	wgpuCommandBufferRelease(commands);
	wgpuRenderPassEncoderRelease(pass);
	wgpuCommandEncoderRelease(encoder);
	wgpuTextureViewRelease(view);
	wgpuTextureRelease(surface_texture.texture);

	wgpuSurfacePresent(m_surface);

	// Drives Dawn's callbacks (device errors among them), so a problem is
	// reported on the frame it happens rather than at shutdown.
	wgpuInstanceProcessEvents(m_instance);

	m_frame_index++;
	if ((m_frame_index % 600) == 0) {
		blk::log("Renderer_WebGpu - frame %u", m_frame_index);
	}
}

/// Renderer_WebGpu::load_texture
u32 Renderer_WebGpu::load_texture(const std::string& path, LoadTextureParams& params) {
	return 0;
}

/// Renderer_WebGpu::create_gpu_pipeline
RenderPipeline* Renderer_WebGpu::create_gpu_pipeline(const std::string& friendly_name, const std::string& path) {
	return new WebGpuPipelineStub();
}

/// Renderer_WebGpu::create_compute_pipeline
RenderPipeline* Renderer_WebGpu::create_compute_pipeline(const std::string& friendly_name, const std::string& path) {
	return new WebGpuPipelineStub();
}

/// Renderer_WebGpu::create_render_buffer_internal
RenderBuffer* Renderer_WebGpu::create_render_buffer_internal() {
	return new WebGpuBufferStub();
}
