/// render_defs.h
///
/// 2025 blk

#pragma once
#include <vector>
#include "job_manager.h"
#include "Matrix.h"
#include "Quaternion.h"

/// RenderCamera
///
/// Explicit, self-contained view/projection state for one render graph pass.
/// Built by make_render_camera(); never read from Renderer's inherited
/// members mid-pass so multiple cameras (shadow casters, editor viewport,
/// game view) can coexist in the same frame.
struct RenderCamera {
	Vec3 view_position;
	Quat4 view_rotation;
	Mat4 view_matrix;
	Mat4 projection_matrix;
	Mat4 view_projection_matrix;
	Mat4 inv_view_projection_matrix;
};

RenderCamera make_render_camera(const Vec3& position, const Quat4& rotation, const f32 fov, const f32 aspect, const f32 near_z, const f32 far_z);

/// ViewContext
///
/// One camera's worth of work for the render graph to build passes against.
/// Renderer::render() currently only ever builds a single-element list (the
/// primary game camera, drawn into the existing swapchain-backed targets) --
/// a later phase adds a second ViewContext (its own target/viewport/scissor
/// fields go here) for a portal or an editor viewport, without reworking the
/// pass/barrier code that consumes this list.
struct ViewContext {
	RenderCamera camera;
};

enum ERenderPass {
	RP_FirstPerson,
	RP_Lighting,
	RP_PreTranslucent,
	RP_Translucent,
	RP_TranslucentWithDepth,
	RP_PostLighting,
	RP_InWorldUI,
	RP_Distortion,
	RP_PostProcess,
	RP_UI,
	RP_Debug,
	RP_MousePicker,
	NUM_RENDER_PASSES BLK_ENUM(Skip)
};

/// Which ERenderPass buckets a render-graph pass consumes. Lets a pass
/// declare its object filter instead of hardcoding an equality check.
using ERenderPassMask = std::vector<ERenderPass>;

bool render_pass_in_mask(const ERenderPass pass, const ERenderPassMask& mask);

enum ECullMode {
	CullMode_ShaderDefault,
	CullMode_None,
	CullMode_FrontFaces,
	CullMode_BackFaces,
};

enum EBlendMode {
	None,
	Alpha,
	Additive,
};

struct vertexColorLayout {
	Vec3 position;
	u8 color[4];

	void SetColor(const Vec4& inColor) {
		color[0] = (u8)(inColor.z * 255.0f);
		color[1] = (u8)(inColor.y * 255.0f);
		color[2] = (u8)(inColor.x * 255.0f);
		color[3] = (u8)(inColor.w * 255.0f);
	}

	Vec4 GetColor() const {
		Vec4 outColor((float)color[2], (float)color[1], (float)color[0], (float)color[3]);
		outColor.x = outColor.x / 255.0f;
		outColor.y = outColor.y / 255.0f;
		outColor.z = outColor.z / 255.0f;
		outColor.w = outColor.w / 255.0f;

		return outColor;
	}
};

/// BoneMatrix_t
struct BoneMatrix_t {
	BoneMatrix_t() {}

	explicit BoneMatrix_t(const Quat4& quat, const Vec3& pos) {
		SetFromQuat(quat);
		m_Axis[3] = pos;
	}

	void SetIdentity() {
		m_Axis[0].set(1.0f, 0.0f, 0.0f);
		m_Axis[1].set(0.0f, 1.0f, 0.0f);
		m_Axis[2].set(0.0f, 0.0f, 1.0f);
		m_Axis[3].set(0.0f, 0.0f, 0.0f);
	}

	const Vec3& operator[](const int index) const {
		return GetAxis(index);
	}

	const Vec3& GetAxis(const i32 axisIndex) const {
		if (axisIndex < 0 || axisIndex > 3) {
			blk::error("Doh!");
		}
		return m_Axis[axisIndex];
	}

	const Vec3& GetOrigin() const { return m_Axis[3]; }

	void SetAxis(const int axisIndex, const Vec3& inVec) {
		if (axisIndex < 0 || axisIndex > 3) {
			blk::error("Doh!");
		}
		m_Axis[axisIndex] = inVec;
	}

	void SetFromQuat(const Quat4& srcQuat);

	void TransposeUpper();

	void Invert();

	void operator*=(const BoneMatrix_t& op2);
	void operator*=(const Mat4& op2);

	Vec3 m_Axis[4];
};

/// RenderJob
class RenderJob : public Job {
public:
	RenderJob() :
		m_bRequestShutdown(false) {}

	void Run();

