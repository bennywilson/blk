/// model.cpp
///
/// 2016-2026 blk_engine

#if defined(_WIN32)
	#include <fbxsdk.h>
#endif
#include <fstream>
#include <sstream>
#include "blk_core.h"
#include "entity_header.h"
#include "intersection_tests.h"
#include "model.h"
#include "renderer.h"

#pragma pack(push, packing)
#pragma pack(1)

typedef struct {
	char m_ID[10];
	int m_Version;
} ms3dHeader_t;

typedef struct {
	u8 m_flags;
	float m_vertex[3];
	char m_boneID;
	u8 m_refCount;
} ms3dVertex_t;

typedef struct {
	ushort m_Flags;
	ushort m_VertexIndices[3];
	float m_VertexNormals[3][3];
	float u[3];
	float v[3];
	u8 m_smoothingGroup;
	u8 m_GroupIndex;
} ms3dTriangle_t;

typedef struct {
	char m_Name[32];
	float m_Ambient[4];
	float m_Diffuse[4];
	float m_Specular[4];
	float m_Emissive[4];
	float m_Shininess;
	float m_Transparency;
	char m_Mode;
	char m_Texture[128];
	char m_AlphaMap[128];
} ms3dMaterial_t;

typedef struct {
	float m_Time;
	float m_rotation[3];
} ms3dRotationKeyFrame_t;

typedef struct {
	float m_Time;
	float m_position[3];
} ms3dPositionKeyFrame_t;

typedef struct {
	u8 m_Flags;
	char m_Name[32];
	char m_ParentName[32];
	float m_rotation[3];
	float m_position[3];
	ushort m_NumRotationKeyFrames;
	ushort m_NumPositionKeyFrames;
} ms3dBone_t;

#pragma pack(pop, packing)

/// Model::Model
Model::Model() :
	m_vertex_buffer(nullptr),
	m_index_buffer(nullptr),
	m_NumVertices(0),
	m_NumTriangles(0),
	m_Stride(sizeof(vertexLayout)),
	m_bCPUAccessOnly(false) {
}

/// Model::~Model
Model::~Model() {
	release_internal();
}

/// Model::Load_Internal
bool Model::load_internal() {
	const std::string fileExt = GetFileExtension(full_file_name());
	if (fileExt == "ms3d") {
		return LoadMS3D();
	} else if (fileExt == "fbx") {
		return LoadFBX();
	} else if (fileExt == "diablo3") {
		return LoadDiablo3();
	} else if (fileExt == "ply") {
		return load_ply();
	}

	return false;
}

