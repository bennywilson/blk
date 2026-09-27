/// material.cpp
///
/// 2016 blk

#include <fstream>
#include "blk_core.h"
#include "entity_header.h"
#include "render_defs.h"
#include "renderer.h"
#include "material.h"

/// Texture::Texture
Texture::Texture() :
	m_is_cpu_texture(false),
	m_width(0),
	m_height(0),
	m_texture_id(-1) {
	if (g_renderer != nullptr && g_renderer->software_renderer()) {
		m_is_cpu_texture = true;
	}
}


/// Texture::Texture
Texture::Texture(const String& fileName) :
	m_is_cpu_texture(false),
	m_width(0),
	m_height(0),
	m_texture_id(-1) {
	if (g_renderer != nullptr && g_renderer->software_renderer()) {
		m_is_cpu_texture = true;
	}

	m_full_file_name = fileName.stl_str();
	m_full_name = String(m_full_file_name);

	load_internal();
}

/// Texture::load_internal
bool Texture::load_internal() {
	Renderer::LoadTextureParams load_params;
	if (m_is_cpu_texture) {
		load_params.cpu_accessible = true;
		load_params.texture_data = &m_cpu_texture;
	}

	m_texture_id = g_renderer->load_texture(full_file_name(), load_params);
	m_width = load_params.width;
	m_height = load_params.height;

	return true;
}

/// Texture::cpu_texture
const std::vector<Vec4>& Texture::cpu_texture(u32& width, u32& height) {
	if (m_is_cpu_texture == false) {
		m_is_cpu_texture = true;
		release();
		load_internal();
	}

	width = m_width;
	height = m_height;

	return m_cpu_texture;
}

/// Texture::release_internal
void Texture::release_internal() {
}

/// Shader::Shader
Shader::Shader() :
	m_VertexShaderFunctionName("vertexShader"),
	m_PixelShaderFunctionName("pixelShader"),
	m_bBlendEnabled(false),
	m_bDistortionEnabled(false),
	m_SrcBlend(Blend_One),
	m_DstBlend(Blend_One),
	m_BlendOp(BlendOp_Add),
	m_SrcBlendAlpha(Blend_One),
	m_DstBlendAlpha(Blend_One),
	m_BlendOpAlpha(BlendOp_Add),
	m_ColorWriteEnable(ColorWriteEnable::ColorWriteEnable_All),
	m_CullMode(CullMode_BackFaces) {
}

/// Shader::Shader
Shader::Shader(const std::string& fileName) :
	m_VertexShaderFunctionName("vertexShader"),
	m_PixelShaderFunctionName("pixelShader"),
	m_bBlendEnabled(false),
	m_bDistortionEnabled(false),
	m_SrcBlend(Blend_One),
	m_DstBlend(Blend_One),
	m_BlendOp(BlendOp_Add),
	m_SrcBlendAlpha(Blend_One),
	m_DstBlendAlpha(Blend_One),
	m_BlendOpAlpha(BlendOp_Add),
	m_ColorWriteEnable(ColorWriteEnable::ColorWriteEnable_All),
	m_CullMode(CullMode_BackFaces) {

	m_full_file_name = fileName;
}

