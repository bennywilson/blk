/// Renderer_Sw.h
///
/// 2025 blk

#pragma once

#include "renderer.h"

///	Renderer_Sw
class Renderer_Sw : public Renderer {
public:
	~Renderer_Sw();

	virtual bool software_renderer() const override {
		return true;
	};

private:
	virtual void initialize_internal(HWND hwnd, const uint32_t frameWidth, const uint32_t frameHeight) override;

	virtual void shut_down_internal() override;

	virtual RenderPipeline* create_gpu_pipeline(const std::string& friendly_name, const std::string& path) override;
	virtual RenderPipeline* create_compute_pipeline(const std::string& friendly_name, const std::string& path) override { return nullptr; }
	virtual RenderBuffer* create_render_buffer_internal() override;

	virtual u32 load_texture(const std::string& path, LoadTextureParams& params) override;

	virtual void render_custom_internal(const RenderCamera& camera) override;

	void render_software_rasterization();

	// Blits m_color_buffer to m_hwnd via GDI; no GPU-API dependency
	void present_to_window();

	HWND m_hwnd = nullptr;

	std::vector<u8> m_color_buffer;
	std::vector<f32> m_depth_buffer;
};