/// Model::LoadMS3D
bool Model::LoadMS3D() {
	std::ifstream modelFile;
	modelFile.open(blk::os_path(m_full_file_name), std::ifstream::in | std::ifstream::binary);
	blk::error_check(modelFile.good(), "Model::LoadMS3D() - Failed to load model %s", m_full_file_name.c_str());

	// Find the file size
	modelFile.seekg(0, std::ifstream::end);
	const std::streamoff fileSize = modelFile.tellg();
	modelFile.seekg(0, std::ifstream::beg);

	// Load file into memory
	char* const pMemoryFileBuffer = new char[fileSize];
	modelFile.read(pMemoryFileBuffer, fileSize);
	modelFile.close();

	const char* pPtr = pMemoryFileBuffer;

	// Header
	const ms3dHeader_t* const pHeader = (const ms3dHeader_t*)pPtr;
	pPtr += sizeof(ms3dHeader_t);

	blk::error_check(strncmp(pHeader->m_ID, "MS3D000000", 10) == 0, "Model::LoadResource_Internal - Invalid model header %d for %s", pHeader->m_ID, m_full_file_name.c_str());

	// Vertices
	m_Bounds.Reset();

	ushort numVertices = *(ushort*)pPtr;
	pPtr += sizeof(ushort);

	Vec3* const tempVertices = new Vec3[numVertices];
	struct vertexBoneData {
		u8 indices[4];
		u8 weights[4];
	};

	std::vector<vertexBoneData> tempVertexBoneData;
	tempVertexBoneData.resize(numVertices);

	for (uint i = 0; i < numVertices; i++) {
		const ms3dVertex_t* const pVertices = (const ms3dVertex_t*)pPtr;
		pPtr += sizeof(ms3dVertex_t);

		tempVertices[i].x = pVertices->m_vertex[0];
		tempVertices[i].y = pVertices->m_vertex[1];
		tempVertices[i].z = pVertices->m_vertex[2] * -1; // flip from rhs to lhs

		tempVertexBoneData[i].indices[0] = pVertices->m_boneID;

		m_Bounds.AddPoint(tempVertices[i]);
	}

	// Triangles
	const uint numTriangles = *(ushort*)pPtr;
	pPtr += sizeof(ushort);
	ms3dTriangle_t* tempTriangles = new ms3dTriangle_t[numTriangles];

	for (uint i = 0; i < numTriangles; i++) {
		const ms3dTriangle_t* pTriangles = (ms3dTriangle_t*)pPtr;
		pPtr += sizeof(ms3dTriangle_t);
		tempTriangles[i] = *pTriangles;

		// Flip z components of normals from rhs to lhs
		tempTriangles[i].m_VertexNormals[0][2] *= -1;
		tempTriangles[i].m_VertexNormals[1][2] *= -1;
		tempTriangles[i].m_VertexNormals[2][2] *= -1;
	}

	// Groups ------------------------------------------------//
	uint numGroups = *(ushort*)pPtr;
	pPtr += sizeof(ushort);

	m_Meshes.resize(numGroups);

	int ibIndex = 0;
	for (uint iGroup = 0, iDestGroupIdx = 0; iGroup < numGroups; iGroup++) {

		mesh_t& currentMesh = m_Meshes[iGroup];

		pPtr += sizeof(u8);   // Skip flags
		pPtr += 32;      // Skip name

		currentMesh.m_NumTriangles = *(ushort*)pPtr;
		pPtr += sizeof(ushort);

		currentMesh.m_TriangleIndices = new ushort[currentMesh.m_NumTriangles];
		currentMesh.m_IndexBufferIndex = ibIndex;
		ibIndex += currentMesh.m_NumTriangles * 3;

		for (uint iTris = 0; iTris < currentMesh.m_NumTriangles; iTris++) {
			currentMesh.m_TriangleIndices[iTris] = *(ushort*)pPtr;
			pPtr += sizeof(ushort);
		}

		currentMesh.m_MaterialIndex = *(u8*)pPtr;

		pPtr += sizeof(char);
	}

	const uint numMaterials = *(ushort*)pPtr;
	m_Materials.resize(numMaterials);
	pPtr += sizeof(ushort);

	for (uint iMat = 0; iMat < numMaterials; iMat++) {
		const ms3dMaterial_t* const pMat = (ms3dMaterial_t*)pPtr;
		pPtr += sizeof(ms3dMaterial_t);

		m_Materials[iMat].m_DiffuseColor.set(pMat->m_Diffuse[0], pMat->m_Diffuse[1], pMat->m_Diffuse[2], 1.0f);
	}

	// Joints
	const float AnimationFPS = *(float*)pPtr;
	pPtr += sizeof(float);

	const float CurrentTime = *(float*)pPtr;
	pPtr += sizeof(float);

	const int TotalFrames = *(int*)pPtr;
	pPtr += sizeof(int);

	const ushort numJoints = *(ushort*)pPtr;
	pPtr += sizeof(ushort);

	m_bones.resize(numJoints);

	std::unordered_map<String, int, StringHash> boneNameToIdxMap;

	for (uint i = 0; i < m_bones.size(); i++) {
		const ms3dBone_t* const pJoint = (ms3dBone_t*)pPtr;
		pPtr += sizeof(ms3dBone_t);

		m_bones[i].m_Name = pJoint->m_Name;
		m_bones[i].m_ParentIndex = -1;

		boneNameToIdxMap[m_bones[i].m_Name] = i;

		// Find index to parent
		const String parentName(pJoint->m_ParentName);
		auto it = boneNameToIdxMap.find(parentName);
		if (it != boneNameToIdxMap.end()) {
			m_bones[i].m_ParentIndex = it->second;
		}

		m_bones[i].m_RelativePosition.set(pJoint->m_position[0], pJoint->m_position[1], -pJoint->m_position[2]);

		// Convert from euler angles to quaternions
		Quat4 rotationX(Vec3::right, pJoint->m_rotation[0]);
		Quat4 rotationY(Vec3::up, pJoint->m_rotation[1]);
		Quat4 rotationZ(Vec3::forward, -pJoint->m_rotation[2]);
		m_bones[i].m_RelativeRotation = rotationX * rotationY * rotationZ;

		// Skip animations
		pPtr += sizeof(ms3dRotationKeyFrame_t) * pJoint->m_NumPositionKeyFrames;
		pPtr += sizeof(ms3dPositionKeyFrame_t) * pJoint->m_NumRotationKeyFrames;
	}

	// Build ref pose
	m_RefPose.insert(m_RefPose.begin(), m_bones.size(), BoneMatrix_t());
	m_InvRefPose.insert(m_InvRefPose.begin(), m_bones.size(), BoneMatrix_t());

	for (int i = 0; i < m_bones.size(); i++) {

		const int parent = m_bones[i].m_ParentIndex;
		const Mat4 rotationmat = m_bones[i].m_RelativeRotation.to_mat4();
		BoneMatrix_t parentMat;
		if (parent != 65535) {
			parentMat = m_RefPose[parent];
		} else {
			parentMat.SetIdentity();
		}
		m_RefPose[i].SetAxis(0, rotationmat[0].ToVec3());
		m_RefPose[i].SetAxis(1, rotationmat[1].ToVec3());
		m_RefPose[i].SetAxis(2, rotationmat[2].ToVec3());
		m_RefPose[i].SetAxis(3, m_bones[i].m_RelativePosition);
		m_RefPose[i] = m_RefPose[i] * parentMat;

		m_InvRefPose[i] = m_RefPose[i];
		m_InvRefPose[i].Invert();
	}

	// Get comments if any
	int subVersion = *((int*)pPtr);
	pPtr += sizeof(int);

	if (subVersion == 1) {

		for (int i = 0; i < 4; i++) {
			const int numComments = *((int*)pPtr);
			pPtr += sizeof(int);

			for (int j = 0; j < numComments; j++) {
				pPtr += sizeof(int);

				size_t commentLen = *((size_t*)pPtr);
				pPtr += sizeof(size_t);
				if (commentLen > 0) {
					pPtr += sizeof(char) * commentLen;
				}
			}
		}
	}

	// Get additional vertex weights.
	//
	// The whole section is checked, not just its first byte: it is a subversion
	// int followed by one fixed-size record per vertex, and testing `pPtr` alone
	// let a file that stops early be read past the end of the buffer. Either all
	// of it is there or none of it is.
	constexpr size_t k_vertex_weight_record = (sizeof(char) * 3) + (sizeof(char) * 3) + (sizeof(uint) * 2);
	const size_t extra_weights_bytes = sizeof(int) + (size_t)numVertices * k_vertex_weight_record;
	if ((size_t)(pPtr - pMemoryFileBuffer) + extra_weights_bytes <= (size_t)fileSize) {
		subVersion = *((int*)pPtr);
		pPtr += sizeof(int);

		for (int i = 0; i < numVertices; i++) {
			// Assuming subversion 3
			const char* const pBoneIds = (const char*)pPtr;
			pPtr += sizeof(char) * 3;

			tempVertexBoneData[i].indices[1] = pBoneIds[0];
			tempVertexBoneData[i].indices[2] = pBoneIds[1];
			tempVertexBoneData[i].indices[3] = pBoneIds[2];

			const char* const pWeights = (const char*)pPtr;
			pPtr += sizeof(char) * 3;


			// Spelled out rather than chained: `a == b == c == 0` parses as
			// `((a == b) == c) == 0`, which was true for most weight
			// combinations and bound those vertices rigidly to one bone.
			if (pWeights[0] == 0 && pWeights[1] == 0 && pWeights[2] == 0) {
				tempVertexBoneData[i].weights[0] = 255;
				tempVertexBoneData[i].weights[1] = 0;
				tempVertexBoneData[i].weights[2] = 0;
				tempVertexBoneData[i].weights[3] = 0;
			} else {
				tempVertexBoneData[i].weights[0] = pWeights[0];
				tempVertexBoneData[i].weights[1] = pWeights[1];
				tempVertexBoneData[i].weights[2] = pWeights[2];
				tempVertexBoneData[i].weights[3] = 255 - pWeights[0] - pWeights[1] - pWeights[2];
			}

			const uint* const pExtra = (uint*)pPtr;
			pPtr += sizeof(uint) * 2;
		}
	} else {
		// The section is missing or truncated. Every vertex keeps the primary
		// bone read with its position and is bound rigidly to it - weights are
		// zero-initialised otherwise, which would collapse the mesh, and reading
		// the section anyway is what used to walk off the end of the buffer.
		blk::warn("Model::LoadMS3D() - %s has no vertex weight section; binding each vertex rigidly to its primary bone", m_full_file_name.c_str());
		for (uint i = 0; i < numVertices; i++) {
			tempVertexBoneData[i].weights[0] = 255;
		}
	}

	// create index buffer
	m_CPUIndices.resize(ibIndex);

	ibIndex = 0;

	std::unordered_map<vertexLayout, int, VertexHash> vertHash;

	for (uint i = 0; i < m_Meshes.size(); i++) {
		blk::error_check(ibIndex == m_Meshes[i].m_IndexBufferIndex, "Model::Load_Internal() - Index buffer mismatch");

		for (uint iTris = 0; iTris < m_Meshes[i].m_NumTriangles; iTris++) {
			const int triangleIndex = m_Meshes[i].m_TriangleIndices[iTris];
			ms3dTriangle_t& currentTriangle = tempTriangles[triangleIndex];
			for (int j = 0; j < 3; j++) {
				vertexLayout newVert;
				newVert.position = tempVertices[currentTriangle.m_VertexIndices[j]];
				newVert.uv.set(currentTriangle.u[j], currentTriangle.v[j]);

				Vec4 normal(currentTriangle.m_VertexNormals[j][0], currentTriangle.m_VertexNormals[j][1], currentTriangle.m_VertexNormals[j][2], 0);
				newVert.SetNormal(normal);

				// TODO: We probably want vertex colors even if not cpu only
				if (m_bCPUAccessOnly) {
					const Material& modelMaterial = GetMaterials()[m_Meshes[i].m_MaterialIndex];
					newVert.SetColor(modelMaterial.GetDiffuseColor());
				} else {
					const vertexBoneData& boneData = tempVertexBoneData[currentTriangle.m_VertexIndices[j]];
					newVert.color[0] = (u8)boneData.indices[0];
					newVert.color[1] = (u8)boneData.indices[1];
					newVert.color[2] = (u8)boneData.indices[2];
					newVert.color[3] = (u8)boneData.indices[3];

					newVert.tangent[0] = (u8)boneData.weights[0];
					newVert.tangent[1] = (u8)boneData.weights[1];
					newVert.tangent[2] = (u8)boneData.weights[2];
					newVert.tangent[3] = (u8)boneData.weights[3];
				}

				auto it = vertHash.find(newVert);

				int vertIndex;
				if (it == vertHash.end()) {
					vertIndex = (int)m_CPUVertices.size();
					vertHash[newVert] = vertIndex;
					m_CPUVertices.push_back(newVert);
					m_Meshes[i].m_Bounds.AddPoint(newVert.position);

				} else {
					vertIndex = it->second;
				}

				m_CPUIndices[(size_t)(ibIndex + (2 - j))] = vertIndex;

				// todo - Maybe should be editor only?
				m_Meshes[i].m_Vertices.push_back(newVert.position);
			}

			ibIndex += 3;
		}
	}

	if (m_bCPUAccessOnly == false) {
		std::vector<vertexLayout> verts(m_CPUVertices.size());

		for (uint i = 0; i < m_CPUVertices.size(); i++) {
			verts[i].position = m_CPUVertices[i].position;
			verts[i].uv = m_CPUVertices[i].uv;

			verts[i].normal[0] = m_CPUVertices[i].normal[0];
			verts[i].normal[1] = m_CPUVertices[i].normal[1];
			verts[i].normal[2] = m_CPUVertices[i].normal[2];
			verts[i].normal[3] = m_CPUVertices[i].normal[3];

			verts[i].tangent[0] = m_CPUVertices[i].tangent[0];
			verts[i].tangent[1] = m_CPUVertices[i].tangent[1];
			verts[i].tangent[2] = m_CPUVertices[i].tangent[2];
			verts[i].tangent[3] = m_CPUVertices[i].tangent[3];


			verts[i].color[0] = m_CPUVertices[i].color[0];
			verts[i].color[1] = m_CPUVertices[i].color[1];
			verts[i].color[2] = m_CPUVertices[i].color[2];
			verts[i].color[3] = m_CPUVertices[i].color[3];

			// color, normal, etc
		}

		// D3D12
		if (g_renderer != nullptr) {
			m_vertex_buffer = g_renderer->create_render_buffer();
			if (m_vertex_buffer != nullptr) {
				m_vertex_buffer->write_vertex_buffer(verts);
			}

			m_index_buffer = g_renderer->create_render_buffer();
			if (m_index_buffer != nullptr) {
				m_index_buffer->write_index_buffer(m_CPUIndices);
			}
		}
	}

	delete[] tempVertices;
	delete[] tempTriangles;
	delete[] pMemoryFileBuffer;

	return true;
}

