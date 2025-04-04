/// kbMaterial.cpp
///
/// 2016-2025 blk 1.0

#include <fstream>
#include <Wincodec.h>
#include "blk_core.h"
#include "entity_header.h"
#include "render_defs.h"
#include "renderer.h"
#include "material.h"

// Code to initialize a texture using the Windows Imaging Component from https://msdn.microsoft.com/en-us/library/windows/desktop/ff476904(v=vs.85).aspx
template<class T> class ScopedObject {
public:
	explicit ScopedObject(T* const p = nullptr) : _pointer(p) {}
	~ScopedObject() {
		if (_pointer != nullptr) {
			_pointer->Release();
			_pointer = nullptr;
		}
	}

	T* operator->() { return _pointer; }
	T** operator&() { return &_pointer; }

	T* Get() const { return _pointer; }

private:

	ScopedObject(const ScopedObject&);
	ScopedObject& operator=(const ScopedObject&);

	T* _pointer;
};

struct WICTranslate {
	GUID wic;
	DXGI_FORMAT format;
};

static WICTranslate g_WICFormats[] = {
	{ GUID_WICPixelFormat128bppRGBAFloat,       DXGI_FORMAT_R32G32B32A32_FLOAT },

	{ GUID_WICPixelFormat64bppRGBAHalf,         DXGI_FORMAT_R16G16B16A16_FLOAT },
	{ GUID_WICPixelFormat64bppRGBA,             DXGI_FORMAT_R16G16B16A16_UNORM },

	{ GUID_WICPixelFormat32bppRGBA,             DXGI_FORMAT_R8G8B8A8_UNORM },
	{ GUID_WICPixelFormat32bppBGRA,             DXGI_FORMAT_B8G8R8A8_UNORM }, // DXGI 1.1
	{ GUID_WICPixelFormat32bppBGR,              DXGI_FORMAT_B8G8R8X8_UNORM }, // DXGI 1.1

	{ GUID_WICPixelFormat32bppRGBA1010102XR,    DXGI_FORMAT_R10G10B10_XR_BIAS_A2_UNORM }, // DXGI 1.1
	{ GUID_WICPixelFormat32bppRGBA1010102,      DXGI_FORMAT_R10G10B10A2_UNORM },
	{ GUID_WICPixelFormat32bppRGBE,             DXGI_FORMAT_R9G9B9E5_SHAREDEXP },

#ifdef DXGI_1_2_FORMATS

	{ GUID_WICPixelFormat16bppBGRA5551,         DXGI_FORMAT_B5G5R5A1_UNORM },
	{ GUID_WICPixelFormat16bppBGR565,           DXGI_FORMAT_B5G6R5_UNORM },

#endif // DXGI_1_2_FORMATS

	{ GUID_WICPixelFormat32bppGrayFloat,        DXGI_FORMAT_R32_FLOAT },
	{ GUID_WICPixelFormat16bppGrayHalf,         DXGI_FORMAT_R16_FLOAT },
	{ GUID_WICPixelFormat16bppGray,             DXGI_FORMAT_R16_UNORM },
	{ GUID_WICPixelFormat8bppGray,              DXGI_FORMAT_R8_UNORM },

	{ GUID_WICPixelFormat8bppAlpha,             DXGI_FORMAT_A8_UNORM },

#if (_WIN32_WINNT >= 0x0602 /*_WIN32_WINNT_WIN8*/)
	{ GUID_WICPixelFormat96bppRGBFloat,         DXGI_FORMAT_R32G32B32_FLOAT },
#endif
};

//-------------------------------------------------------------------------------------
// WIC Pixel Format nearest conversion table
//-------------------------------------------------------------------------------------
struct WICConvert {
	GUID source;
	GUID target;
};

