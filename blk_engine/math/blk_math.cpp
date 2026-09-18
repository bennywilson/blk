/// blk_math.cpp
///
/// 2016 blk

// `rand`/`RAND_MAX`. Implicit on MSVC, where windows.h pulls it in transitively.
#include <cstdlib>
#include "blk_math.h"
#include "Matrix.h"

int blk::irand(const int min, const int max) {
	return min + rand() % (max - min);
}

// frand
f32 blk::frand(const f32 min, const f32 max) {
	const f32 rand_val = rand() / (f32)RAND_MAX;
	return min + rand_val * (max - min);
}

Vec2 Vec2Rand(const Vec2& min, const Vec2& max) {
	Vec2 randVec;
	randVec.x = min.x + (blk::frand() * (max.x - min.x));
	randVec.y = min.y + (blk::frand() * (max.y - min.y));

	return randVec;
}

Vec3 Vec3Rand(const Vec3& min, const Vec3& max) {
	Vec3 randVec;
	randVec.x = min.x + (blk::frand() * (max.x - min.x));
	randVec.y = min.y + (blk::frand() * (max.y - min.y));
	randVec.z = min.z + (blk::frand() * (max.z - min.z));

	return randVec;
}

Vec4 Vec4Rand(const Vec4& min, const Vec4& max) {
	Vec4 randVec;
	randVec.x = min.x + (blk::frand() * (max.x - min.x));
	randVec.y = min.y + (blk::frand() * (max.y - min.y));
	randVec.z = min.z + (blk::frand() * (max.z - min.z));
	randVec.w = min.w + (blk::frand() * (max.w - min.w));
	return randVec;
}