/// Model::LoadFBX
#if defined(_WIN32)
FbxManager* g_pFBXSDKManager = nullptr;

FbxAMatrix GetGeometryTransformation(FbxNode const* inNode) {

	blk::error_check(inNode != nullptr, "GetGeometryTransformation() - null mesh");

	const FbxVector4 lT = inNode->GetGeometricTranslation(FbxNode::eSourcePivot);
	const FbxVector4 lR = inNode->GetGeometricRotation(FbxNode::eSourcePivot);
	const FbxVector4 lS = inNode->GetGeometricScaling(FbxNode::eSourcePivot);

	return FbxAMatrix(lT, lR, lS);
}

bool Model::LoadFBX() {

	struct FBXData {
		FbxImporter* pImporter = nullptr;
		FbxScene* pScene = nullptr;

		~FBXData() {
			if (pImporter != nullptr) {
				pImporter->Destroy();
			}
			if (pScene != nullptr) {
				pScene->Destroy();
			}
		}
	} fbxData;

	if (g_pFBXSDKManager == nullptr) {
		g_pFBXSDKManager = FbxManager::Create();

		FbxIOSettings* const pIOsettings = FbxIOSettings::Create(g_pFBXSDKManager, IOSROOT);
		g_pFBXSDKManager->SetIOSettings(pIOsettings);
	}

	fbxData.pImporter = FbxImporter::Create(g_pFBXSDKManager, "");

	bool bSuccess = fbxData.pImporter->Initialize(full_file_name().c_str(), -1, g_pFBXSDKManager->GetIOSettings());
	if (bSuccess == false) {
		return false;
	}

	fbxData.pScene = FbxScene::Create(g_pFBXSDKManager, "");
	bSuccess = fbxData.pImporter->Import(fbxData.pScene);
	if (bSuccess == false) {
		return false;
	}

	FbxNode* pRootNode = fbxData.pScene->GetRootNode();
	blk::error_check(pRootNode != nullptr, "Model::LoadFBX() - Root node not found in %s", full_file_name().c_str());

	std::unordered_map<vertexLayout, int, VertexHash> vertexMap;
	std::vector<vertexLayout> vertexList;
	std::vector<ushort> indexList;

	std::map<int, Bounds> boneToBounds;
	std::map<int, int> vertToBone;
	std::map<int, Color> boneToColor;


	for (int iMesh = 0; iMesh < pRootNode->GetChildCount(); iMesh++) {

		FbxMesh* const pFBXMesh = pRootNode->GetChild(iMesh)->GetMesh();
		if (pFBXMesh == nullptr) {
			continue;
		}

		m_Meshes.push_back(mesh_t());
		mesh_t& newMesh = m_Meshes[m_Meshes.size() - 1];
		newMesh.m_IndexBufferIndex = (unsigned int)indexList.size();
		newMesh.m_MaterialIndex = 0;
		newMesh.m_NumTriangles = pFBXMesh->GetPolygonCount();

		newMesh.m_Bounds.Reset();

		uint vertexCount = 0;

		int verts_added = 0;

		int numDeformers = pFBXMesh->GetDeformerCount();
		FbxAMatrix geomXForm = GetGeometryTransformation(pRootNode->GetChild(iMesh));
		for (int iDeform = 0; iDeform < numDeformers; iDeform++) {
			FbxSkin* pCurSkin = (FbxSkin*)pFBXMesh->GetDeformer(iDeform, FbxDeformer::eSkin);
			if (pCurSkin == nullptr) {
				continue;
			}


			uint numClusters = pCurSkin->GetClusterCount();
			for (uint iCluster = 0; iCluster < numClusters; iCluster++) {
				FbxCluster* pCurCluster = pCurSkin->GetCluster(iCluster);
				std::string curJointName = pCurCluster->GetLink()->GetName();

				boneToBounds[iCluster].Reset();
				Color boneColor(blk::frand() * 0.5f + 0.5f, blk::frand() * 0.5f + 0.5f, blk::frand() * 0.5f + 0.5f, 1.0f);
				boneToColor[iCluster] = boneColor;

				FbxAMatrix xformMat;
				FbxAMatrix xformLinkMat;
				FbxAMatrix globalBindPoseInverseMatrix;

				pCurCluster->GetTransformMatrix(xformMat);
				pCurCluster->GetTransformLinkMatrix(xformLinkMat);
				globalBindPoseInverseMatrix = xformLinkMat.Inverse() * xformMat * geomXForm;
				//blk::log( "Yay!");

				unsigned int numOfIndices = pCurCluster->GetControlPointIndicesCount();
				int* pCtrlPtList = pCurCluster->GetControlPointIndices();
				for (unsigned int i = 0; i < numOfIndices; ++i) {
					verts_added++;
					vertToBone[pCtrlPtList[i]] = iCluster;
				}
			}
		}

		for (int iTri = 0; iTri < (int)newMesh.m_NumTriangles; iTri++) {

			int iCurVertex = vertexCount + 2;
			for (int iTriVert = 2; iTriVert >= 0; iTriVert--, vertexCount++, iCurVertex--) {
				vertexLayout triVert;
				memset(&triVert, 0, sizeof(triVert));

				const int iCtrlPt = pFBXMesh->GetPolygonVertex(iTri, iTriVert);
				const FbxVector4 ctrlPt = pFBXMesh->GetControlPointAt(iCtrlPt);

				triVert.position.set((float)ctrlPt[1], (float)ctrlPt[2], -(float)ctrlPt[0]);
				newMesh.m_Bounds.AddPoint(triVert.position);

				FbxGeometryElementNormal* const pFBXVertNormal = pFBXMesh->GetElementNormal(0);
				if (pFBXVertNormal != nullptr) {
					auto mappingMode = pFBXVertNormal->GetMappingMode();
					blk::error_check(mappingMode == FbxGeometryElement::eByPolygonVertex, "Model::LoadFBX() - Invalid vertex normal mapping mode");

					auto refMode = pFBXVertNormal->GetReferenceMode();
					blk::error_check(refMode == FbxGeometryElement::eDirect, "Model::LoadFBX() - Invalid vertex normal reference mode");

					const auto fbxNormal = pFBXVertNormal->GetDirectArray().GetAt(iCurVertex).mData;
					Vec4 normal((float)fbxNormal[1], (float)fbxNormal[2], -(float)fbxNormal[0], 0.0f);
					normal.w = 0;
					triVert.SetNormal(normal);
				}

				FbxGeometryElementTangent* const pFBXVertTangent = pFBXMesh->GetElementTangent(0);
				if (pFBXVertTangent != nullptr) {

					auto mappingMode = pFBXVertTangent->GetMappingMode();
					blk::error_check(mappingMode == FbxGeometryElement::eByPolygonVertex, "Model::LoadFBX() - Invalid vertex tangent mapping mode");

					auto refMode = pFBXVertTangent->GetReferenceMode();
					blk::error_check(refMode == FbxGeometryElement::eDirect, "Model::LoadFBX() - Invalid vertex tangent reference mode");

					const auto fbxTangent = pFBXVertTangent->GetDirectArray().GetAt(iCurVertex).mData;
					Vec4 tangent((float)fbxTangent[1], (float)fbxTangent[2], -(float)fbxTangent[0], 0.0f);
					triVert.SetTangent(tangent);
				}

				/*	FbxGeometryElementBinormal *const pFBXVertBinormal = pFBXMesh->GetElementBinormal(0);
					if ( pFBXVertBinormal != nullptr ) {

						auto mappingMode = pFBXVertBinormal->GetMappingMode();
						blk::error_check( mappingMode == FbxGeometryElement::eByPolygonVertex, "Model::LoadFBX() - Invalid vertex binormal mapping mode" );

						auto refMode = pFBXVertBinormal->GetReferenceMode();
						blk::error_check( refMode == FbxGeometryElement::eDirect, "Model::LoadFBX() - Invalid vertex binormal reference mode" );

						const auto fbxBinormal = pFBXVertBinormal->GetDirectArray().GetAt(iCurVertex).mData;
						Vec4 binormal( (float)fbxBinormal[1], (float)fbxBinormal[2], -(float)fbxBinormal[0], 0.0f );
				//		triVert.SetBitangent( binormal );
					}*/

				FbxGeometryElementUV* const pFBXVertUV = pFBXMesh->GetElementUV(0);
				if (pFBXVertUV != nullptr) {

					auto uvMapMode = pFBXVertUV->GetMappingMode();
					blk::error_check(uvMapMode == FbxGeometryElement::eByPolygonVertex, "Model::LoadFBX() - Invalid uvs mapping mode");

					auto uvRefMode = pFBXVertUV->GetReferenceMode();
					blk::error_check(uvRefMode == FbxGeometryElement::eIndexToDirect, "Model::LoadFBX() - Invalid uvs reference mode");

					const int uvIndex = pFBXVertUV->GetIndexArray().GetAt(iCurVertex);
					const auto fbxUV = pFBXVertUV->GetDirectArray().GetAt(uvIndex).mData;
					triVert.uv.set((float)fbxUV[0], 1.0f - (float)fbxUV[1]);
				}

				FbxGeometryElementVertexColor* const pFBXVertColor = pFBXMesh->GetElementVertexColor(0);
				if (pFBXVertColor != nullptr) {

					auto mappingMode = pFBXVertColor->GetMappingMode();
					blk::error_check(mappingMode == FbxGeometryElement::eByPolygonVertex, "Model::LoadFBX() - Invalid vertex color mapping mode");

					auto refMode = pFBXVertColor->GetReferenceMode();
					blk::error_check(refMode == FbxGeometryElement::eIndexToDirect, "Model::LoadFBX() - Invalid vertex color reference mode");

					const int colorIndex = pFBXVertColor->GetIndexArray().GetAt(iCurVertex);
					const auto fbxColor = pFBXVertColor->GetDirectArray().GetAt(colorIndex);
					Vec4 color((float)fbxColor.mRed, (float)fbxColor.mGreen, (float)fbxColor.mBlue, (float)fbxColor.mAlpha);
					triVert.SetColor(color);
				}

				// todo this was required for destructibles to work
				int boneIdx = vertToBone[iCtrlPt];
				boneToBounds[boneIdx].AddPoint(triVert.position);
				triVert.color[0] = (u8)boneIdx;
				triVert.color[1] = 0;
				triVert.color[2] = 0;
				triVert.color[3] = 0;
				triVert.tangent[0] = 255;
				triVert.tangent[1] = 0;
				triVert.tangent[2] = 0;
				triVert.tangent[3] = 0;

				/*
									newVert.color[0] = (u8)boneIndices[currentTriangle.m_VertexIndices[j]];
					newVert.color[1] = (u8)boneIndices[currentTriangle.m_VertexIndices[j]];
					newVert.color[2] = (u8)boneIndices[currentTriangle.m_VertexIndices[j]];
					newVert.color[3] = (u8)boneIndices[currentTriangle.m_VertexIndices[j]];
				*/
				//triVert.SetColor( boneToColor[boneIdx] );
				auto vertIt = vertexMap.find(triVert);
				if (vertIt == vertexMap.end()) {
					const int vertIdx = (int)vertexList.size();
					vertexMap.insert(std::pair<vertexLayout, int>(triVert, vertIdx));
					vertexList.push_back(triVert);
					indexList.push_back(vertIdx);
				} else {
					indexList.push_back(vertIt->second);
				}
			}
		}
	}
	/*
		for ( int i = 0; i < pRootNode->GetChildCount(); i++ ) {
			FbxNode * pCurNode = pRootNode->GetChild(i);
			blk::log( "Processing parent node %s", pCurNode->GetName() );

			for ( int j = 0; j < pCurNode->GetChildCount(); j++ ) {
				FbxNode * pRootBone = pCurNode->GetChild(j);
				if ( pRootBone->GetNodeAttribute() == nullptr || pRootBone->GetNodeAttribute()->GetAttributeType() != FbxNodeAttribute::eSkeleton ) {
					continue;
				}
				blk::log( "	Processing Root bone %s", pRootBone->GetName() );

				for ( int l = 0; l < pRootBone->GetChildCount(); l++ ) {
					FbxNode * pBoneNode = pRootBone->GetChild(l);
					if ( pBoneNode->GetNodeAttribute() == nullptr || pBoneNode->GetNodeAttribute()->GetAttributeType() != FbxNodeAttribute::eSkeleton ) {
						continue;
					}
					blk::log( "		Processing child bones %s", pBoneNode->GetName() );
				}
			}
		}*/


		// D3D12
	if (g_renderer != nullptr) {
		m_vertex_buffer = g_renderer->create_render_buffer();
		if (m_vertex_buffer != nullptr) {
			m_vertex_buffer->write_vertex_buffer(vertexList);
		}

		m_index_buffer = g_renderer->create_render_buffer();
		if (m_index_buffer != nullptr) {
			m_index_buffer->write_index_buffer(indexList);
		}
	}

	Material newMaterial;
	newMaterial.m_shader = nullptr;//(Shader *) g_ResourceManager.GetResource( "../../kbEngine/assets/Shaders/basicShader.kbShader", true );
	m_Materials.push_back(newMaterial);

	m_bones.resize(boneToBounds.size());
	for (int i = 0; i < boneToBounds.size(); i++) {
		Bounds& boneBounds = boneToBounds[i];
		m_bones[i].m_RelativePosition = boneBounds.Center();
		m_bones[i].m_RelativeRotation = Quat4(0.0f, 0.0f, 0.0f, 1.0f);

		BoneMatrix_t invRef;
		invRef.SetIdentity();
		invRef.SetAxis(3, -m_bones[i].m_RelativePosition);
		m_InvRefPose.push_back(invRef);

		BoneMatrix_t ref;
		ref.SetIdentity();
		ref.SetAxis(3, m_bones[i].m_RelativePosition);
		m_RefPose.push_back(ref);
	}
	return true;
}
#else
// The FBX SDK ships no wasm build, so .fbx models don't load off Windows.
bool Model::LoadFBX() {
	blk::warn("Model::LoadFBX() - FBX is unsupported on this platform, skipping %s", full_file_name().c_str());
	return false;
}
#endif