std::unordered_map<std::string, ColorWriteEnable> g_ColorWriteMap;
ColorWriteEnable GetColorWriteEnableFromName(const std::string& name) {

	if (g_ColorWriteMap.empty()) {
		typedef std::pair<std::string, ColorWriteEnable> colorWriteMapPair;

		g_ColorWriteMap.insert(colorWriteMapPair("colorwriteenable_r", ColorWriteEnable::ColorWriteEnable_Red));
		g_ColorWriteMap.insert(colorWriteMapPair("colorwriteenable_rg", ColorWriteEnable::ColorWriteEnable_Red | ColorWriteEnable::ColorWriteEnable_Green));
		g_ColorWriteMap.insert(colorWriteMapPair("colorwriteenable_rgb", ColorWriteEnable::ColorWriteEnable_Red | ColorWriteEnable::ColorWriteEnable_Green | ColorWriteEnable::ColorWriteEnable_Blue));
		g_ColorWriteMap.insert(colorWriteMapPair("colorwriteenable_rgba", ColorWriteEnable::ColorWriteEnable_All));
		g_ColorWriteMap.insert(colorWriteMapPair("colorwriteenable_rb", ColorWriteEnable::ColorWriteEnable_Red | ColorWriteEnable::ColorWriteEnable_Blue));
		g_ColorWriteMap.insert(colorWriteMapPair("colorwriteenable_rba", ColorWriteEnable::ColorWriteEnable_Red | ColorWriteEnable::ColorWriteEnable_Blue | ColorWriteEnable::ColorWriteEnable_Alpha));
		g_ColorWriteMap.insert(colorWriteMapPair("colorwriteenable_ra", ColorWriteEnable::ColorWriteEnable_Red | ColorWriteEnable::ColorWriteEnable_Alpha));
		g_ColorWriteMap.insert(colorWriteMapPair("colorwriteenable_g", ColorWriteEnable::ColorWriteEnable_Green));
		g_ColorWriteMap.insert(colorWriteMapPair("colorwriteenable_gb", ColorWriteEnable::ColorWriteEnable_Green | ColorWriteEnable::ColorWriteEnable_Blue));
		g_ColorWriteMap.insert(colorWriteMapPair("colorwriteenable_gba", ColorWriteEnable::ColorWriteEnable_Green | ColorWriteEnable::ColorWriteEnable_Blue | ColorWriteEnable::ColorWriteEnable_Alpha));
		g_ColorWriteMap.insert(colorWriteMapPair("colorwriteenable_ga", ColorWriteEnable::ColorWriteEnable_Green | ColorWriteEnable::ColorWriteEnable_Alpha));
		g_ColorWriteMap.insert(colorWriteMapPair("colorwriteenable_b", ColorWriteEnable::ColorWriteEnable_Blue));
		g_ColorWriteMap.insert(colorWriteMapPair("colorwriteenable_ba", ColorWriteEnable::ColorWriteEnable_Blue | ColorWriteEnable::ColorWriteEnable_Alpha));
		g_ColorWriteMap.insert(colorWriteMapPair("colorwriteenable_a", ColorWriteEnable::ColorWriteEnable_Alpha));
	}

	auto colorMapIt = g_ColorWriteMap.find(name);
	if (colorMapIt != g_ColorWriteMap.end()) {
		return colorMapIt->second;
	}

	blk::warn("GetColorWriteEnableFromName() - Invalid value %s", name.c_str());
	return ColorWriteEnable::ColorWriteEnable_All;
}

std::unordered_map<std::string, Blend> g_BlendMap;
Blend GetBlendFromName(const std::string& name) {
	if (g_BlendMap.empty()) {
		typedef std::pair<std::string, Blend> blendMapPair;
		g_BlendMap.insert(blendMapPair("blend_zero", Blend_Zero));
		g_BlendMap.insert(blendMapPair("blend_one", Blend_One));
		g_BlendMap.insert(blendMapPair("blend_srccolor", Blend_SrcColor));
		g_BlendMap.insert(blendMapPair("blend_invsrccolor", Blend_InvSrcColor));
		g_BlendMap.insert(blendMapPair("blend_srcalpha", Blend_SrcAlpha));
		g_BlendMap.insert(blendMapPair("blend_invsrcalpha", Blend_InvSrcAlpha));
		g_BlendMap.insert(blendMapPair("blend_dstalpha", Blend_DstAlpha));
		g_BlendMap.insert(blendMapPair("blend_invdstalpha", Blend_InvDstAlpha));
		g_BlendMap.insert(blendMapPair("blend_dstcolor", Blend_DstColor));
		g_BlendMap.insert(blendMapPair("blend_invdstcolor", Blend_InvDstColor));
	}

	auto blendMapIt = g_BlendMap.find(name);
	if (blendMapIt != g_BlendMap.end()) {
		return blendMapIt->second;
	}

	blk::warn("GetBlendFromName() - Invalid value %s", name.c_str());
	return Blend_One;
}

