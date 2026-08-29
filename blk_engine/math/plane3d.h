/// Plane3d.h
///
/// 2017 blk

#pragma once

#include "matrix.h"

enum Plane3d_Intersect {
	PLANE_BOTH_IN,
	PLANE_BOTH_OUT,
	PLANE_BOTH_ON,
	PLANE_FIRSTVERT_IN,
	PLANE_SECONDVERT_IN,
};

/// Plane3d
class Plane3d : public Vec3 {
public:
	Plane3d() : w(1.f) {}
	Plane3d(const f32 inX, const f32 inY, const f32 inZ, const f32 inW) { x = inX, y = inY, z = inZ, w = inW; }
	Plane3d(const Vec3& Normal, const f32 W) { x = Normal.x, y = Normal.y, z = Normal.z, w = W; }
	Plane3d(const Vec3& Point, const Vec3& Normal) {
		x = Normal.x;
		y = Normal.y;
		z = Normal.z;
		w = Point.dot(Normal);
	}

	Plane3d_Intersect Intersect(const Vec3& startPt, const Vec3& endPt, f32& t, Vec3& intersectionPt);

	bool intersects_plane(Vec3& out_point, Vec3& out_direction, const Plane3d& other_plane) const;

	f32 dot_with_vec(const Vec3& Vec);

	f32 w;
};