/// Model::LoadDiablo3
bool Model::LoadDiablo3() {
	struct FileReader {
		FileReader() {}
		const std::string delimiters = "\n,";

		int GetInt() {
			std::string::size_type endPos = m_ModelText.find_first_of(delimiters, m_CurPos);
			int retInt = 0;
			if (endPos != std::string::npos) {
				const std::string intstr = m_ModelText.substr(m_CurPos, endPos - m_CurPos);
				retInt = std::atoi(intstr.c_str());
				m_CurPos = endPos + 1;
			} else {
				m_CurPos = m_ModelText.size();
			}

			return retInt;
		}

		float GetFloat() {
			std::string::size_type endPos = m_ModelText.find_first_of(delimiters, m_CurPos);
			float retFloat = 0;
			if (endPos != std::string::npos) {
				const std::string floatstr = m_ModelText.substr(m_CurPos, endPos - m_CurPos);
				retFloat = (float)std::atof(floatstr.c_str());
				m_CurPos = endPos + 1;
			} else {
				m_CurPos = m_ModelText.size();
			}

			return retFloat;
		}

		Vec2 GetVec2() {
			return Vec2(GetFloat(), GetFloat());
		}

		Vec3 GetVec3() {
			return Vec3(GetFloat(), GetFloat(), GetFloat());
		}

		Vec4 GetVec4() {
			return Vec4(GetFloat(), GetFloat(), GetFloat(), GetFloat());
		}

		std::string m_ModelText;
		size_t m_CurPos = 0;

	} fileReader;


	std::ifstream modelFile;
	modelFile.open(blk::os_path(m_full_file_name), std::ifstream::in);
	blk::error_check(modelFile.good(), "Model::LoadDiablo3() - Failed to load model %s", m_full_file_name.c_str());
	fileReader.m_ModelText = std::string((std::istreambuf_iterator<char>(modelFile)), std::istreambuf_iterator<char>());

	std::vector<vertexLayout> vertexList;
	std::vector<ushort> indexList;

	while (fileReader.m_CurPos < fileReader.m_ModelText.size()) {
		// Vertex,Index,POSITION 0,POSITION 1,POSITION 2,NORMAL 0,NORMAL 1,NORMAL 2,NORMAL 3,COLOR0 0,COLOR0 1,COLOR0 2,COLOR0 3,COLOR1 0,COLOR1 1,COLOR1 2,COLOR1 3,TEXCOORD0 0,TEXCOORD0 1,TEXCOORD0 2,TEXCOORD0 3,TEXCOORD1 0,TEXCOORD1 1,TEXCOORD1 2,TEXCOORD1 3,BLENDINDICES 0,BLENDINDICES 1,BLENDINDICES 2,BLENDINDICES 3,BLENDWEIGHT 0,BLENDWEIGHT 1,BLENDWEIGHT 2

		const int vertNum = fileReader.GetInt();
		const int vertIdx = fileReader.GetInt();
		const Vec3 vertPos = fileReader.GetVec3();
		const Vec4 vertNormal = fileReader.GetVec4();
		const Vec4 vertColor1 = fileReader.GetVec4();
		const Vec4 vertColor2 = fileReader.GetVec4();
		const Vec4 vertUV1 = fileReader.GetVec4();
		const Vec4 vertUV2 = fileReader.GetVec4();

		// Blend Indices
		fileReader.GetInt();
		fileReader.GetInt();
		fileReader.GetInt();
		fileReader.GetInt();

		// Blend Weights
		fileReader.GetVec3();

		vertexLayout newVert;
		newVert.position.set(vertPos.y, vertPos.x, vertPos.z);
		newVert.normal[0] = (u8)vertNormal.x;
		newVert.normal[1] = (u8)vertNormal.y;
		newVert.normal[2] = (u8)vertNormal.z;
		newVert.normal[3] = (u8)vertNormal.w;

		if (vertUV1.z == 128) {
			newVert.uv.x = vertUV1.w / 512.0f;
		} else {
			newVert.uv.x = 0.5f + vertUV1.w / 512.0f;
		}

		if (vertUV1.x == 128) {
			newVert.uv.y = vertUV1.y / 512.0f;
		} else {
			newVert.uv.y = 0.5f + vertUV1.y / 512.0f;
		}

		vertexList.push_back(newVert);
		indexList.push_back(vertNum);
		//blk::log( "%d, %d, (%f %f %f), (%f %f %f %f), (%f %f)", vertNum, vertIdx, vertPos.x, vertPos.y, vertPos.z, vertNormal.x, vertNormal.y, vertNormal.z, vertNormal.w, vertUV1.x, vertUV1.y );
	}

	//m_VertexBuffer.CreateVertexBuffer(vertexList);
	//m_IndexBuffer.CreateIndexBuffer(indexList);

	if (g_renderer) {
		auto* vertex_buffer = g_renderer->create_render_buffer();
		vertex_buffer->write_vertex_buffer(vertexList);

		auto* index_buffer = g_renderer->create_render_buffer();
		index_buffer->write_index_buffer(indexList);
	}

	m_Meshes.push_back(mesh_t());
	mesh_t& newMesh = m_Meshes[m_Meshes.size() - 1];
	newMesh.m_IndexBufferIndex = 0;
	newMesh.m_MaterialIndex = 0;
	newMesh.m_NumTriangles = (uint)indexList.size() / 3;

	Material newMaterial;
	newMaterial.m_shader = nullptr;//(Shader *) g_ResourceManager.GetResource( "../../kbEngine/assets/Shaders/basicShader.kbShader", true );
	m_Materials.push_back(newMaterial);

	return true;
}

