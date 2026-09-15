/// cloth_component.h
///
/// 2016 blk

#pragma once

#include "component.h"
#include "render_defs.h"

/// EClothType
enum EClothType {
	CT_None,
	CT_Square,
};

/// ClothSpring_t
struct ClothSpring_t {
	int	m_MassIndices[2];
	float m_Length;
};

/// ClothMass_t
struct ClothMass_t {
	ClothMass_t() : m_LastPosition(Vec3::zero), m_FrameForces(Vec3::zero), m_bAnchored(false) { }

	const Vec3& position() const { return m_Matrix.GetOrigin(); }
	const Vec3& GetAxis(const int index) const { return m_Matrix.GetAxis(index); }

	void set_position(const Vec3 newOrigin) { m_Matrix.SetAxis(3, newOrigin); }
	void SetAxis(const int index, const Vec3& axis) { m_Matrix.SetAxis(index, axis); }

	BoneMatrix_t m_Matrix;
	Vec3 m_LastPosition;
	Vec3 m_FrameForces;
	bool m_bAnchored;
};

/// ClothBone
class ClothBone : public GameComponent {
public:
	friend class ClothComponent;

	BLK_DECLARE_COMPONENT(ClothBone, GameComponent);

private:
	String									m_BoneName;
	std::vector<String>						m_NeighborBones;
	bool										m_bIsAnchored;
};

/// ClothComponent
class ClothComponent : public GameComponent {
public:
	BLK_DECLARE_COMPONENT(ClothComponent, GameComponent);

	virtual										~ClothComponent();

	const std::vector<class ClothBone>& GetBoneInfo() const { return m_BoneInfo; }
	const std::vector<class ClothBone>& GetAdditionalBoneInfo() const { return m_AdditionalBoneInfo; }
	const std::vector<ClothMass_t>& GetMasses() const { return m_Masses; }
	const std::vector<ClothSpring_t>& GetSprings() const { return m_Springs; }

	void										AddForceToMass(const int massIdx, const Vec3& force) { m_Masses[massIdx].m_FrameForces += force; }

	void										SetClothCollisionSphere(const int idx, const Vec4& sphere);

protected:

	virtual void								RunSimulation(const float DeltaTime);

private:

	virtual void								update_internal(const float DeltaTime) override;

	void										SetupCloth();

	int											m_Width;
	int											m_Height;
	int											m_CurrentTickFrame;
	EClothType									m_ClothType;
	std::vector<class ClothBone>				m_BoneInfo;
	std::vector<class ClothBone>				m_AdditionalBoneInfo;
	std::vector<class BoneCollisionSphere>	m_CollisionSpheres;
	int											m_NumConstrainIterations;

	Vec3										m_gravity;

	// Wind Data
	Vec3										m_MaxWindVelocity;
	Vec3										m_MinWindVelocity;
	float										m_MaxWindGustDuration;
	float										m_MinWindGustDuration;
	bool										m_bAddFakeOscillation;

	// Run-time
	Vec3										m_CurWindVelocity;
	Vec3										m_NextWindVelocity;
	float										m_NextWindChangeTime;

	const Model* m_pSkeletalModel;

	std::vector<int>							m_BoneIndices;
	std::vector<ClothMass_t>					m_Masses;
	std::vector<ClothSpring_t>				m_Springs;
};