static WICConvert g_WICConvert[] = {
	// Note target GUID in this conversion table must be one of those directly supported formats (above).

	{ GUID_WICPixelFormatBlackWhite,            GUID_WICPixelFormat8bppGray }, // DXGI_FORMAT_R8_UNORM

	{ GUID_WICPixelFormat1bppIndexed,           GUID_WICPixelFormat32bppRGBA }, // DXGI_FORMAT_R8G8B8A8_UNORM 
	{ GUID_WICPixelFormat2bppIndexed,           GUID_WICPixelFormat32bppRGBA }, // DXGI_FORMAT_R8G8B8A8_UNORM 
	{ GUID_WICPixelFormat4bppIndexed,           GUID_WICPixelFormat32bppRGBA }, // DXGI_FORMAT_R8G8B8A8_UNORM 
	{ GUID_WICPixelFormat8bppIndexed,           GUID_WICPixelFormat32bppRGBA }, // DXGI_FORMAT_R8G8B8A8_UNORM 

	{ GUID_WICPixelFormat2bppGray,              GUID_WICPixelFormat8bppGray }, // DXGI_FORMAT_R8_UNORM 
	{ GUID_WICPixelFormat4bppGray,              GUID_WICPixelFormat8bppGray }, // DXGI_FORMAT_R8_UNORM 

	{ GUID_WICPixelFormat16bppGrayFixedPoint,   GUID_WICPixelFormat16bppGrayHalf }, // DXGI_FORMAT_R16_FLOAT 
	{ GUID_WICPixelFormat32bppGrayFixedPoint,   GUID_WICPixelFormat32bppGrayFloat }, // DXGI_FORMAT_R32_FLOAT 

#ifdef DXGI_1_2_FORMATS

	{ GUID_WICPixelFormat16bppBGR555,           GUID_WICPixelFormat16bppBGRA5551 }, // DXGI_FORMAT_B5G5R5A1_UNORM

#else

	{ GUID_WICPixelFormat16bppBGR555,           GUID_WICPixelFormat32bppRGBA }, // DXGI_FORMAT_R8G8B8A8_UNORM
	{ GUID_WICPixelFormat16bppBGRA5551,         GUID_WICPixelFormat32bppRGBA }, // DXGI_FORMAT_R8G8B8A8_UNORM
	{ GUID_WICPixelFormat16bppBGR565,           GUID_WICPixelFormat32bppRGBA }, // DXGI_FORMAT_R8G8B8A8_UNORM

#endif // DXGI_1_2_FORMATS

	{ GUID_WICPixelFormat32bppBGR101010,        GUID_WICPixelFormat32bppRGBA1010102 }, // DXGI_FORMAT_R10G10B10A2_UNORM

	{ GUID_WICPixelFormat24bppBGR,              GUID_WICPixelFormat32bppRGBA }, // DXGI_FORMAT_R8G8B8A8_UNORM 
	{ GUID_WICPixelFormat24bppRGB,              GUID_WICPixelFormat32bppRGBA }, // DXGI_FORMAT_R8G8B8A8_UNORM 
	{ GUID_WICPixelFormat32bppPBGRA,            GUID_WICPixelFormat32bppRGBA }, // DXGI_FORMAT_R8G8B8A8_UNORM 
	{ GUID_WICPixelFormat32bppPRGBA,            GUID_WICPixelFormat32bppRGBA }, // DXGI_FORMAT_R8G8B8A8_UNORM 

	{ GUID_WICPixelFormat48bppRGB,              GUID_WICPixelFormat64bppRGBA }, // DXGI_FORMAT_R16G16B16A16_UNORM
	{ GUID_WICPixelFormat48bppBGR,              GUID_WICPixelFormat64bppRGBA }, // DXGI_FORMAT_R16G16B16A16_UNORM
	{ GUID_WICPixelFormat64bppBGRA,             GUID_WICPixelFormat64bppRGBA }, // DXGI_FORMAT_R16G16B16A16_UNORM
	{ GUID_WICPixelFormat64bppPRGBA,            GUID_WICPixelFormat64bppRGBA }, // DXGI_FORMAT_R16G16B16A16_UNORM
	{ GUID_WICPixelFormat64bppPBGRA,            GUID_WICPixelFormat64bppRGBA }, // DXGI_FORMAT_R16G16B16A16_UNORM

	{ GUID_WICPixelFormat48bppRGBFixedPoint,    GUID_WICPixelFormat64bppRGBAHalf }, // DXGI_FORMAT_R16G16B16A16_FLOAT 
	{ GUID_WICPixelFormat48bppBGRFixedPoint,    GUID_WICPixelFormat64bppRGBAHalf }, // DXGI_FORMAT_R16G16B16A16_FLOAT 
	{ GUID_WICPixelFormat64bppRGBAFixedPoint,   GUID_WICPixelFormat64bppRGBAHalf }, // DXGI_FORMAT_R16G16B16A16_FLOAT 
	{ GUID_WICPixelFormat64bppBGRAFixedPoint,   GUID_WICPixelFormat64bppRGBAHalf }, // DXGI_FORMAT_R16G16B16A16_FLOAT 
	{ GUID_WICPixelFormat64bppRGBFixedPoint,    GUID_WICPixelFormat64bppRGBAHalf }, // DXGI_FORMAT_R16G16B16A16_FLOAT 
	{ GUID_WICPixelFormat64bppRGBHalf,          GUID_WICPixelFormat64bppRGBAHalf }, // DXGI_FORMAT_R16G16B16A16_FLOAT 
	{ GUID_WICPixelFormat48bppRGBHalf,          GUID_WICPixelFormat64bppRGBAHalf }, // DXGI_FORMAT_R16G16B16A16_FLOAT 

	{ GUID_WICPixelFormat96bppRGBFixedPoint,    GUID_WICPixelFormat128bppRGBAFloat }, // DXGI_FORMAT_R32G32B32A32_FLOAT 
	{ GUID_WICPixelFormat128bppPRGBAFloat,      GUID_WICPixelFormat128bppRGBAFloat }, // DXGI_FORMAT_R32G32B32A32_FLOAT 
	{ GUID_WICPixelFormat128bppRGBFloat,        GUID_WICPixelFormat128bppRGBAFloat }, // DXGI_FORMAT_R32G32B32A32_FLOAT 
	{ GUID_WICPixelFormat128bppRGBAFixedPoint,  GUID_WICPixelFormat128bppRGBAFloat }, // DXGI_FORMAT_R32G32B32A32_FLOAT 
	{ GUID_WICPixelFormat128bppRGBFixedPoint,   GUID_WICPixelFormat128bppRGBAFloat }, // DXGI_FORMAT_R32G32B32A32_FLOAT 

	{ GUID_WICPixelFormat32bppCMYK,             GUID_WICPixelFormat32bppRGBA }, // DXGI_FORMAT_R8G8B8A8_UNORM 
	{ GUID_WICPixelFormat64bppCMYK,             GUID_WICPixelFormat64bppRGBA }, // DXGI_FORMAT_R16G16B16A16_UNORM
	{ GUID_WICPixelFormat40bppCMYKAlpha,        GUID_WICPixelFormat64bppRGBA }, // DXGI_FORMAT_R16G16B16A16_UNORM
	{ GUID_WICPixelFormat80bppCMYKAlpha,        GUID_WICPixelFormat64bppRGBA }, // DXGI_FORMAT_R16G16B16A16_UNORM

#if (_WIN32_WINNT >= 0x0602 /*_WIN32_WINNT_WIN8*/)
	{ GUID_WICPixelFormat32bppRGB,              GUID_WICPixelFormat32bppRGBA }, // DXGI_FORMAT_R8G8B8A8_UNORM
	{ GUID_WICPixelFormat64bppRGB,              GUID_WICPixelFormat64bppRGBA }, // DXGI_FORMAT_R16G16B16A16_UNORM
	{ GUID_WICPixelFormat64bppPRGBAHalf,        GUID_WICPixelFormat64bppRGBAHalf }, // DXGI_FORMAT_R16G16B16A16_FLOAT 
#endif

	// We don't support n-channel formats
};