bool Model::load_ply() {
	blk::log("Model::load_ply() - Using Fast Binary Loader");

	std::ifstream file(blk::os_path(name()), std::ios::binary);
	if (!file.is_open()) {
		blk::log("Failed to open file: %s", name().c_str());
		return false;
	}

	// Parse Header
	std::string line;
	size_t vertex_count = 0;
	size_t vertex_stride = 0; // Total bytes per vertex
	std::unordered_map<std::string, size_t> prop_offsets;

	while (std::getline(file, line)) {
		// Handle Windows \r line endings if present
		if (!line.empty() && line.back() == '\r') {
			line.pop_back();
		}

		if (line == "end_header") {
			break; // Header done, binary data starts immediately after
		}

		std::istringstream iss(line);
		std::string token;
		iss >> token;

		if (token == "element") {
			std::string type;
			iss >> type;
			if (type == "vertex") {
				iss >> vertex_count;
			}
		} else if (token == "property") {
			std::string type, name;
			iss >> type >> name;
			// Record the u8 offset of this property
			prop_offsets[name] = vertex_stride;

			// Standard 3DGS properties are all 32-bit floats
			// If you have double/uint/uchar properties, you'd calculate size dynamically here.
			vertex_stride += sizeof(float);
		}
	}

	blk::log("# Verts found in header: %llu", vertex_count);

	if (vertex_count == 0 || vertex_stride == 0) {
		return false;
	}

	// Pre-cache offsets to avoid map lookups inside the tight loop
	// (If a property is missing in the file, map returns 0, which is safe enough for a blind read,
	// but ideally you'd check if prop_offsets.count(name) > 0).
	const size_t off_pos[3] = { prop_offsets["x"], prop_offsets["y"], prop_offsets["z"] };
	const size_t off_scale[3] = { prop_offsets["scale_0"], prop_offsets["scale_1"], prop_offsets["scale_2"] };
	const size_t off_rot[4] = { prop_offsets["rot_0"], prop_offsets["rot_1"], prop_offsets["rot_2"], prop_offsets["rot_3"] };
	const size_t off_opac = prop_offsets["opacity"];
	const size_t off_fdc[3] = { prop_offsets["f_dc_0"], prop_offsets["f_dc_1"], prop_offsets["f_dc_2"] };

	size_t off_frest[45];
	for (int i = 0; i < 45; ++i) {
		off_frest[i] = prop_offsets["f_rest_" + std::to_string(i)];
	}

	// Block-read the entire binary chunk into a raw buffer
	size_t data_size = vertex_count * vertex_stride;
	std::vector<char> raw_buffer(data_size);
	file.read(raw_buffer.data(), data_size);

	// Stride through the memory and build the Engine structures
	m_point_cloud.resize(vertex_count);

	for (size_t i = 0; i < vertex_count; ++i) {
		// Base pointer to the start of this specific vertex's bytes
		const char* v_base = raw_buffer.data() + (i * vertex_stride);
		auto& pt = m_point_cloud[i];

		// Helper macro/lambda to pluck a float from the raw bytes
		auto get_float = [&](size_t offset) -> float {
			return *reinterpret_cast<const float*>(v_base + offset);
		};

		pt.position = Vec3(
			get_float(off_pos[0]),
			-get_float(off_pos[1]),
			get_float(off_pos[2]));

		// Standard 3DGS ply layout is rot_0=w, rot_1=x, rot_2=y, rot_3=z.
		// The Y-mirror above would alone call for negating x and z, but 3DGS's
		// quat->matrix convention is the transpose of Quat4::to_mat4() (i.e.
		// its conjugate: negate x,y,z), and gaussian_splat_draw.hlsl's
		// quat_to_matrix() uses to_mat4()'s formula. Composing both negations
		// nets out to just negating y.
		pt.rotation = Quat4(
			get_float(off_rot[1]), // x
			-get_float(off_rot[2]), // y
			get_float(off_rot[3]), // z
			get_float(off_rot[0])  // w
		);

		pt.scale = Vec3(get_float(off_scale[0]), get_float(off_scale[1]), get_float(off_scale[2]));
		pt.opacity = get_float(off_opac);

		pt.f_dc = Vec3(get_float(off_fdc[0]), get_float(off_fdc[1]), get_float(off_fdc[2]));

		for (int r = 0; r < 45; ++r) {
			pt.f_rest[r] = get_float(off_frest[r]);
		}
	}

	blk::log("Successfully loaded %llu splats directly to memory.", vertex_count);
	return true;
}
/// Model::create_dynamic
void Model::create_dynamic(const u32 num_verts, const u32 num_indices) {
	if (m_NumVertices > 0 || m_Meshes.size() > 0 || m_Materials.size() > 0) {
		release_internal();
	}

	m_NumVertices = num_verts;

	if (g_renderer != nullptr) {
		m_vertex_buffer = g_renderer->create_render_buffer();
		m_vertex_buffer->create_vertex_buffer((u32)m_NumVertices);

		m_index_buffer = g_renderer->create_render_buffer();
		m_index_buffer->create_vertex_buffer((u32)num_indices);
	}
}

