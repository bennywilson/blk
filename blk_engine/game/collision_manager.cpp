/// collision_manager.cpp
///
/// 2016 blk

#include "blk_core.h"
#include "blk_containers.h"
#include "Matrix.h"
#include "Quaternion.h"
#include "entity_header.h"
#include "collision_manager.h"
#include "intersection_tests.h"
#include "blk_console.h"

BLK_DEFINE_COMPONENT(CollisionComponent)

CollisionManager g_CollisionManager;

ConsoleVariable g_ShowCollision("showcollision", false, ConsoleVariable::Console_Bool, "Show collision", "");

/// CollisionComponent::Constructor
void CollisionComponent::Constructor() {
	m_CollisionType = CollisionType_Sphere;
	m_Extent.set(10.0f, 10.0f, 10.0f);
}

/// CollisionComponent::~CollisionComponent
CollisionComponent::~CollisionComponent() {
}

/// CollisionComponent::enable_internal
void CollisionComponent::enable_internal(const bool isEnabled) {
	if (isEnabled) {
		g_CollisionManager.RegisterComponent(this);
	} else {
		g_CollisionManager.UnregisterComponent(this);
	}
}

/// CollisionComponent::update_internal
void CollisionComponent::update_internal(const float DeltaTime) {
	Super::update_internal(DeltaTime);

	/*if (g_ShowCollision.GetBool()) {
		const Vec3 collisionCenter = GetOwner()->position();//, pCollision->m_Extent.x 
		if (m_CollisionType == ECollisionType::CollisionType_Sphere) {
			g_pRenderer->DrawSphere(collisionCenter, m_Extent.x, 12, Color::green);
		} else if (m_CollisionType == ECollisionType::CollisionType_Box) {
			g_pRenderer->DrawBox(Bounds(collisionCenter - m_Extent, collisionCenter + m_Extent), Color::green);
		}
	}*/
}

/// CollisionComponent::SetWorldSpaceCollisionSphere
void CollisionComponent::SetWorldSpaceCollisionSphere(const int idx, const Vec4& newSphere) {

	if (idx < 0 || idx >= m_LocalSpaceCollisionSpheres.size()) {
		blk::error("CollisionComponent::SetWorldSpaceCollisionSphere() - Invalid idx %d provided", idx);
		return;
	}

	if (m_WorldSpaceCollisionSpheres.size() == 0) {
		m_WorldSpaceCollisionSpheres.resize(m_LocalSpaceCollisionSpheres.size());
	}

	m_WorldSpaceCollisionSpheres[idx] = newSphere;
}

/// CollisionComponent::SetCustomTriangleCollision
void CollisionComponent::SetCustomTriangleCollision(const std::vector<customTriangle_t>& inCollision) {

	if (IsEnabled()) {
		g_CollisionManager.UnregisterComponent(this);
	}

	m_CollisionType = CollisionType_CustomTriangles;
	m_CustomTriangleCollision = inCollision;

	if (IsEnabled()) {
		g_CollisionManager.RegisterComponent(this);
	}
}

/// CollisionManager::CollisionManager
CollisionManager::CollisionManager() {
}

/// CollisionManager::~CollisionManager
CollisionManager::~CollisionManager() {
	blk::error_check(m_CollisionComponents.size() == 0, "CollisionManager::~CollisionManager() - There are still %d registered components", (int)m_CollisionComponents.size());
}

