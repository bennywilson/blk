//===================================================================================================
// intersection_tests.h
//
//
// 2016 blk
//===================================================================================================
#pragma once

class Vec3;
class Bounds;

bool RayOBBIntersection(const Mat4& orientation, const Vec3& origin, const Vec3& start, const Vec3& end, const Vec3& min, const Vec3& max);
bool RayAABBIntersection(const Vec3& origin, const Vec3& direction, const Bounds& box);
bool RayAABBIntersection(float& outT, const Vec3& origin, const Vec3& direction, const Bounds& box);
bool RayTriIntersection(float& outT, const Vec3& rayOrigin, const Vec3& rayDirection, const Vec3& v0, const Vec3& v1, const Vec3& v2);

bool RaySphereIntersection(Vec3& outIntersectionPt, const Vec3& rayOrigin, const Vec3& rayDirection, const Vec3& sphereOrigin, const float sphereRadius);