/// Model::map_vertex_buffer
u8* Model::map_vertex_buffer() {
	return m_vertex_buffer->map();
}

/// Model::unmap_vertex_buffer
void Model::unmap_vertex_buffer(const u32 num_verts_written) {
	m_vertex_buffer->unmap();
}

/// Model::map_index_buffer
u8* Model::map_index_buffer() {
	return m_index_buffer->map();
}

/// Model::unmap_index_buffer
void Model::unmap_index_buffer() {
	m_index_buffer->unmap();
}

/// Model::SwapTexture
void Model::SwapTexture(const UINT meshIdx, const Texture* pTexture, const int textureIdx) {

	if (meshIdx < 0 || meshIdx >= m_Materials.size()) {
		return;
	}

	Material& material = m_Materials[meshIdx];
	if (textureIdx < 0 || textureIdx >= material.m_Textures.size() + 1) {
		return;
	}

	if (textureIdx < material.m_Textures.size()) {
		material.m_Textures[textureIdx] = pTexture;
	} else {
		material.m_Textures.push_back(pTexture);
	}
}

/// Model::RayIntersection
ModelIntersection_t Model::RayIntersection(const Vec3& inRayOrigin, const Vec3& inRayDirection, const Vec3& modelTranslation, const Quat4& modelRotation, const Vec3& scale) const {
	ModelIntersection_t intersectionInfo;

	Mat4 inverseModelRotation;
	inverseModelRotation.make_scale(scale);
	inverseModelRotation = inverseModelRotation * modelRotation.to_mat4();
	inverseModelRotation.inverse_self();

	const Vec3 rayStart = (inRayOrigin - modelTranslation) * inverseModelRotation;
	const Vec3 rayDir = inRayDirection.normalize_safe() * inverseModelRotation;
	float t = FLT_MAX;

	for (int iMesh = 0; iMesh < m_Meshes.size(); iMesh++) {
		for (size_t iVert = 0; iVert < m_Meshes[iMesh].m_Vertices.size(); iVert += 3) {

			const Vec3& v0 = m_Meshes[iMesh].m_Vertices[iVert + 0];
			const Vec3& v1 = m_Meshes[iMesh].m_Vertices[iVert + 1];
			const Vec3& v2 = m_Meshes[iMesh].m_Vertices[iVert + 2];

			if (RayTriIntersection(t, rayStart, rayDir, v0, v1, v2)) {
				if (t < intersectionInfo.t && t >= 0) {
					intersectionInfo.t = t;
					intersectionInfo.meshNum = iMesh;
				}
			}
		}
	}

	if (intersectionInfo.meshNum >= 0) {
		intersectionInfo.hasIntersection = true;
		intersectionInfo.intersectionPoint = inRayOrigin + intersectionInfo.t * inRayDirection.normalize_safe();
	}

	return intersectionInfo;
}

/// Model::release_internals
void Model::release_internal() {
	//m_VertexBuffer.Release();
	//m_IndexBuffer.Release();

	m_Materials.clear();

	for (uint i = 0; i < m_Meshes.size(); i++) {
		delete[] m_Meshes[i].m_TriangleIndices;
	}
	m_Meshes.clear();

	m_CPUVertices.clear();
	m_CPUIndices.clear();
	m_Bounds.Reset();
}

/// Model::GetBoneIndex
int Model::GetBoneIndex(const String& BoneName) const {
	for (int i = 0; i < m_bones.size(); i++) {
		if (m_bones[i].m_Name == BoneName) {
			return i;
		}
	}

	return -1;
}

