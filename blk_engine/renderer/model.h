/// model.h
///
/// 2016 blk

#pragma once

#include "blk_core.h"
#include "bounds.h"
#include "Matrix.h"
#include "render_defs.h"
#include "material.h"


struct PointCloudSample {
	Vec3 position;
	Vec3 f_dc;         // Base color (sh0)
	float f_rest[45];  // Raw higher-order SH data
	float opacity;
	Vec3 scale;
	Quat4 rotation;
};

/// ModelIntersection_t
struct ModelIntersection_t {
	ModelIntersection_t() :
		t(FLT_MAX), meshNum(-1), intersectionPoint(Vec3::zero), hasIntersection(false) {}

	float t;
	int meshNum;
	Vec3 intersectionPoint;
	bool hasIntersection;
};

/// Animation
class Animation : public Resource {
	friend class Model;

public:
	Animation();
	virtual TypeInfoType_t type() const { return BLK_TYPEINFO_ANIMATION; }

	float GetLengthInSeconds() const { return m_LengthInSeconds; }

private:
	virtual bool load_internal();
	virtual void release_internal();

private:
	struct RotationKeyFrame_t {
		float m_Time;
		Quat4 m_rotation;
	};

	struct TranslationKeyFrame_t {
		float m_Time;
		Vec3 m_position;
	};

	struct BoneKeyFrames_t {
		std::vector<RotationKeyFrame_t> m_rotationKeyFrames;
		std::vector<TranslationKeyFrame_t> m_TranslationKeyFrames;
	};

	std::vector<BoneKeyFrames_t> m_JointKeyFrameData;
	float m_LengthInSeconds;
};

Vec3 operator*(const Vec3& op1, const BoneMatrix_t& op2);
BoneMatrix_t operator*(const BoneMatrix_t& op1, const BoneMatrix_t& op2);

struct AnimatedBone_t {
	Quat4 m_bone_space_rotation;
	Vec3 m_bone_space_position;

	BoneMatrix_t m_local_space_matrix;
};


/// Model
class Model : public Resource {
	friend class Renderer_DX11;
	friend class Renderer_Dx12;

public:
	Model();
	~Model();

	struct mesh_t {
		mesh_t() :
			m_TriangleIndices(nullptr) { m_Bounds.Reset(); }
		Bounds m_Bounds;
		unsigned short* m_TriangleIndices = nullptr;   // <- don't need to save this
		unsigned int m_NumTriangles = 0;
		unsigned int m_IndexBufferIndex = 0;
		unsigned char m_MaterialIndex = 0;

		// Cpu accessible non-index vertex list.  Used in ray-tracing
		std::vector<Vec3> m_Vertices;
	};

	struct bone_t {
		String m_Name;
		unsigned short m_ParentIndex;
		Quat4 m_RelativeRotation;
		Vec3 m_RelativePosition;
	};

	// Dx 12

	void create_dynamic(const u32 num_verts, const u32 num_indices);

	const RenderBuffer* vertex_buffer() const {
		return m_vertex_buffer;
	}

	const RenderBuffer* index_buffer() const {
		return m_index_buffer;
	}

	u8* map_vertex_buffer();
	void unmap_vertex_buffer(const u32 num_verts = 0);

	u8* map_index_buffer();
	void unmap_index_buffer();

	const std::vector<PointCloudSample>& point_cloud() const { return m_point_cloud; }

	// CPU Access
	void SetCPUAccessOnly(const bool bCPUAccessOnly) { m_bCPUAccessOnly = bCPUAccessOnly; }
	const std::vector<vertexLayout>& GetCPUVertices() const { return m_CPUVertices; }
	const std::vector<ushort>& GetCPUIndices() const { return m_CPUIndices; }

	void SwapTexture(const UINT MeshIdx, const Texture* pTexture, const int textureIdx);

	const std::vector<mesh_t>& GetMeshes() const { return m_Meshes; }
	const std::vector<Material>& GetMaterials() const { return m_Materials; }

	const Bounds& GetBounds() const { return m_Bounds; }
	size_t NumMeshes() const { return m_Meshes.size(); }
	size_t NumMaterials() const { return m_Materials.size(); }
	size_t NumVertices() const { return m_NumVertices; }
	UINT VertexStride() const { return m_Stride; }

	ModelIntersection_t RayIntersection(const Vec3& rayOrigin, const Vec3& rayDirection, const Vec3& modelTranslation, const Quat4& modelRotation, const Vec3& scale) const;

	void Animate(std::vector<BoneMatrix_t>& outMatrices, const float time, const Animation* const pAnimation, const bool bLoopAnim);
	void BlendAnimations(std::vector<BoneMatrix_t>& outMatrices, const Animation* const pFromAnim, const float fromAnimTime, const bool bFromAnimLoops, const Animation* const pToAnim, const float ToAnimTime, const bool bToAnimLoops, const float normalizedBlendTime);
	void SetBoneMatrices(std::vector<AnimatedBone_t>& outMatrices, const float time, const Animation* const pAnimation, const bool bLoopAnim);

	int NumBones() const { return (int)m_bones.size(); }
	int GetBoneIndex(const String& BoneName) const;
	const BoneMatrix_t& GetRefBoneMatrix(const int index) const { return m_RefPose[index]; }
	const BoneMatrix_t& GetInvRefBoneMatrix(const int index) const { return m_InvRefPose[index]; }

	// Debug
	void DrawDebugTBN(const Vec3& modelTranslation, const Quat4& modelRotation, const Vec3& modelScale);

protected:
	virtual bool load_internal();
	bool LoadMS3D();
	bool LoadFBX();
	bool LoadDiablo3();
	bool load_ply();

	virtual void release_internal();

protected:
	RenderBuffer* m_vertex_buffer;
	RenderBuffer* m_index_buffer;

protected:

	//RenderBuffer m_VertexBuffer;
//	RenderBuffer m_IndexBuffer;

	Bounds m_Bounds;

	std::vector<ushort> m_CPUIndices;
	std::vector<vertexLayout> m_CPUVertices;

	int m_NumTriangles;
	int m_NumVertices;
	std::vector<mesh_t> m_Meshes;
	std::vector<Material> m_Materials;
	std::vector<bone_t> m_bones;
	std::vector<BoneMatrix_t> m_RefPose;
	std::vector<BoneMatrix_t> m_InvRefPose;

	std::vector<PointCloudSample> m_point_cloud;

	UINT m_Stride;

	bool m_bCPUAccessOnly : 1;

private:
	virtual TypeInfoType_t type() const { return BLK_TYPEINFO_STATICMODEL; }

	virtual void Load(const std::string& fileName) {};

	// Debug
	std::vector<Vec3> m_DebugPositions;
	std::vector<Vec3> m_DebugNormals;
	std::vector<Vec3> m_DebugTangents;
};
