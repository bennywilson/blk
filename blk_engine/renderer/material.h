/// material.h
///
/// 2016 blk

#pragma once

#include "resource_manager.h"
#include "render_defs.h"

/// Texture
class Texture : public Resource {
public:
	Texture();
	explicit Texture(const String& fileName);

	~Texture() { /*blk::error_check(m_pGPUTexture == nullptr, " Texture::~Texture() - Destructing a Texture that hasn't been released");*/ }

	virtual TypeInfoType_t type() const { return BLK_TYPEINFO_TEXTURE; }

	const std::vector<Vec4>& cpu_texture(u32& width, u32& height);

	u32 get_texture_id() const {
		return m_texture_id;
	}

	uint width() const { return m_width; }
	uint height() const { return m_height; }

private:
	virtual bool load_internal();
	virtual void release_internal();

	std::vector<Vec4> m_cpu_texture;

	uint m_width;
	uint m_height;
	i32 m_texture_id;

	bool m_is_cpu_texture;
};

/// ShaderVarBinding_t
struct ShaderVarBindings_t {
	ShaderVarBindings_t() :
		m_ConstantBufferSizeBytes(0) {}

	size_t m_ConstantBufferSizeBytes;

	struct binding_t {
		binding_t(const std::string& inName, const size_t offset, const bool bHasDefaultValue, const Vec4 defaultValue, const bool bIsUserDefinedVar) :
			m_VarName(inName),
			m_VarByteOffset(offset),
			m_DefaultValue(defaultValue),
			m_bHasDefaultValue(bHasDefaultValue),
			m_bIsUserDefinedVar(bIsUserDefinedVar) {}

		std::string m_VarName;
		size_t m_VarByteOffset;
		Vec4 m_DefaultValue;
		bool m_bHasDefaultValue;
		bool m_bIsUserDefinedVar;
	};
	std::vector<binding_t> m_VarBindings;

	struct textureBinding_t {
		textureBinding_t() :
			m_pDefaultTexture(nullptr), m_pDefaultRenderTexture(nullptr), m_bIsUserDefinedVar(false) {}

		std::string m_TextureName;
		Texture* m_pDefaultTexture;
		RenderTexture* m_pDefaultRenderTexture;
		bool m_bIsUserDefinedVar;
	};
	std::vector<textureBinding_t> m_Textures;

	bool ContainsBinding(const char* const pBinding) {
		for (int i = 0; i < m_VarBindings.size(); i++) {
			if (m_VarBindings[i].m_VarName == pBinding) {
				return true;
			}
		}
		return false;
	}
};

///  Shader
class Shader : public Resource {
	friend class Shader_TypeInfo;

public:
	Shader(const std::string& fileName);
	Shader();

	virtual TypeInfoType_t type() const { return BLK_TYPEINFO_SHADER; }

	void SetVertexShaderFunctionName(const std::string& inName) { m_VertexShaderFunctionName = inName; }
	void SetPixelShaderFunctionName(const std::string& inName) { m_PixelShaderFunctionName = inName; }

	void SetGlobalShaderParams(const std::vector<Vec4>& shaderParams) { m_GlobalShaderParams_GameThread = shaderParams; }
	void CommitShaderParams();
	const std::vector<Vec4>& GetGlobalShaderParams() const { return m_GlobalShaderParams_RenderThread; } // todo: check if render thread

	const ShaderVarBindings_t& GetShaderVarBindings() const { return m_ShaderVarBindings; }

	// Render States
	bool IsBlendEnabled() const { return m_bBlendEnabled; }
	bool IsDistortionEnabled() const { return m_bDistortionEnabled; }

	Blend GetSrcBlend() const { return m_SrcBlend; }
	Blend GetDstBlend() const { return m_DstBlend; }
	BlendOp GetBlendOp() const { return m_BlendOp; }

	Blend GetSrcBlendAlpha() const { return m_SrcBlendAlpha; }
	Blend GetDstBlendAlpha() const { return m_DstBlendAlpha; }
	BlendOp GetBlendOpAlpha() const { return m_BlendOpAlpha; }

	ColorWriteEnable GetColorWriteEnable() const { return m_ColorWriteEnable; }

	ECullMode GetCullMode() const { return m_CullMode; }

private:
	virtual bool load_internal() override;
	virtual void release_internal() override;

	std::map<String, int> m_ShaderConstantsMap; // Maps constant variable name to it's byte offset

	std::string m_VertexShaderFunctionName;
	std::string m_PixelShaderFunctionName;

	std::vector<Vec4> m_GlobalShaderParams_GameThread;
	std::vector<Vec4> m_GlobalShaderParams_RenderThread;

	ShaderVarBindings_t m_ShaderVarBindings;

	bool m_bBlendEnabled;
	bool m_bDistortionEnabled;

	Blend m_SrcBlend;
	Blend m_DstBlend;
	BlendOp m_BlendOp;

	Blend m_SrcBlendAlpha;
	Blend m_DstBlendAlpha;
	BlendOp m_BlendOpAlpha;

	ColorWriteEnable m_ColorWriteEnable;
	ECullMode m_CullMode;
};

/// Material
class Material {
	friend class Model;

public:
	Material() :
		m_shader(nullptr), m_CullingMode(CullMode_BackFaces) {}

	const Shader* get_shader() const { return m_shader; }

	const std::vector<const Texture*> GetTextureList() const { return m_Textures; }

	const Color& GetDiffuseColor() const { return m_DiffuseColor; }

	ECullMode GetCullingMode() const { return m_CullingMode; }

	void SetCullingMode(const ECullMode newMode) { m_CullingMode = newMode; }

private:
	std::vector<const Texture*> m_Textures;
	Shader* m_shader;
	Color m_DiffuseColor;
	ECullMode m_CullingMode;
};