/// Model::SetBoneMatrices
void Model::SetBoneMatrices(
	std::vector<AnimatedBone_t>& bones,
	const f32 time,
	const Animation* const animation,
	const bool is_looping) {
	if (m_bones.size() == 0 || animation == nullptr) {
		return;
	}

	const Animation& anim_data = *animation;
	if (anim_data.m_JointKeyFrameData.size() == 0) {
		return;
	}

	const f32 anim_duration = anim_data.m_LengthInSeconds;
	const f32 anim_time = (is_looping && time > anim_duration) ? (fmod(time, anim_duration)) : (time);

	bones.resize(m_bones.size());
	for (u32 i = 0; i < m_bones.size(); i++) {
		bones[i].m_bone_space_position = Vec3::zero;
		bones[i].m_bone_space_rotation = Quat4::identity;

		const Animation::BoneKeyFrames_t& joints = anim_data.m_JointKeyFrameData[i];
		for (u32 next_key = 0; next_key < joints.m_rotationKeyFrames.size(); next_key++) {
			// todo: fix linear search to find next key
			f32 next_time = joints.m_rotationKeyFrames[next_key].m_Time;
			if (anim_time >= next_time && next_key != joints.m_rotationKeyFrames.size() - 1) {
				continue;
			}

			i32 prev_key = next_key;
			if (anim_time >= next_time) {
				if (is_looping) {
					prev_key = (i32)joints.m_rotationKeyFrames.size() - 1;

					// todo: determine proper wrapping behavior
					if (joints.m_rotationKeyFrames.size() > 1) {
						next_key = 1;
					} else {
						next_key = 0;
					}
				} else {
					prev_key = next_key = (i32)joints.m_rotationKeyFrames.size() - 1;
				}
			} else {
				prev_key = blk::clamp(prev_key - 1, 0, (i32)joints.m_rotationKeyFrames.size());
			}

			const f32 prev_time = joints.m_rotationKeyFrames[prev_key].m_Time;
			const f32 time_between_keys = next_time - prev_time;
			const f32 time_since_prev_key = anim_time - prev_time;
			const f32 t = (time_between_keys > 0) ? (time_since_prev_key / time_between_keys) : (0.0f);

			const Vec3 prev_position = joints.m_TranslationKeyFrames[prev_key].m_position;
			const Vec3 next_position = joints.m_TranslationKeyFrames[next_key].m_position;
			bones[i].m_bone_space_position = prev_position + (next_position - prev_position) * t;

			const Quat4 prev_rotation = joints.m_rotationKeyFrames[prev_key].m_rotation;
			const Quat4 next_rotation = joints.m_rotationKeyFrames[next_key].m_rotation;
			bones[i].m_bone_space_rotation = Quat4::slerp(prev_rotation, next_rotation, t);

			break;
		}
	}
}

/// Model::Animate
void Model::Animate(std::vector<BoneMatrix_t>& outMatrices, const float time, const Animation* const pAnimation, const bool bLoopAnim) {
	std::vector<AnimatedBone_t> tempBones;
	SetBoneMatrices(tempBones, time, pAnimation, bLoopAnim);

	for (int i = 0; i < tempBones.size(); i++) {

		const int parent = m_bones[i].m_ParentIndex;

		BoneMatrix_t matLocalSkel(m_bones[i].m_RelativeRotation, m_bones[i].m_RelativePosition);
		BoneMatrix_t matAnimate(tempBones[i].m_bone_space_rotation, tempBones[i].m_bone_space_position);

		BoneMatrix_t matLocal = matAnimate * matLocalSkel;
		if (parent != 65535) {
			tempBones[i].m_local_space_matrix = matLocal * tempBones[parent].m_local_space_matrix;
		} else {
			tempBones[i].m_local_space_matrix = matLocal;
		}

		const BoneMatrix_t& invRef = GetInvRefBoneMatrix(i);
		outMatrices[i] = invRef * tempBones[i].m_local_space_matrix;
	}
}

/// Model::BlendAnimations
void Model::BlendAnimations(std::vector<BoneMatrix_t>& outMatrices, const Animation* const pFromAnim, const float FromAnimTime, const bool bFromAnimLoops, const Animation* const pToAnim, const float ToAnimTime, const bool bToAnimLoops, const float normalizedBlendTime) {

	std::vector<AnimatedBone_t> fromTempBones;
	SetBoneMatrices(fromTempBones, FromAnimTime, pFromAnim, bFromAnimLoops);

	std::vector<AnimatedBone_t> toTempBones;
	SetBoneMatrices(toTempBones, ToAnimTime, pToAnim, bToAnimLoops);

	for (int i = 0; i < fromTempBones.size(); i++) {

		toTempBones[i].m_bone_space_position = blk::lerp(fromTempBones[i].m_bone_space_position, toTempBones[i].m_bone_space_position, normalizedBlendTime);
		toTempBones[i].m_bone_space_rotation = Quat4::slerp(fromTempBones[i].m_bone_space_rotation, toTempBones[i].m_bone_space_rotation, normalizedBlendTime);

		const int parent = m_bones[i].m_ParentIndex;

		BoneMatrix_t matLocalSkel(m_bones[i].m_RelativeRotation, m_bones[i].m_RelativePosition);
		BoneMatrix_t matAnimate(toTempBones[i].m_bone_space_rotation, toTempBones[i].m_bone_space_position);

		BoneMatrix_t matLocal = matAnimate * matLocalSkel;
		if (parent != 65535) {
			toTempBones[i].m_local_space_matrix = matLocal * toTempBones[parent].m_local_space_matrix;
		} else {
			toTempBones[i].m_local_space_matrix = matLocal;
		}

		const BoneMatrix_t& invRef = GetInvRefBoneMatrix(i);
		outMatrices[i] = invRef * toTempBones[i].m_local_space_matrix;
	}
}

/// Animation::Animation
Animation::Animation() :
	m_LengthInSeconds(0) {
}

