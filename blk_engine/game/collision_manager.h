/// collision_manager.h
///
/// 2016 blk

#pragma once


/// CollisionComponent
enum ECollisionType {
	CollisionType_Sphere,
	CollisionType_Box,
	CollisionType_StaticMesh,
	CollisionType_CustomTriangles
};

/// BoneCollisionSphere
class BoneCollisionSphere : public GameComponent {

	BLK_DECLARE_COMPONENT(BoneCollisionSphere, GameComponent);
	friend class ClothComponent;

//---------------------------------------------------------------------------------------------------
public:
	const String& GetBoneName() const { return m_BoneName; }
	const Vec4& GetSphere() const { return m_Sphere; }

private:
	String m_BoneName;
	Vec4 m_Sphere;
};


 /// CollisionComponent
class CollisionComponent : public GameComponent {
	BLK_DECLARE_COMPONENT(CollisionComponent, GameComponent);
	friend class CollisionManager;

public:
	virtual ~CollisionComponent();

	const std::vector<BoneCollisionSphere>& GetLocalSpaceCollisionSpheres() const { return m_LocalSpaceCollisionSpheres; }
	const std::vector<Vec4>& GetWorldSpaceCollisionSpheres() const { return m_WorldSpaceCollisionSpheres; }
	void SetWorldSpaceCollisionSphere(const int idx, const Vec4& newSphere);

	float GetRadius() const { return m_Extent.length(); }

	struct customTriangle_t {
		Vec3 m_Vertex1;
		Vec3 m_Vertex2;
		Vec3 m_Vertex3;
	};
	void SetCustomTriangleCollision(const std::vector<customTriangle_t>& inCollision);


protected:
	virtual void enable_internal(const bool isEnabled) override;
	virtual void update_internal(const float DeltaTime) override;

private:
	ECollisionType m_CollisionType;
	Vec3 m_Extent;

	std::vector<Vec4> m_WorldSpaceCollisionSpheres;
	std::vector<BoneCollisionSphere> m_LocalSpaceCollisionSpheres;
	std::vector<customTriangle_t> m_CustomTriangleCollision;
};

/// CollisionInfo_t
struct CollisionInfo_t {
	CollisionInfo_t() :
		m_T(FLT_MAX),
		m_pHitComponent(nullptr),
		m_bHit(false) {}

	Vec3 m_HitLocation;
	float m_T;
	GameComponent* m_pHitComponent;
	bool m_bHit;
};

/// CollisionManager
class CollisionManager {

//---------------------------------------------------------------------------------------------------
public:
	CollisionManager();
	~CollisionManager();

	CollisionInfo_t PerformLineCheck(const Vec3& start, const Vec3& end);

	void RegisterComponent(CollisionComponent* Collision);
	void UnregisterComponent(CollisionComponent* Collision);

private:
	std::vector<CollisionComponent*> m_CollisionComponents;
};

extern CollisionManager g_CollisionManager;