//---------------------------------------------------------------------------------
static DXGI_FORMAT _WICToDXGI(const GUID& guid) {

	for (size_t i = 0; i < _countof(g_WICFormats); ++i) {
		if (memcmp(&g_WICFormats[i].wic, &guid, sizeof(GUID)) == 0) {
			return g_WICFormats[i].format;
		}
	}

	return DXGI_FORMAT_UNKNOWN;
}

static IWICImagingFactory* _GetWIC() {
	static IWICImagingFactory* s_Factory = nullptr;

	if (s_Factory != nullptr) {
		return s_Factory;
	}

	HRESULT hr = CoCreateInstance(
		CLSID_WICImagingFactory,
		nullptr,
		CLSCTX_INPROC_SERVER,
		__uuidof(IWICImagingFactory),
		(LPVOID*)&s_Factory
	);

	if (FAILED(hr)) {
		s_Factory = nullptr;
		return nullptr;
	}

	return s_Factory;
}

//---------------------------------------------------------------------------------
static size_t _WICBitsPerPixel(REFGUID targetGuid) {
	IWICImagingFactory* const pWIC = _GetWIC();
	if (pWIC == nullptr) {
		return 0;
	}

	ScopedObject<IWICComponentInfo> cinfo;
	if (FAILED(pWIC->CreateComponentInfo(targetGuid, &cinfo))) {
		return 0;
	}

	WICComponentType type;
	if (FAILED(cinfo->GetComponentType(&type))) {
		return 0;
	}

	if (type != WICPixelFormat) {
		return 0;
	}

	ScopedObject<IWICPixelFormatInfo> pfinfo;
	if (FAILED(cinfo->QueryInterface(__uuidof(IWICPixelFormatInfo), reinterpret_cast<void**>(&pfinfo)))) {
		return 0;
	}

	UINT bpp;
	if (FAILED(pfinfo->GetBitsPerPixel(&bpp))) {
		return 0;
	}

	return bpp;
}

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
Texture::Texture(const kbString& fileName) :
	m_is_cpu_texture(false),
	m_width(0),
	m_height(0),
	m_texture_id(-1) {
	if (g_renderer != nullptr && g_renderer->software_renderer()) {
		m_is_cpu_texture = true;
	}

	m_full_file_name = fileName.stl_str();
	m_full_name = kbString(m_full_file_name);

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

/// kbShader::kbShader
kbShader::kbShader() :
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
	m_ColorWriteEnable(kbColorWriteEnable::ColorWriteEnable_All),
	m_CullMode(CullMode_BackFaces) {
}

/// kbShader::kbShader
kbShader::kbShader(const std::string& fileName) :
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
	m_ColorWriteEnable(kbColorWriteEnable::ColorWriteEnable_All),
	m_CullMode(CullMode_BackFaces) {

	m_full_file_name = fileName;
}

std::unordered_map<std::string, kbColorWriteEnable> g_ColorWriteMap;
kbColorWriteEnable GetColorWriteEnableFromName(const std::string& name) {

	if (g_ColorWriteMap.empty()) {
		typedef std::pair<std::string, kbColorWriteEnable> colorWriteMapPair;

		g_ColorWriteMap.insert(colorWriteMapPair("colorwriteenable_r", kbColorWriteEnable::ColorWriteEnable_Red));
		g_ColorWriteMap.insert(colorWriteMapPair("colorwriteenable_rg", kbColorWriteEnable::ColorWriteEnable_Red | kbColorWriteEnable::ColorWriteEnable_Green));
		g_ColorWriteMap.insert(colorWriteMapPair("colorwriteenable_rgb", kbColorWriteEnable::ColorWriteEnable_Red | kbColorWriteEnable::ColorWriteEnable_Green | kbColorWriteEnable::ColorWriteEnable_Blue));
		g_ColorWriteMap.insert(colorWriteMapPair("colorwriteenable_rgba", kbColorWriteEnable::ColorWriteEnable_All));
		g_ColorWriteMap.insert(colorWriteMapPair("colorwriteenable_rb", kbColorWriteEnable::ColorWriteEnable_Red | kbColorWriteEnable::ColorWriteEnable_Blue));
		g_ColorWriteMap.insert(colorWriteMapPair("colorwriteenable_rba", kbColorWriteEnable::ColorWriteEnable_Red | kbColorWriteEnable::ColorWriteEnable_Blue | kbColorWriteEnable::ColorWriteEnable_Alpha));
		g_ColorWriteMap.insert(colorWriteMapPair("colorwriteenable_ra", kbColorWriteEnable::ColorWriteEnable_Red | kbColorWriteEnable::ColorWriteEnable_Alpha));
		g_ColorWriteMap.insert(colorWriteMapPair("colorwriteenable_g", kbColorWriteEnable::ColorWriteEnable_Green));
		g_ColorWriteMap.insert(colorWriteMapPair("colorwriteenable_gb", kbColorWriteEnable::ColorWriteEnable_Green | kbColorWriteEnable::ColorWriteEnable_Blue));
		g_ColorWriteMap.insert(colorWriteMapPair("colorwriteenable_gba", kbColorWriteEnable::ColorWriteEnable_Green | kbColorWriteEnable::ColorWriteEnable_Blue | kbColorWriteEnable::ColorWriteEnable_Alpha));
		g_ColorWriteMap.insert(colorWriteMapPair("colorwriteenable_ga", kbColorWriteEnable::ColorWriteEnable_Green | kbColorWriteEnable::ColorWriteEnable_Alpha));
		g_ColorWriteMap.insert(colorWriteMapPair("colorwriteenable_b", kbColorWriteEnable::ColorWriteEnable_Blue));
		g_ColorWriteMap.insert(colorWriteMapPair("colorwriteenable_ba", kbColorWriteEnable::ColorWriteEnable_Blue | kbColorWriteEnable::ColorWriteEnable_Alpha));
		g_ColorWriteMap.insert(colorWriteMapPair("colorwriteenable_a", kbColorWriteEnable::ColorWriteEnable_Alpha));
	}

	auto colorMapIt = g_ColorWriteMap.find(name);
	if (colorMapIt != g_ColorWriteMap.end()) {
		return colorMapIt->second;
	}

	blk::warn("GetColorWriteEnableFromName() - Invalid value %s", name.c_str());
	return kbColorWriteEnable::ColorWriteEnable_All;
}

std::unordered_map<std::string, kbBlend> g_BlendMap;
kbBlend GetBlendFromName(const std::string& name) {
	if (g_BlendMap.empty()) {
		typedef std::pair<std::string, kbBlend> blendMapPair;
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

std::unordered_map<std::string, kbBlendOp> g_BlendOpMap;
kbBlendOp GetBlendOpFromName(std::string& name) {
	if (g_BlendOpMap.empty()) {
		typedef std::pair<std::string, kbBlendOp> blendOpMapPair;
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

/// kbShader::load_internal
bool kbShader::load_internal() {
	/*if (g_pD3D11Renderer != nullptr) {		// HACK TODO
		// Load File
		std::ifstream shaderFile;
		shaderFile.open(full_file_name().c_str(), std::fstream::in);
		if (shaderFile.fail()) {
			return false;
		}

		std::string shaderText((std::istreambuf_iterator<char>(shaderFile)), std::istreambuf_iterator<char>());
		shaderFile.close();

		kbTextParser shaderParser(shaderText);
		shaderParser.RemoveComments();

		if (shaderParser.SetBlock("kbShaderState")) {
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

/// kbShader::release_internal
void kbShader::release_internal() {
	m_ShaderVarBindings.m_VarBindings.clear();
	m_ShaderVarBindings.m_Textures.clear();

	m_bBlendEnabled = false;
	m_SrcBlend = Blend_One;
	m_DstBlend = Blend_One;
	m_BlendOp = BlendOp_Add;
	m_SrcBlendAlpha = Blend_One;
	m_DstBlendAlpha = Blend_One;
	m_BlendOpAlpha = BlendOp_Add;
	m_ColorWriteEnable = kbColorWriteEnable::ColorWriteEnable_All;
	m_CullMode = CullMode_BackFaces;
}

/// kbShader::CommitShaderParams
void kbShader::CommitShaderParams() {
	/*blk::error_check(g_pRenderer->IsRenderingSynced(), "kbShader::CommitShaderParams() - Can only be called when rendering is synced");

	m_GlobalShaderParams_RenderThread = m_GlobalShaderParams_GameThread;*/
}