	void RequestShutdown() { m_bRequestShutdown = true; }

private:
	bool m_bRequestShutdown;
};

/// ShaderParamOverrides_t
struct ShaderParamOverrides_t {
	struct ShaderParam_t {
		enum type {
			SHADER_MAT4,
			SHADER_VEC4,
			SHADER_MAT4_LIST,
			SHADER_VEC4_LIST,
			SHADER_TEX,
		} m_Type;

		std::vector<Mat4> m_Mat4List;
		std::vector<Vec4> m_Vec4List;

		ShaderParam_t() :
			m_texture(nullptr), m_render_texture(nullptr) {}
		const class Texture* m_texture;
		const class RenderTexture* m_render_texture;
		std::string m_VarName;
		size_t m_VarSizeBytes;
	};

	ShaderParamOverrides_t() :
		m_shader(nullptr), m_cull_override(CullMode_ShaderDefault) {}

	std::vector<ShaderParam_t> m_ParamOverrides;
	const class Shader* m_shader;
	ECullMode m_cull_override;

	ShaderParam_t& AllocateParam(const std::string& varName) {
		for (int i = 0; i < m_ParamOverrides.size(); i++) {
			if (m_ParamOverrides[i].m_VarName == varName) {
				return m_ParamOverrides[i];
			}
		}

		ShaderParam_t newParam;
		m_ParamOverrides.push_back(newParam);
		return m_ParamOverrides[m_ParamOverrides.size() - 1];
	}

	void SetMat4(const std::string& varName, const Mat4& newMat) {
		ShaderParam_t& newParam = AllocateParam(varName);
		newParam.m_VarName = varName;
		newParam.m_Mat4List.clear();
		newParam.m_Mat4List.push_back(newMat);
		newParam.m_Type = ShaderParam_t::SHADER_MAT4;
		newParam.m_VarSizeBytes = sizeof(Mat4);
	}

	void SetMat4List(const std::string& varName, const std::vector<Mat4>& list) {
		ShaderParam_t& newParam = AllocateParam(varName);
		newParam.m_VarName = varName;
		newParam.m_Mat4List = list;
		newParam.m_Type = ShaderParam_t::SHADER_MAT4_LIST;
		newParam.m_VarSizeBytes = sizeof(Vec4);
	}

	void SetVec4(const std::string& varName, const Vec4& newVec) {
		ShaderParam_t& newParam = AllocateParam(varName);
		newParam.m_VarName = varName;
		newParam.m_Vec4List.clear();
		newParam.m_Vec4List.push_back(newVec);
		newParam.m_Type = ShaderParam_t::SHADER_VEC4;
		newParam.m_VarSizeBytes = sizeof(Vec4);
	}

	void SetVec4List(const std::string& varName, const std::vector<Vec4>& list) {
		ShaderParam_t& newParam = AllocateParam(varName);
		newParam.m_VarName = varName;
		newParam.m_Vec4List = list;
		newParam.m_Type = ShaderParam_t::SHADER_VEC4_LIST;
		newParam.m_VarSizeBytes = sizeof(Vec4);
	}

	void SetTexture(const std::string& varName, const Texture* const pTexture) {
		ShaderParam_t& newParam = AllocateParam(varName);
		newParam.m_VarName = varName;
		newParam.m_texture = pTexture;
		newParam.m_Type = ShaderParam_t::SHADER_TEX;
		newParam.m_VarSizeBytes = sizeof(Texture*);
	}

	void SetTexture(const std::string& varName, const RenderTexture* const pRenderTexture) {
		ShaderParam_t& newParam = AllocateParam(varName);
		newParam.m_VarName = varName;
		newParam.m_render_texture = pRenderTexture;
		newParam.m_Type = ShaderParam_t::SHADER_TEX;
		newParam.m_VarSizeBytes = sizeof(RenderTexture*);
	}
};

/// RenderObject
class RenderObject {
public:
	RenderObject() :
		m_pComponent(nullptr),
		m_model(nullptr),
		m_render_pass(RP_Lighting),
		m_CullMode(CullMode_ShaderDefault),
		m_render_order_bias(0.0f),
		m_EntityId(0),
		m_VertBufferStartIndex(-1),
		m_VertBufferIndexCount(-1),
		m_CullDistance(-1.0f),
		m_casts_shadow(false),
		m_bIsSkinnedModel(false),
		m_bIsFirstAdd(true),
		m_bIsRemove(false) {}

