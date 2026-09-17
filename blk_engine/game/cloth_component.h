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
	int m_MassIndices[2];
	float m_Length;
};

/// ClothMass_t
struct ClothMass_t {
	ClothMass_t() :
		m_LastPosition(Vec3::zero), m_FrameForces(Vec3::zero), m_bAnchored(false) {}

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
	BLK_PROPERTY()
	String m_BoneName;

	BLK_PROPERTY()
	std::vector<String> m_NeighborBones;

	BLK_PROPERTY()
	bool m_bIsAnchored;
};

/// ClothComponent
class ClothComponent : public GameComponent {
public:
	BLK_DECLARE_COMPONENT(ClothComponent, GameComponent);

	virtual ~ClothComponent();

	const std::vector<class ClothBone>& GetBoneInfo() const { return m_BoneInfo; }
	const std::vector<class ClothBone>& GetAdditionalBoneInfo() const { return m_AdditionalBoneInfo; }
	const std::vector<ClothMass_t>& GetMasses() const { return m_Masses; }
	const std::vector<ClothSpring_t>& GetSprings() const { return m_Springs; }

	void AddForceToMass(const int massIdx, const Vec3& force) { m_Masses[massIdx].m_FrameForces += force; }

	void SetClothCollisionSphere(const int idx, const Vec4& sphere);

protected:

	virtual void RunSimulation(const float DeltaTime);

private:

	virtual void update_internal(const float DeltaTime) override;

	void SetupCloth();

	BLK_PROPERTY()
	int m_Width;

	BLK_PROPERTY()
	int m_Height;
	int m_CurrentTickFrame;

	BLK_PROPERTY()
	EClothType m_ClothType;

	BLK_PROPERTY()
	std::vector<class ClothBone> m_BoneInfo;

	BLK_PROPERTY()
	std::vector<class ClothBone> m_AdditionalBoneInfo;

	BLK_PROPERTY()
	std::vector<class BoneCollisionSphere> m_CollisionSpheres;

	BLK_PROPERTY(MinVal = 1)
	int m_NumConstrainIterations;

	BLK_PROPERTY()
	Vec3 m_gravity;

	// Wind Data
	BLK_PROPERTY()
	Vec3 m_MaxWindVelocity;

	BLK_PROPERTY()
	Vec3 m_MinWindVelocity;

	BLK_PROPERTY()
	float m_MaxWindGustDuration;

	BLK_PROPERTY()
	float m_MinWindGustDuration;

	BLK_PROPERTY()
	bool m_bAddFakeOscillation;

	// Run-time
	Vec3 m_CurWindVelocity;
	Vec3 m_NextWindVelocity;
	float m_NextWindChangeTime;

	const Model* m_pSkeletalModel;

	std::vector<int> m_BoneIndices;
	std::vector<ClothMass_t> m_Masses;
	std::vector<ClothSpring_t> m_Springs;
};
