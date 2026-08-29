/// Plane3d.cpp
///
/// 2017 blk

#include "blk_core.h"
#include "plane3d.h"

/// Plane3d::Intesect
Plane3d_Intersect Plane3d::Intersect(const Vec3& startPt, const Vec3& endPt, float& t, Vec3& intersectionPt) {
	Vec3 planeNormal(x, y, z);
	Vec3 knownPt = planeNormal * w;
	Vec3 vToStartPt = startPt - knownPt;
	Vec3 vToEndPt = endPt - knownPt;

	int side = 0;

	float startDot = vToStartPt.dot(planeNormal);
	float endDot = vToEndPt.dot(planeNormal);

	if (startDot > 0 && endDot > 0) {
		return PLANE_BOTH_IN;
	}

	if (startDot <= 0 && endDot <= 0) {
		return PLANE_BOTH_OUT;
	}

	if (startDot == 0 && endDot == 0) {
		return PLANE_BOTH_OUT;	// for now
	}

	Vec3 vecTo = endPt - startPt;
	vecTo.normalize_self();

	float denominator = planeNormal.dot(vecTo);

	//	if (denominator == 0)	
	//		return 0;

	float numerator = knownPt.dot(planeNormal) - startPt.dot(planeNormal);
	t = numerator / denominator;
	intersectionPt = startPt + vecTo * t;

	if (startDot > 0) {
		return PLANE_FIRSTVERT_IN;
	} else {
		return PLANE_SECONDVERT_IN;
	}
}

/// Plane3d::intersects_plane
bool Plane3d::intersects_plane(Vec3& out_point, Vec3& out_direction, const Plane3d& other_plane) const {
	// Compute line direction, perpendicular to both plane normals.
	const Plane3d& op1 = *this;
	out_direction = op1.cross(other_plane);

	const f32 epsilon = 0.000001f;
	const f32 dir_sqr = out_direction.length_sqr();
	if (dir_sqr < epsilon) {
		return false;
	} else {
		// Compute intersection.
		out_point = ((other_plane.cross(out_direction)) * op1.w + (out_direction.cross(op1)) * other_plane.w) / dir_sqr;
		out_direction.normalize_self();
		return true;
	}
}

/// Plane3d::dot_with_vec
f32 Plane3d::dot_with_vec(const Vec3& Vec) {
	return (x * Vec.x) + (y * Vec.y) + (z * Vec.z) - w;
}