	const class GameComponent* m_pComponent;
	const class Model* m_model;
	std::vector<ShaderParamOverrides_t> m_Materials;
	ERenderPass m_render_pass;
	ECullMode m_CullMode;
	float m_render_order_bias;
	Vec3 m_position;
	Quat4 m_rotation;
	Vec3 m_Scale;
	uint m_EntityId;

	int m_VertBufferStartIndex;
	int m_VertBufferIndexCount;

	std::vector<BoneMatrix_t> m_MatrixList;

	float m_CullDistance;

	bool m_casts_shadow : 1;
	bool m_bIsSkinnedModel : 1;

	// Updated by renderer
	bool m_bIsFirstAdd : 1;
	bool m_bIsRemove : 1;
};

/// RenderLight
class RenderLight {

	//---------------------------------------------------------------------------------------------------
public:
	RenderLight() :
		m_pLightComponent(nullptr),
		m_bIsFirstAdd(false),
		m_bIsRemove(false) {
		memset(&m_CascadedShadowSplits, 0, sizeof(m_CascadedShadowSplits));
	}

	const class LightComponent* m_pLightComponent;
	Vec3 m_position;
	Quat4 m_rotation;
	Vec4 m_Color;
	float m_Radius;
	float m_Length;
	float m_CascadedShadowSplits[4];
	bool m_casts_shadow;
	bool m_bIsFirstAdd;
	bool m_bIsRemove;
};

/// eRenderObjectOp
enum eRenderObjectOp {
	ROO_Add,
	ROO_Remove,
	ROO_Update,
};

/// LightShafts
class LightShafts {

	//---------------------------------------------------------------------------------------------------
public:
	LightShafts() :
		m_pLightShaftsComponent(nullptr),
		m_texture(nullptr),
		m_Color(0.0f, 0.0f, 0.0f, 1.0f),
		m_Pos(Vec3::zero),
		m_rotation(Quat4::zero),
		m_Width(0.0f),
		m_Height(0.0f),
		m_NumIterations(0),
		m_IterationWidth(0.0f),
		m_IterationHeight(0.0f),
		m_Operation(ROO_Add),
		m_bIsDirectional(true) {
		m_rotation.set(0.0f, 0.0f, 0.0f, 1.0f);
	}

	const class LightShaftsComponent* m_pLightShaftsComponent;
	class Texture* m_texture;
	Color m_Color;
	Vec3 m_Pos;
	Quat4 m_rotation;
	float m_Width;
	float m_Height;
	int m_NumIterations;
	float m_IterationWidth;
	float m_IterationHeight;
	eRenderObjectOp m_Operation;
	bool m_bIsDirectional;
};

/// RenderTargetMap
struct RenderTargetMap {
	u8* m_pData;
	uint m_Width;
	uint m_Height;
	uint m_rowPitch;
};

enum Blend {
	Blend_Zero,
	Blend_One,
	Blend_SrcColor,
	Blend_InvSrcColor,
	Blend_SrcAlpha,
	Blend_InvSrcAlpha,
	Blend_DstAlpha,
	Blend_InvDstAlpha,
	Blend_DstColor,
	Blend_InvDstColor,
};

enum BlendOp {
	BlendOp_Add,
	BlendOp_Subtract,
	BlendOp_Max,
	BlendOp_Min
};

enum class ColorWriteEnable {
	ColorWriteEnable_Red = 1,
	ColorWriteEnable_Green = 2,
	ColorWriteEnable_Blue = 4,
	ColorWriteEnable_Alpha = 8,
	ColorWriteEnable_RGB = ColorWriteEnable_Red | ColorWriteEnable_Green | ColorWriteEnable_Blue,
	ColorWriteEnable_All = ColorWriteEnable_Red | ColorWriteEnable_Green | ColorWriteEnable_Blue | ColorWriteEnable_Alpha
};

inline ColorWriteEnable operator|(const ColorWriteEnable lhs, const ColorWriteEnable rhs) {
	return (ColorWriteEnable)((u32)lhs | (u32)rhs);
}

/// vertexLayout
struct vertexLayout {
	Vec3 position;
	Vec2 uv;
	u8 color[4];
	u8 normal[4];
	u8 tangent[4];

	void SetColor(const Vec4& inColor) {
		color[0] = (u8)(inColor.x * 255.0f);
		color[1] = (u8)(inColor.y * 255.0f);
		color[2] = (u8)(inColor.z * 255.0f);
		color[3] = (u8)(inColor.w * 255.0f);
	}