std::unordered_map<std::string, BlendOp> g_BlendOpMap;
BlendOp GetBlendOpFromName(std::string& name) {
	if (g_BlendOpMap.empty()) {
		typedef std::pair<std::string, BlendOp> blendOpMapPair;
		g_BlendOpMap.insert(blendOpMapPair("blendop_add", BlendOp_Add));
		g_BlendOpMap.insert(blendOpMapPair("blendop_subtract", BlendOp_Subtract));
		g_BlendOpMap.insert(blendOpMapPair("blendop_max", BlendOp_Max));
		g_BlendOpMap.insert(blendOpMapPair("blendop_min", BlendOp_Min));
	}

	auto blendOpMapIt = g_BlendOpMap.find(name);
	if (blendOpMapIt != g_BlendOpMap.end()) {
		return blendOpMapIt->second;
	}

	blk::warn("GetBlendOpFromName() - Invalid value %s", name.c_str());
	return BlendOp_Add;
}

/// Shader::load_internal
bool Shader::load_internal() {
	/*if (g_pD3D11Renderer != nullptr) {		// HACK TODO
		// Load File
		std::ifstream shaderFile;
		shaderFile.open(full_file_name().c_str(), std::fstream::in);
		if (shaderFile.fail()) {
			return false;
		}

		std::string shaderText((std::istreambuf_iterator<char>(shaderFile)), std::istreambuf_iterator<char>());
		shaderFile.close();

		TextParser shaderParser(shaderText);
		shaderParser.RemoveComments();

		if (shaderParser.SetBlock("ShaderState")) {
			shaderParser.MakeLowerCase();

			std::string value;

			if (shaderParser.ContainsKey("distortion")) {
				m_bDistortionEnabled = true;
			}

			if (shaderParser.GetValueForKey(value, "srcblend")) {
				m_SrcBlend = GetBlendFromName(value);
				m_bBlendEnabled = true;
			}

			if (shaderParser.GetValueForKey(value, "dstblend")) {
				m_DstBlend = GetBlendFromName(value);
				m_bBlendEnabled = true;
			}

			if (shaderParser.GetValueForKey(value, "blendop")) {
				m_BlendOp = GetBlendOpFromName(value);
				m_bBlendEnabled = true;
			}

			if (shaderParser.GetValueForKey(value, "srcblendalpha")) {
				m_SrcBlendAlpha = GetBlendFromName(value);
				m_bBlendEnabled = true;
			}

			if (shaderParser.GetValueForKey(value, "dstblendalpha")) {
				m_DstBlendAlpha = GetBlendFromName(value);
				m_bBlendEnabled = true;
			}

			if (shaderParser.GetValueForKey(value, "blendopalpha")) {
				m_BlendOpAlpha = GetBlendOpFromName(value);
				m_bBlendEnabled = true;
			}

			if (shaderParser.GetValueForKey(value, "colorwriteenable")) {
				m_ColorWriteEnable = GetColorWriteEnableFromName(value);
			}

			if (shaderParser.GetValueForKey(value, "cullmode")) {
				if (value == "cullmode_none") {
					m_CullMode = CullMode_None;
				} else if (value == "cullmode_frontfaces") {
					m_CullMode = CullMode_FrontFaces;
				} else if (value == " cullmode_backfaces") {
					m_CullMode = CullMode_BackFaces;
				}
			}
			shaderParser.ReplaceBlockWithSpaces();
		}

		g_pD3D11Renderer->CreateShaderFromText(full_file_name(), shaderText, m_pVertexShader, m_pGeometryShader, m_pPixelShader, m_pVertexLayout, m_VertexShaderFunctionName.c_str(), m_PixelShaderFunctionName.c_str(), &m_ShaderVarBindings);
	}*/
	return true;
}

/// Shader::release_internal
void Shader::release_internal() {
	m_ShaderVarBindings.m_VarBindings.clear();
	m_ShaderVarBindings.m_Textures.clear();

	m_bBlendEnabled = false;
	m_SrcBlend = Blend_One;
	m_DstBlend = Blend_One;
	m_BlendOp = BlendOp_Add;
	m_SrcBlendAlpha = Blend_One;
	m_DstBlendAlpha = Blend_One;
	m_BlendOpAlpha = BlendOp_Add;
	m_ColorWriteEnable = ColorWriteEnable::ColorWriteEnable_All;
	m_CullMode = CullMode_BackFaces;
}

/// Shader::CommitShaderParams
void Shader::CommitShaderParams() {
	/*blk::error_check(g_pRenderer->IsRenderingSynced(), "Shader::CommitShaderParams() - Can only be called when rendering is synced");

	m_GlobalShaderParams_RenderThread = m_GlobalShaderParams_GameThread;*/
}
