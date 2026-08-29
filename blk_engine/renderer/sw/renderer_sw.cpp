/// Renderer_Sw.cpp
///
/// 2025 blk 1.0

#include "blk_core.h"
#include "entity_header.h"
#include "Renderer_Sw.h"
#include "sw_defs.h"
#include "render_component.h"

using namespace std;

/// Renderer_Sw::~Renderer_Sw
Renderer_Sw::~Renderer_Sw() {
	shut_down();	// function is virtual but called in ~Renderer which is UB
}

/// Renderer_Sw::initialize_internal
void Renderer_Sw::initialize_internal(HWND hwnd, const uint32_t frame_width, const uint32_t frame_height) {
	m_hwnd = hwnd;

	m_color_buffer.resize((size_t)frame_width * frame_height * 4);
	m_depth_buffer.resize((size_t)frame_width * frame_height);

	load_pipeline(ERenderPipelineType::Gpu, "triangle", "");
	load_pipeline(ERenderPipelineType::Gpu, "kuwahara", "");
	load_pipeline(ERenderPipelineType::Gpu, "outline", "");

	blk::log("Renderer_Sw initialized");
}

/// Renderer_Sw::shut_down_internal
void Renderer_Sw::shut_down_internal() {
}

/// Renderer_Sw::create_render_buffer_internal
RenderBuffer* Renderer_Sw::create_render_buffer_internal() {
	return nullptr;
}

/// Renderer_Sw::load_texture
u32 Renderer_Sw::load_texture(const std::string& path, LoadTextureParams& param) {
	static u32 count = 0;
	return count++;
}

/// Renderer_Sw::render_custom_internal
void Renderer_Sw::render_custom_internal(const RenderCamera& camera) {
	render_software_rasterization();
	present_to_window();
}

/// Renderer_Sw::render_software_rasterization
void Renderer_Sw::render_software_rasterization() {
	// Update constant buffer
	Mat4 projection_matrix;
	projection_matrix.make_identity();
	projection_matrix.create_perspective_matrix(
		kbToRadians(50.),
		1197.f / (float)854,
		1.f, 20000.f
	);

	const Mat4 trans = Mat4::make_translation(-m_view_position);
	Mat4 rot = m_view_rotation.to_mat4();
	rot.transpose_self();

	Mat4 view_matrix = trans * rot;
	Mat4 vp_matrix =
		view_matrix *
		projection_matrix;

	// Shader params
	auto render_comp = *render_components().begin();
	const auto& shader_params = render_comp->materials()[0].shader_params();
	Vec4 shader_param_color(1.f, 1.f, 1.f, 1.f);
	static const Texture* color_tex = nullptr;

	for (const auto& param : shader_params) {
		const kbString& param_name = param.param_name();
		if (param_name == "color") {
			shader_param_color = param.vector();
		} else if (param_name == "color_tex" || param_name == "shaderTexture") {
			color_tex = param.texture();
		}
	}

	Vec4 start_color(0x04 / 255.f, 0x06 / 255.f, 0x22 / 255.f, 1.f);
	Vec4 end_color(0x26 / 255.f, 0x23 / 255.f, 0x6b / 255.f, 1.f);

	for (size_t y = 0; y < m_frame_height; y++) {
		const f32 t = y / (f32)m_frame_height;
		const u32 r = (u32)(255.f * (start_color.x + (end_color.x - start_color.x) * t));
		const u32 g = (u32)(255.f * (start_color.y + (end_color.y - start_color.y) * t));
		const u32 b = (u32)(255.f * (start_color.z + (end_color.z - start_color.z) * t));
		const u32 a = (u32)(255.f * (start_color.w + (end_color.w - start_color.w) * t));
		for (size_t x = 0; x < m_frame_width; x++) {
			const size_t idx = (x + y * m_frame_width) * 4;
			m_color_buffer[idx + 0] = r;
			m_color_buffer[idx + 1] = g;
			m_color_buffer[idx + 2] = b;
			m_color_buffer[idx + 3] = a;
		}
	}

	std::fill(m_depth_buffer.begin(), m_depth_buffer.end(), FLT_MAX);

	auto* tri_pipeline = (TrianglePipeline*)get_pipeline("triangle");
	tri_pipeline->set_view_proj(view_matrix, projection_matrix);
	tri_pipeline->render(render_components(),
		m_color_buffer,
		m_depth_buffer,
		Vec2i(m_frame_width, m_frame_height));

	auto* outline_pipeline = (KuwaharaPipeline*)get_pipeline("outline");
	outline_pipeline->render(render_components(),
		m_color_buffer,
		m_depth_buffer,
		Vec2i(m_frame_width, m_frame_height));
}

/// Renderer_Sw::present_to_window
void Renderer_Sw::present_to_window() {
	struct {
		BITMAPINFOHEADER header;
		DWORD masks[3];
	} bmi = {};

	bmi.header.biSize = sizeof(BITMAPINFOHEADER);
	bmi.header.biWidth = (LONG)m_frame_width;
	bmi.header.biHeight = -(LONG)m_frame_height;	// negative: buffer is top-down, matching the rasterizer above
	bmi.header.biPlanes = 1;
	bmi.header.biBitCount = 32;
	bmi.header.biCompression = BI_BITFIELDS;
	bmi.masks[0] = 0x000000ff;	// R is the first byte of each pixel in m_color_buffer
	bmi.masks[1] = 0x0000ff00;	// G
	bmi.masks[2] = 0x00ff0000;	// B

	HDC const hdc = GetDC(m_hwnd);
	StretchDIBits(
		hdc,
		0, 0, (int)m_frame_width, (int)m_frame_height,
		0, 0, (int)m_frame_width, (int)m_frame_height,
		m_color_buffer.data(),
		(BITMAPINFO*)&bmi,
		DIB_RGB_COLORS,
		SRCCOPY);
	ReleaseDC(m_hwnd, hdc);
}

/// Renderer_Sw::create_gpu_pipeline
RenderPipeline* Renderer_Sw::create_gpu_pipeline(const string& friendly_name, const string& path) {
	if (friendly_name == "triangle") {
		return new TrianglePipeline();
	} else if (friendly_name == "kuwahara") {
		return new KuwaharaPipeline();
	} else if (friendly_name == "outline") {
		return new OutlinePipeline();
	}

	blk::error("Invalid pipeline %s", friendly_name.c_str());
	return nullptr;
}
