/// blk_math
///
/// 2016 blk

#pragma once

#include <cmath>

typedef float f32;

float SeededNoise(const float x, const float y);

float SmoothNoise(const float x, const float y);

float InterpolatedNoise(const float x, const float y);

float NormalizedNoise(const float x, const float y);

namespace blk {
	const float PI = 3.14159265359f;
	const float EPSILON = 0.00001f;
	inline constexpr f32 to_radians(const float degrees) { return degrees * PI / 180.0f; }
	inline float to_degrees(const float radians) { return radians * 180.0f / PI; }

	inline bool compare_byte4(const unsigned char lhs[4], const unsigned char rhs[4]) { return lhs[0] == rhs[0] && lhs[1] == rhs[1] && lhs[2] == rhs[2] && lhs[3] == rhs[3]; }

	template<typename T> T clamp(const T& value, const T& min, const T& max) { return value < min ? min : (value > max ? max : value); }
	template<typename T> T saturate(const T& value) { return value < 0 ? 0 : (value > 1 ? 1 : value); }

	template<typename T> inline T lerp(const T a, const T b, const float t) { return ((b - a) * t) + a; }

	inline int irand(const int min, const int max);

	float frand(const float min = 0.f, const float max = 1.f);
}

class Vec2;
class Vec3;
class Vec4;

Vec2 Vec2Rand(const Vec2& min, const Vec2& max);
Vec3 Vec3Rand(const Vec3& min, const Vec3& max);
Vec4 Vec4Rand(const Vec4& min, const Vec4& max);

template<typename T> T min3(const T& a, const T& b, const T& c) {
	if (a <= b && a <= c) {
		return a;
	} else if (b <= a && b <= c) {
		return b;
	}
	return c;
}

template<typename T> T max3(const T& a, const T& b, const T& c) {
	if (a >= b && a >= c) {
		return a;
	} else if (b >= a && b >= c) {
		return b;
	}
	return c;
}