	void SetNormal(const Vec4& inNormal) {
		normal[0] = (u8)(((inNormal.x * 0.5f) + 0.5f) * 255.0f);
		normal[1] = (u8)(((inNormal.y * 0.5f) + 0.5f) * 255.0f);
		normal[2] = (u8)(((inNormal.z * 0.5f) + 0.5f) * 255.0f);
		normal[3] = (u8)(((inNormal.w * 0.5f) + 0.5f) * 255.0f);
	}

	void SetTangent(const Vec4& inTangent) {
		tangent[0] = (u8)(((inTangent.x * 0.5f) + 0.5f) * 255.0f);
		tangent[1] = (u8)(((inTangent.y * 0.5f) + 0.5f) * 255.0f);
		tangent[2] = (u8)(((inTangent.z * 0.5f) + 0.5f) * 255.0f);
		tangent[3] = (u8)(((inTangent.w * 0.5f) + 0.5f) * 255.0f);
	}

	void SetBitangent(const Vec4& inBitangent) {
		color[0] = (u8)(((inBitangent.x * 0.5f) + 0.5f) * 255.0f);
		color[1] = (u8)(((inBitangent.y * 0.5f) + 0.5f) * 255.0f);
		color[2] = (u8)(((inBitangent.z * 0.5f) + 0.5f) * 255.0f);
		color[3] = (u8)(((inBitangent.w * 0.5f) + 0.5f) * 255.0f);
	}

	Vec3 GetNormal() const {
		Vec3 outNormal((float)normal[0], (float)normal[1], (float)normal[2]);
		outNormal.x = ((outNormal.x / 255.0f) * 2.0f) - 1.0f;
		outNormal.y = ((outNormal.y / 255.0f) * 2.0f) - 1.0f;
		outNormal.z = ((outNormal.z / 255.0f) * 2.0f) - 1.0f;

		outNormal.normalize_self();
		return outNormal;
	}

	Vec3 GetTangent() const {
		Vec3 outTangent((float)tangent[0], (float)tangent[1], (float)tangent[2]);
		outTangent.x = ((outTangent.x / 255.0f) * 2.0f) - 1.0f;
		outTangent.y = ((outTangent.y / 255.0f) * 2.0f) - 1.0f;
		outTangent.z = ((outTangent.z / 255.0f) * 2.0f) - 1.0f;

		outTangent.normalize_self();
		return outTangent;
	}

	Vec4 GetColor() const {
		Vec4 outColor((float)color[2], (float)color[1], (float)color[0], (float)color[3]);
		outColor.x = outColor.x / 255.0f;
		outColor.y = outColor.y / 255.0f;
		outColor.z = outColor.z / 255.0f;
		outColor.w = outColor.w / 255.0f;

		return outColor;
	}

	void Clear() {
		memset(this, 0, sizeof(vertexLayout));
	}

	bool operator==(const vertexLayout& op2) const {
		const float epsilon = 0.0000000001f;
		return position.compare(op2.position, epsilon) && uv.compare(op2.uv, epsilon) &&
			blk::compare_byte4(color, op2.color) &&
			blk::compare_byte4(normal, op2.normal) && blk::compare_byte4(tangent, op2.tangent);
	}
};

/// ParticleVertex
struct ParticleVertex {
	Vec3 position;
	Vec2 uv;
	u8 color[4];
	f32 rotation;
	f32 scale;
};

enum class ERenderPipelineType {
	Gpu,
	Compute
};

/// RenderPipeline
class RenderPipeline {
public:
	// See the note on `BaseComponent::~BaseComponent()` - `= 0 {}` is MSVC-only.
	virtual ~RenderPipeline() = 0;
	virtual void release() = 0;

private:
	std::string name;
};

inline RenderPipeline::~RenderPipeline() {}

/// RenderBuffer
class RenderBuffer {
public:
	RenderBuffer() = default;
	virtual ~RenderBuffer() {}

	virtual void release() {};

	virtual u8* map() { return nullptr; }
	virtual void unmap() {}

	void create_vertex_buffer(const u32 num_verts);
	void write_vertex_buffer(const std::vector<vertexLayout>& vertices);

	void create_index_buffer(const u32 num_indices);
	void write_index_buffer(const std::vector<uint16_t>& indices);

	u32 num_elements() const { return m_num_elements; }
	u32 size_bytes() const { return m_size_bytes; }

private:
	virtual void create_internal() {}

	u32 m_num_elements = 0;
	u32 m_size_bytes = 0;
};


///  VertexHash
struct VertexHash {
	size_t operator()(const vertexLayout& key) const {
		f32 val = key.position.x + key.position.y + key.position.z;
		return (INT_PTR)val;
	}
};