/// Animation::load_internal
bool Animation::load_internal() {
	std::ifstream modelFile;
	modelFile.open(blk::os_path(m_full_file_name), std::ifstream::in | std::ifstream::binary);

	if (modelFile.fail()) {
		int numTries = 5;
		while (numTries < 2 && modelFile.fail()) {
			modelFile.close();
			Sleep(2);
			modelFile.open(blk::os_path(m_full_file_name), std::ifstream::in | std::ifstream::binary);
			numTries++;
		}

		if (modelFile.fail()) {
			modelFile.close();
			blk::warn("Model::LoadResource_Internal - Failed to load model %s", m_full_file_name.c_str());
			return false;
		}
	}

	// Find the file size
	modelFile.seekg(0, std::ifstream::end);
	std::streamoff fileSize = modelFile.tellg();
	modelFile.seekg(0, std::ifstream::beg);

	// Load file into memory
	char* const pMemoryFileBuffer = new char[fileSize];
	modelFile.read(pMemoryFileBuffer, fileSize);
	modelFile.close();

	const char* pPtr = pMemoryFileBuffer;

	// Header
	const ms3dHeader_t* pHeader = (const ms3dHeader_t*)pPtr;
	pPtr += sizeof(ms3dHeader_t);

	if (strncmp(pHeader->m_ID, "MS3D000000", 10) != 0) {
		blk::error("Error: Model::LoadResource_Internal - Invalid model header %s", pHeader->m_ID);
	}

	ushort numVertices = *(ushort*)pPtr;
	pPtr += sizeof(ushort);

	pPtr += sizeof(ms3dVertex_t) * numVertices;

	// Triangles
	const uint numTriangles = *(ushort*)pPtr;
	pPtr += sizeof(ushort);
	pPtr += sizeof(ms3dTriangle_t) * numTriangles;

	// Groups
	const uint numGroups = *(ushort*)pPtr;
	pPtr += sizeof(ushort);

	for (uint i = 0; i < numGroups; i++) {

		pPtr += sizeof(u8); // flags
		pPtr += 32;       // name

		const ushort numTriangles = *(ushort*)pPtr;
		pPtr += sizeof(ushort);

		pPtr += sizeof(ushort) * numTriangles;
		pPtr += sizeof(char);
	}

	const uint numMaterials = *(ushort*)pPtr;
	pPtr += sizeof(ushort);
	pPtr += numMaterials * sizeof(ms3dMaterial_t);

	// Joints
	const float AnimationFPS = *(float*)pPtr;
	pPtr += sizeof(float);

	const float CurrentTime = *(float*)pPtr;
	pPtr += sizeof(float);

	const int TotalFrames = *(int*)pPtr;
	pPtr += sizeof(int);

	const ushort numJoints = *(ushort*)pPtr;
	pPtr += sizeof(ushort);

	m_JointKeyFrameData.resize(numJoints);

	for (unsigned i = 0; i < numJoints; i++) {
		const ms3dBone_t* pJoint = (ms3dBone_t*)pPtr;
		pPtr += sizeof(ms3dBone_t);

		const ushort NumTranslationKeyFrames = pJoint->m_NumPositionKeyFrames;
		const ushort NumRotationKeyFrames = pJoint->m_NumRotationKeyFrames;

		const ms3dRotationKeyFrame_t* rotationKeyFrames = (ms3dRotationKeyFrame_t*)pPtr;
		pPtr += sizeof(ms3dPositionKeyFrame_t) * NumRotationKeyFrames;

		const ms3dPositionKeyFrame_t* positionKeyFrames = (ms3dPositionKeyFrame_t*)pPtr;
		pPtr += sizeof(ms3dPositionKeyFrame_t) * NumTranslationKeyFrames;

		Animation::BoneKeyFrames_t& jointData = m_JointKeyFrameData[i];
		jointData.m_rotationKeyFrames.resize(NumRotationKeyFrames);
		jointData.m_TranslationKeyFrames.resize(NumTranslationKeyFrames);

		for (int iKey = 0; iKey < NumRotationKeyFrames; iKey++) {
			const Quat4 rotationX(Vec3::right, rotationKeyFrames[iKey].m_rotation[0]);
			const Quat4 rotationY(Vec3::up, rotationKeyFrames[iKey].m_rotation[1]);
			const Quat4 rotationZ(Vec3::forward, -rotationKeyFrames[iKey].m_rotation[2]);


			jointData.m_rotationKeyFrames[iKey].m_rotation = rotationX * rotationY * rotationZ;
			jointData.m_rotationKeyFrames[iKey].m_Time = rotationKeyFrames[iKey].m_Time;

			if (jointData.m_rotationKeyFrames[iKey].m_Time > m_LengthInSeconds) {
				m_LengthInSeconds = jointData.m_rotationKeyFrames[iKey].m_Time;
			}
		}

		for (int iKey = 0; iKey < NumTranslationKeyFrames; iKey++) {
			jointData.m_TranslationKeyFrames[iKey].m_position.set(positionKeyFrames[iKey].m_position[0], positionKeyFrames[iKey].m_position[1], -positionKeyFrames[iKey].m_position[2]);
			jointData.m_TranslationKeyFrames[iKey].m_Time = positionKeyFrames[iKey].m_Time;

			if (jointData.m_TranslationKeyFrames[iKey].m_Time > m_LengthInSeconds) {
				m_LengthInSeconds = jointData.m_TranslationKeyFrames[iKey].m_Time;
			}
		}
	}

	delete[] pMemoryFileBuffer;

	return true;
}

/// Animation::release_internal
void Animation::release_internal() {
}

BoneMatrix_t operator*(const BoneMatrix_t& op1, const BoneMatrix_t& op2) {
	BoneMatrix_t returnMatrix;

	returnMatrix.m_Axis[0].x = op1.m_Axis[0].x * op2.m_Axis[0].x + op1.m_Axis[0].y * op2.m_Axis[1].x + op1.m_Axis[0].z * op2.m_Axis[2].x;
	returnMatrix.m_Axis[1].x = op1.m_Axis[1].x * op2.m_Axis[0].x + op1.m_Axis[1].y * op2.m_Axis[1].x + op1.m_Axis[1].z * op2.m_Axis[2].x;
	returnMatrix.m_Axis[2].x = op1.m_Axis[2].x * op2.m_Axis[0].x + op1.m_Axis[2].y * op2.m_Axis[1].x + op1.m_Axis[2].z * op2.m_Axis[2].x;
	returnMatrix.m_Axis[3].x = op1.m_Axis[3].x * op2.m_Axis[0].x + op1.m_Axis[3].y * op2.m_Axis[1].x + op1.m_Axis[3].z * op2.m_Axis[2].x + op2.m_Axis[3].x;

	returnMatrix.m_Axis[0].y = op1.m_Axis[0].x * op2.m_Axis[0].y + op1.m_Axis[0].y * op2.m_Axis[1].y + op1.m_Axis[0].z * op2.m_Axis[2].y;
	returnMatrix.m_Axis[1].y = op1.m_Axis[1].x * op2.m_Axis[0].y + op1.m_Axis[1].y * op2.m_Axis[1].y + op1.m_Axis[1].z * op2.m_Axis[2].y;
	returnMatrix.m_Axis[2].y = op1.m_Axis[2].x * op2.m_Axis[0].y + op1.m_Axis[2].y * op2.m_Axis[1].y + op1.m_Axis[2].z * op2.m_Axis[2].y;
	returnMatrix.m_Axis[3].y = op1.m_Axis[3].x * op2.m_Axis[0].y + op1.m_Axis[3].y * op2.m_Axis[1].y + op1.m_Axis[3].z * op2.m_Axis[2].y + op2.m_Axis[3].y;

	returnMatrix.m_Axis[0].z = op1.m_Axis[0].x * op2.m_Axis[0].z + op1.m_Axis[0].y * op2.m_Axis[1].z + op1.m_Axis[0].z * op2.m_Axis[2].z;
	returnMatrix.m_Axis[1].z = op1.m_Axis[1].x * op2.m_Axis[0].z + op1.m_Axis[1].y * op2.m_Axis[1].z + op1.m_Axis[1].z * op2.m_Axis[2].z;
	returnMatrix.m_Axis[2].z = op1.m_Axis[2].x * op2.m_Axis[0].z + op1.m_Axis[2].y * op2.m_Axis[1].z + op1.m_Axis[2].z * op2.m_Axis[2].z;
	returnMatrix.m_Axis[3].z = op1.m_Axis[3].x * op2.m_Axis[0].z + op1.m_Axis[3].y * op2.m_Axis[1].z + op1.m_Axis[3].z * op2.m_Axis[2].z + op2.m_Axis[3].z;
	return returnMatrix;
}

void Model::DrawDebugTBN(const Vec3& modelTranslation, const Quat4& modelRotation, const Vec3& scale) {
	Mat4 modelMatrix;
	modelMatrix.make_scale(scale);
	modelMatrix *= modelRotation.to_mat4();
	modelMatrix[3].set(modelTranslation.x, modelTranslation.y, modelTranslation.z, 1.0f);

	for (int i = 0; i < m_DebugPositions.size(); i++) {
		const Vec3 worldPos = modelMatrix.transform_point(m_DebugPositions[i]);
		const Vec3 worldNormal = m_DebugNormals[i] * modelMatrix;
		const Vec3 worldTangent = m_DebugTangents[i] * modelMatrix;
		const Vec3 worldBitangent = worldNormal.cross(worldTangent).normalize_safe();

		/*g_pRenderer->DrawLine(worldPos, worldPos + worldTangent * 3.0f, Color::red);
		g_pRenderer->DrawLine(worldPos, worldPos + worldBitangent * 3.0f, Color::green);
		g_pRenderer->DrawLine(worldPos, worldPos + worldNormal * 3.0f, Color::blue);*/
	}
}