/// CollisionManager::PerformLineCheck
CollisionInfo_t CollisionManager::PerformLineCheck(const Vec3& start, const Vec3& end) {
	CollisionInfo_t collisionInfo;

	float LineLength = 0.0f;

	if (end.compare(start) == false) {
		LineLength = (end - start).length();
	} else {
		// todo: Point check
		return collisionInfo;
	}

	const float oneOverLength = 1.0f / LineLength;
	const Vec3 rayDir = (end - start) * oneOverLength;

	for (int iCollisionComp = 0; iCollisionComp < m_CollisionComponents.size(); iCollisionComp++) {
		CollisionComponent* const pCollision = m_CollisionComponents[iCollisionComp];

		if (pCollision->m_CollisionType == CollisionType_CustomTriangles) {

			bool bHit = false;
			const std::vector<CollisionComponent::customTriangle_t>& triList = pCollision->m_CustomTriangleCollision;
			for (int iTri = 0; iTri < triList.size(); iTri++) {

				const Vec3& v1 = triList[iTri].m_Vertex1;
				const Vec3& v2 = triList[iTri].m_Vertex2;
				const Vec3& v3 = triList[iTri].m_Vertex3;
				float t;
				if (RayTriIntersection(t, start, rayDir, v1, v2, v3)) {
					if (t < collisionInfo.m_T && t >= 0 && t < LineLength) {
						collisionInfo.m_T = t;
						bHit = true;
					}
				}
			}

			if (bHit) {
				collisionInfo.m_HitLocation = start + rayDir * collisionInfo.m_T;
				collisionInfo.m_pHitComponent = pCollision;
				collisionInfo.m_bHit = true;
			}

		} else if (pCollision->m_CollisionType == CollisionType_StaticMesh) {
			GameEntity* const pOwner = pCollision->GetOwner();
			StaticModelComponent* const pStaticModel = (StaticModelComponent*)pOwner->GetComponentByType(StaticModelComponent::GetType());
			if (pStaticModel == nullptr) {
				blk::warn("CollisionManager::PerformLineCheck() - Entity %s is missing a RenderComponent", pOwner->name().c_str());
				continue;
			}
			ModelIntersection_t intersection = pStaticModel->model()->RayIntersection(start, rayDir, pOwner->position(), pOwner->rotation(), Vec3::one);
			if (intersection.hasIntersection && intersection.t < LineLength && intersection.t < collisionInfo.m_T) {
				collisionInfo.m_bHit = true;
				collisionInfo.m_HitLocation = start + rayDir * intersection.t;
				collisionInfo.m_T = intersection.t;
				collisionInfo.m_pHitComponent = pCollision;
			}
		} else if (pCollision->m_CollisionType == CollisionType_Sphere) {
			GameEntity* const collision_owner = pCollision->GetOwner();
			Vec3 intersectionPt;
			if (RaySphereIntersection(intersectionPt, start, rayDir, collision_owner->position(), pCollision->m_Extent.x)) {
				const float t = (intersectionPt - start).length() / LineLength;
				if (t < collisionInfo.m_T) {

					if (pCollision->GetWorldSpaceCollisionSpheres().size() == 0) {
						collisionInfo.m_bHit = true;
						collisionInfo.m_HitLocation = intersectionPt;
						collisionInfo.m_T = t;
						collisionInfo.m_pHitComponent = pCollision;
					} else {
						for (int iColSphere = 0; iColSphere < pCollision->GetWorldSpaceCollisionSpheres().size(); iColSphere++) {
							const Vec4& curSphere = pCollision->GetWorldSpaceCollisionSpheres()[iColSphere];
							if (RaySphereIntersection(intersectionPt, start, rayDir, curSphere.ToVec3(), curSphere.a)) {
								const float innerT = (intersectionPt - start).length() / LineLength;
								if (innerT < collisionInfo.m_T) {
									collisionInfo.m_bHit = true;
									collisionInfo.m_HitLocation = intersectionPt;
									collisionInfo.m_T = innerT;
									collisionInfo.m_pHitComponent = pCollision;
								}
							}
						}
					}
				}
			}
		}
	}

	return collisionInfo;
}

/// CollisionManager::RegisterComponent
void CollisionManager::RegisterComponent(CollisionComponent* Collision) {
	if (std::find(m_CollisionComponents.begin(), m_CollisionComponents.end(), Collision) == m_CollisionComponents.end()) {
		m_CollisionComponents.push_back(Collision);
	}
}

/// CollisionManager::UnregisterComponent
void CollisionManager::UnregisterComponent(CollisionComponent* Collision) {
	blk::std_remove_swap(m_CollisionComponents, Collision);
}