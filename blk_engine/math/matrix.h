/// Matrix.h
///
/// 2016 blk

#pragma once

#include "blk_math.h"

/// Vec2i
class Vec2i {
public:
	Vec2i() :
		x(0),
		y(0) {}

	Vec2i(const int inX, const int inY) :
		x(inX),
		y(inY) {}

	void set(const int inX, const int inY) {
		x = inX;
		y = inY;
	}

	int x, y;
};

///  Vec2
class Vec2 {
public:
	Vec2() :
		x(0.f),
		y(0.f) {}

	Vec2(const float initX, const float initY) :
		x(initX),
		y(initY) {}

	void set(const float initX, const float initY) {
		x = initX;
		y = initY;
	}

	Vec2 operator +(const Vec2& rhs) const {
		return Vec2(x + rhs.x, y + rhs.y);
	}

	void operator +=(const Vec2& rhs) {
		x += rhs.x;
		y += rhs.y;
	}

	Vec2 operator -(const Vec2& rhs) const {
		return Vec2(x - rhs.x, y - rhs.y);
	}

	void operator -=(const Vec2& rhs) {
		x -= rhs.x, y -= rhs.y;
	}

	void operator *=(const float rhs) {
		x *= rhs, y *= rhs;
	}

	Vec2 operator -() const {
		return Vec2(-x, -y);
	}

	void operator /=(const float rhs) {
		x /= rhs;
		y /= rhs;
	}

	bool compare(const Vec2& op2, const float epsilon = 0.0001f) const {
		return fabs(x - op2.x) < epsilon && fabs(y - op2.y) < epsilon;
	}

	const float operator[](const int index) const { return (&x)[index]; }
	float& operator[](const int index) { return (&x)[index]; }
	float* ptr() const { return (float*)this; }

	void rotate(const float angle) {
		const float rad = angle * (kbPI / 180.f);
		const float cosAngle = cos(rad);
		const float sinAngle = sin(rad);

		float newX = (x * cosAngle) + (y * sinAngle);
		float newY = (x * -sinAngle) + (y * cosAngle);

		x = newX;
		y = newY;
		normalize_self();
	}

	float length() const {
		return sqrt(x * x + y * y);
	}

	float length_sqr() const {
		return (x * x + y * y);
	}

	void normalize_self() {
		float len = length();
		float invLength = 1.f / len;
		x *= invLength;
		y *= invLength;
	}

	float x;
	float y;

	static const Vec2 zero;
	static const Vec2 one;
};

Vec2 operator *(const Vec2& op1, const float op2);
Vec2 operator +(const Vec2& op1, const float op2);
Vec2 operator /(const Vec2& op1, const float op2);

/// Vec3i
class Vec3i {
public:
	Vec3i(const int inX, const int inY, const int inZ) : x(inX), y(inY), z(inZ) { }

	int x;
	int y;
	int z;
};

/// Vec3
class Vec3 {
public:
	Vec3() :
		x(0.f),
		y(0.f),
		z(0.f) {}

	Vec3(const float in_x, const float in_y, const float in_z) :
		x(in_x),
		y(in_y),
		z(in_z) {}

	void set(const float in_x, const float in_y, const float in_z) {
		x = in_x;
		y = in_y;
		z = in_z;
	}

	Vec3 operator +(const Vec3& rhs) const {
		return Vec3(x + rhs.x, y + rhs.y, z + rhs.z);
	}

	void operator +=(const Vec3& rhs) {
		x += rhs.x;
		y += rhs.y;
		z += rhs.z;
	}

	Vec3 operator +(const float rhs) const {
		return Vec3(x + rhs, y + rhs, z + rhs);
	}

	Vec3 operator -(const Vec3& rhs) const {
		return Vec3(x - rhs.x, y - rhs.y, z - rhs.z);
	}

	void operator -=(const Vec3& rhs) {
		x -= rhs.x;
		y -= rhs.y;
		z -= rhs.z;
	}

	Vec3 operator -(const float rhs) const {
		return Vec3(x - rhs, y - rhs, z - rhs);
	}

	Vec3 operator -() const { return Vec3(-x, -y, -z); }

	Vec3 operator *(const class Mat4&) const;

	Vec3 operator *(const float op2) const {
		return Vec3(x * op2, y * op2, z * op2);
	}

	Vec3 operator *(const Vec3& op2) const {
		return Vec3(x * op2.x, y * op2.y, z * op2.z);
	}

	void operator *=(const float op) {
		x *= op;
		y *= op;
		z *= op;
	}

	void operator /=(const float rhs) {
		x /= rhs;
		y /= rhs;
		z /= rhs;
	}

	Vec3 operator /(const float rhs) const {
		return Vec3(x / rhs, y / rhs, z / rhs);
	};

	Vec3 operator / (const Vec3& rhs) const {
		return Vec3(x / rhs.x, y / rhs.y, z / rhs.z);
	}

	bool compare(const Vec3& op2, float epsilon = 0.0001f) const {
		return fabs(x - op2.x) < epsilon && fabs(y - op2.y) < epsilon && fabs(z - op2.z) < epsilon;
	}

	float dot(const Vec3& rhs) const {
		return x * rhs.x + y * rhs.y + z * rhs.z;
	}

	Vec3 cross(const Vec3& op2) const {
		return Vec3(
			(z * op2.y) - (y * op2.z),
			(x * op2.z) - (z * op2.x),
			(y * op2.x) - (x * op2.y)
		);
	}

	void multiply_components(const Vec3& op2) {
		x *= op2.x;
		y *= op2.y;
		z *= op2.z;
	}

	float length() const {
		return sqrt(length_sqr());
	}

	float length_sqr() const {
		return (x * x + y * y + z * z);
	}

	float normalize_self() {
		float len = length();
		float invLength = 1.f / len;
		x *= invLength;
		y *= invLength;
		z *= invLength;
		return len;
	}

	Vec3 normalize_safe() const {
		Vec3 returnVec = *this;
		returnVec.normalize_self();
		return returnVec;
	}

	class Vec4 extend(f32 w) const;

	const float operator[](const int index) const { return (&x)[index]; }
	float& operator[](const int index) { return (&x)[index]; }
	float* ptr() const { return (float*)this; }

	float x, y, z;

public:
	static const Vec3 zero;
	static const Vec3 right;
	static const Vec3 up;
	static const Vec3 down;
	static const Vec3 forward;
	static const Vec3 one;
};

Vec3 operator *(const float op1, const Vec3& op2);

/// Vec4
class Vec4 {
public:
	Vec4() { }

	Vec4(const Vec3& inVec) :
		x(inVec.x),
		y(inVec.y),
		z(inVec.z),
		w(1.f) {}

	Vec4(const Vec3& inVec, float inW) :
		x(inVec.x),
		y(inVec.y),
		z(inVec.z),
		w(inW) {}

	Vec4(const float initX, const float initY, const float initZ, const float initW) :
		x(initX),
		y(initY),
		z(initZ),
		w(initW) {}

	void set(const float inX, const float inY, const float inZ, const float inW) {
		x = inX;
		y = inY;
		z = inZ;
		w = inW;
	}

	Vec4 operator +(const Vec4& op2) const {
		return Vec4(x + op2.x, y + op2.y, z + op2.z, w + op2.w);
	}

	void operator +=(const Vec4& op2) {
		x += op2.x;
		y += op2.y;
		z += op2.z;w += op2.w;
	}
	Vec4 operator -(const Vec4& rhs) const {
		return Vec4(x - rhs.x, y - rhs.y, z - rhs.z, w - rhs.w);
	}

	Vec4 operator *(const Vec4& op2) const {
		return Vec4(x * op2.x, y * op2.y, z * op2.z, 1.f);
	}

	Vec4 operator *(const float op2) const {
		return Vec4(x * op2, y * op2, z * op2, w * op2);
	}
	void operator *=(const float op2) {
		x *= op2; y *= op2, z *= op2, w *= op2;
	}

	Vec4 transform_point(const class Mat4& op2, bool bDivideByW = false) const;

	Vec4 operator /(const float op2) const {
		return Vec4(x / op2, y / op2, z / op2, w / op2);
	}

	Vec4& saturate() {
		if (x < 0.f) {
			x = 0.f;
		} else if (x > 1.f) {
			x = 1.f;
		}
		if (y < 0.f) {
			y = 0.f;
		} else if (y > 1.f) {
			y = 1.f;
		}
		if (z < 0.f) {
			z = 0.f;
		} else if (z > 1.f) {
			z = 1.f;
		}
		return *this;
	}

	void operator /=(const float op2) {
		x /= op2; y /= op2, z /= op2, w /= op2;
	}

	Vec4 operator -() const {
		return Vec4(-x, -y, -z, -w);
	}

	const float operator[](const int index) const { return (&x)[index]; }
	float& operator[](const int index) { return (&x)[index]; }

	const Vec3& ToVec3() const { return *(Vec3*)this; }
	Vec3& ToVec3() { return *(Vec3*)this; }

	union {
		struct {
			float x, y, z, w;
		};

		struct {
			float r, g, b, a;
		};
	};

public:
	static const Vec4 right;
	static const Vec4 up;
	static const Vec4 forward;
	static const Vec4 zero;
};

Vec4 operator *(const f32 op1, const Vec4& op2);

///  kbColor
class kbColor : public Vec4 {
public:
	kbColor() { }
	kbColor(const f32 inX, const f32 inY, const f32 inZ, const f32 inW) :
		Vec4(inX, inY, inZ, inW) { }

	kbColor(const Vec4& inVec) :
		Vec4(inVec.r, inVec.g, inVec.b, inVec.a) { }

	const static kbColor red;
	const static kbColor green;
	const static kbColor blue;
	const static kbColor yellow;
	const static kbColor white;
	const static kbColor black;
};

///  Mat4
class Mat4 {
public:
	Mat4() { }
	explicit Mat4(const Vec4& xAxis, const Vec4& yAxis, const Vec4& zAxis, const Vec4& wAxis);
	explicit Mat4(const class Quat4& rotation, const Vec3& position);

	void set(const Vec4& xAxis, const Vec4& yAxis, const Vec4& zAxis, const Vec4& wAxis) {
		mat[0] = xAxis;
		mat[1] = yAxis;
		mat[2] = zAxis;
		mat[3] = wAxis;
	}

	void make_identity() {
		mat[0].set(1.f, 0.f, 0.f, 0.f);
		mat[1].set(0.f, 1.f, 0.f, 0.f);
		mat[2].set(0.f, 0.f, 1.f, 0.f);
		mat[3].set(0.f, 0.f, 0.f, 1.f);
	}

	void make_scale(const Vec3& scale) {
		mat[0].set(scale.x, 0.f, 0.f, 0.f);
		mat[1].set(0.f, scale.y, 0.f, 0.f);
		mat[2].set(0.f, 0.f, scale.z, 0.f);
		mat[3].set(0.f, 0.f, 0.f, 1.f);
	}

	static Mat4 make_translation(const Vec3& translation) {
		Mat4 mat;
		mat[0].set(1.f, 0.f, 0.f, 0.f);
		mat[1].set(0.f, 1.f, 0.f, 0.f);
		mat[2].set(0.f, 0.f, 1.f, 0.f);
		mat[3].set(translation.x, translation.y, translation.z, 1.f);
		return mat;
	}

	static Mat4 look_at(const Vec3& eye, const Vec3& at, const Vec3& up) {
		Vec3 z_axis = at - eye;
		z_axis.normalize_self();

		Vec3 x_axis = up.cross(z_axis);
		x_axis.normalize_self();

		Vec3 y_axis = z_axis.cross(x_axis);
		y_axis.normalize_self();

		Mat4 look_at;
		look_at.make_identity();

		look_at.mat[0][0] = x_axis.x;
		look_at.mat[1][0] = x_axis.y;
		look_at.mat[2][0] = x_axis.z;
		look_at.mat[3][0] = -(x_axis.dot(eye));

		look_at.mat[0][1] = y_axis.x;
		look_at.mat[1][1] = y_axis.y;
		look_at.mat[2][1] = y_axis.z;
		look_at.mat[3][1] = -(y_axis.dot(eye));

		look_at.mat[0][2] = z_axis.x;
		look_at.mat[1][2] = z_axis.y;
		look_at.mat[2][2] = z_axis.z;
		look_at.mat[3][2] = -(z_axis.dot(eye));

		return look_at;
	}

	void create_perspective_matrix(const f32 fov, const f32 aspect, const f32 zn, const f32 zf) {
		const float yscale = 1.f / tanf(fov / 2.0f);
		const float xscale = yscale / aspect;

		make_identity();

		mat[0][0] = xscale;
		mat[1][1] = yscale;
		mat[2][2] = zf / (zf - zn);
		mat[3][2] = -zn * zf / (zf - zn);
		mat[2][3] = 1;
		mat[3][3] = 0;
	}

	Mat4& transpose_self() {
		Mat4 transposedMat;
		transposedMat[0][0] = mat[0][0];
		transposedMat[0][1] = mat[1][0];
		transposedMat[0][2] = mat[2][0];
		transposedMat[0][3] = mat[3][0];
		transposedMat[1][0] = mat[0][1];
		transposedMat[1][1] = mat[1][1];
		transposedMat[1][2] = mat[2][1];
		transposedMat[1][3] = mat[3][1];
		transposedMat[2][0] = mat[0][2];
		transposedMat[2][1] = mat[1][2];
		transposedMat[2][2] = mat[2][2];
		transposedMat[2][3] = mat[3][2];
		transposedMat[3][0] = mat[0][3];
		transposedMat[3][1] = mat[1][3];
		transposedMat[3][2] = mat[2][3];
		transposedMat[3][3] = mat[3][3];

		*this = transposedMat;

		return *this;
	}

	Mat4& transpose_upper() {
		Mat4 transposedMat;
		transposedMat[0][0] = mat[0][0];
		transposedMat[0][1] = mat[1][0];
		transposedMat[0][2] = mat[2][0];
		transposedMat[0][3] = mat[3][0];
		transposedMat[1][0] = mat[0][1];
		transposedMat[1][1] = mat[1][1];
		transposedMat[1][2] = mat[2][1];
		transposedMat[1][3] = mat[3][1];
		transposedMat[2][0] = mat[0][2];
		transposedMat[2][1] = mat[1][2];
		transposedMat[2][2] = mat[2][2];
		transposedMat[2][3] = mat[3][2];

		transposedMat[3][0] = transposedMat[3][1] = transposedMat[3][2] = 0;
		transposedMat[3][3] = 1;

		*this = transposedMat;

		return *this;
	}

	void inverse_projection() {
		Mat4 TempMatrix(*this);

		float OneOverUpperDet = 1.f / ((mat[0][0] * mat[1][1]) - (mat[0][1] * mat[1][0]));
		mat[0][0] = TempMatrix[1][1] * OneOverUpperDet;
		mat[1][1] = TempMatrix[0][0] * OneOverUpperDet;

		float OneOverLowerDet = 1.f / ((mat[2][2] * mat[3][3]) - (mat[2][3] * mat[3][2]));
		mat[2][2] = TempMatrix[3][3] * OneOverLowerDet;
		mat[3][3] = TempMatrix[2][2] * OneOverLowerDet;
		mat[2][3] = -TempMatrix[2][3] * OneOverLowerDet;
		mat[3][2] = -TempMatrix[3][2] * OneOverLowerDet;
	}

	void inverse_fast() {
		Vec3 Trans(-mat[3][0], -mat[3][1], -mat[3][2]);

		mat[3][0] = mat[3][1] = mat[3][2] = 0;
		transpose_upper();

		Trans = Trans * *this;
		mat[3][0] = Trans.x;
		mat[3][1] = Trans.y;
		mat[3][2] = Trans.z;
	}

	Vec4& operator[](const int index) { return mat[index]; }

	const Vec4& operator[](const int index) const { return mat[index]; }

	void operator *=(const Mat4& op2) {
		Mat4 tempMatrix = *this;

		mat[0][0] = (tempMatrix[0][0] * op2[0][0]) + (tempMatrix[0][1] * op2[1][0]) + (tempMatrix[0][2] * op2[2][0]) + (tempMatrix[0][3] * op2[3][0]);
		mat[1][0] = (tempMatrix[1][0] * op2[0][0]) + (tempMatrix[1][1] * op2[1][0]) + (tempMatrix[1][2] * op2[2][0]) + (tempMatrix[1][3] * op2[3][0]);
		mat[2][0] = (tempMatrix[2][0] * op2[0][0]) + (tempMatrix[2][1] * op2[1][0]) + (tempMatrix[2][2] * op2[2][0]) + (tempMatrix[2][3] * op2[3][0]);
		mat[3][0] = (tempMatrix[3][0] * op2[0][0]) + (tempMatrix[3][1] * op2[1][0]) + (tempMatrix[3][2] * op2[2][0]) + (tempMatrix[3][3] * op2[3][0]);

		mat[0][1] = (tempMatrix[0][0] * op2[0][1]) + (tempMatrix[0][1] * op2[1][1]) + (tempMatrix[0][2] * op2[2][1]) + (tempMatrix[0][3] * op2[3][1]);
		mat[1][1] = (tempMatrix[1][0] * op2[0][1]) + (tempMatrix[1][1] * op2[1][1]) + (tempMatrix[1][2] * op2[2][1]) + (tempMatrix[1][3] * op2[3][1]);
		mat[2][1] = (tempMatrix[2][0] * op2[0][1]) + (tempMatrix[2][1] * op2[1][1]) + (tempMatrix[2][2] * op2[2][1]) + (tempMatrix[2][3] * op2[3][1]);
		mat[3][1] = (tempMatrix[3][0] * op2[0][1]) + (tempMatrix[3][1] * op2[1][1]) + (tempMatrix[3][2] * op2[2][1]) + (tempMatrix[3][3] * op2[3][1]);

		mat[0][2] = (tempMatrix[0][0] * op2[0][2]) + (tempMatrix[0][1] * op2[1][2]) + (tempMatrix[0][2] * op2[2][2]) + (tempMatrix[0][3] * op2[3][2]);
		mat[1][2] = (tempMatrix[1][0] * op2[0][2]) + (tempMatrix[1][1] * op2[1][2]) + (tempMatrix[1][2] * op2[2][2]) + (tempMatrix[1][3] * op2[3][2]);
		mat[2][2] = (tempMatrix[2][0] * op2[0][2]) + (tempMatrix[2][1] * op2[1][2]) + (tempMatrix[2][2] * op2[2][2]) + (tempMatrix[2][3] * op2[3][2]);
		mat[3][2] = (tempMatrix[3][0] * op2[0][2]) + (tempMatrix[3][1] * op2[1][2]) + (tempMatrix[3][2] * op2[2][2]) + (tempMatrix[3][3] * op2[3][2]);

		mat[0][3] = (tempMatrix[0][0] * op2[0][3]) + (tempMatrix[0][1] * op2[1][3]) + (tempMatrix[0][2] * op2[2][3]) + (tempMatrix[0][3] * op2[3][3]);
		mat[1][3] = (tempMatrix[1][0] * op2[0][3]) + (tempMatrix[1][1] * op2[1][3]) + (tempMatrix[1][2] * op2[2][3]) + (tempMatrix[1][3] * op2[3][3]);
		mat[2][3] = (tempMatrix[2][0] * op2[0][3]) + (tempMatrix[2][1] * op2[1][3]) + (tempMatrix[2][2] * op2[2][3]) + (tempMatrix[2][3] * op2[3][3]);
		mat[3][3] = (tempMatrix[3][0] * op2[0][3]) + (tempMatrix[3][1] * op2[1][3]) + (tempMatrix[3][2] * op2[2][3]) + (tempMatrix[3][3] * op2[3][3]);
	}

	Mat4 operator*(const Mat4& MatrixOperand) const {
		Mat4 tempMatrix;

		tempMatrix[0][0] = (mat[0][0] * MatrixOperand[0][0]) + (mat[0][1] * MatrixOperand[1][0]) + (mat[0][2] * MatrixOperand[2][0]) + (mat[0][3] * MatrixOperand[3][0]);
		tempMatrix[1][0] = (mat[1][0] * MatrixOperand[0][0]) + (mat[1][1] * MatrixOperand[1][0]) + (mat[1][2] * MatrixOperand[2][0]) + (mat[1][3] * MatrixOperand[3][0]);
		tempMatrix[2][0] = (mat[2][0] * MatrixOperand[0][0]) + (mat[2][1] * MatrixOperand[1][0]) + (mat[2][2] * MatrixOperand[2][0]) + (mat[2][3] * MatrixOperand[3][0]);
		tempMatrix[3][0] = (mat[3][0] * MatrixOperand[0][0]) + (mat[3][1] * MatrixOperand[1][0]) + (mat[3][2] * MatrixOperand[2][0]) + (mat[3][3] * MatrixOperand[3][0]);

		tempMatrix[0][1] = (mat[0][0] * MatrixOperand[0][1]) + (mat[0][1] * MatrixOperand[1][1]) + (mat[0][2] * MatrixOperand[2][1]) + (mat[0][3] * MatrixOperand[3][1]);
		tempMatrix[1][1] = (mat[1][0] * MatrixOperand[0][1]) + (mat[1][1] * MatrixOperand[1][1]) + (mat[1][2] * MatrixOperand[2][1]) + (mat[1][3] * MatrixOperand[3][1]);
		tempMatrix[2][1] = (mat[2][0] * MatrixOperand[0][1]) + (mat[2][1] * MatrixOperand[1][1]) + (mat[2][2] * MatrixOperand[2][1]) + (mat[2][3] * MatrixOperand[3][1]);
		tempMatrix[3][1] = (mat[3][0] * MatrixOperand[0][1]) + (mat[3][1] * MatrixOperand[1][1]) + (mat[3][2] * MatrixOperand[2][1]) + (mat[3][3] * MatrixOperand[3][1]);

		tempMatrix[0][2] = (mat[0][0] * MatrixOperand[0][2]) + (mat[0][1] * MatrixOperand[1][2]) + (mat[0][2] * MatrixOperand[2][2]) + (mat[0][3] * MatrixOperand[3][2]);
		tempMatrix[1][2] = (mat[1][0] * MatrixOperand[0][2]) + (mat[1][1] * MatrixOperand[1][2]) + (mat[1][2] * MatrixOperand[2][2]) + (mat[1][3] * MatrixOperand[3][2]);
		tempMatrix[2][2] = (mat[2][0] * MatrixOperand[0][2]) + (mat[2][1] * MatrixOperand[1][2]) + (mat[2][2] * MatrixOperand[2][2]) + (mat[2][3] * MatrixOperand[3][2]);
		tempMatrix[3][2] = (mat[3][0] * MatrixOperand[0][2]) + (mat[3][1] * MatrixOperand[1][2]) + (mat[3][2] * MatrixOperand[2][2]) + (mat[3][3] * MatrixOperand[3][2]);

		tempMatrix[0][3] = (mat[0][0] * MatrixOperand[0][3]) + (mat[0][1] * MatrixOperand[1][3]) + (mat[0][2] * MatrixOperand[2][3]) + (mat[0][3] * MatrixOperand[3][3]);
		tempMatrix[1][3] = (mat[1][0] * MatrixOperand[0][3]) + (mat[1][1] * MatrixOperand[1][3]) + (mat[1][2] * MatrixOperand[2][3]) + (mat[1][3] * MatrixOperand[3][3]);
		tempMatrix[2][3] = (mat[2][0] * MatrixOperand[0][3]) + (mat[2][1] * MatrixOperand[1][3]) + (mat[2][2] * MatrixOperand[2][3]) + (mat[2][3] * MatrixOperand[3][3]);
		tempMatrix[3][3] = (mat[3][0] * MatrixOperand[0][3]) + (mat[3][1] * MatrixOperand[1][3]) + (mat[3][2] * MatrixOperand[2][3]) + (mat[3][3] * MatrixOperand[3][3]);

		return tempMatrix;
	}

	static Mat4 ortho_lh(const f32 width, const f32 height, const f32 near_plane, const f32 far_plane) {
		Mat4 ortho_mat;
		ortho_mat.make_identity();

		ortho_mat.mat[0].x = 2.f / width;
		ortho_mat.mat[1].y = 2.f / height;
		ortho_mat.mat[2].z = 1.f / (far_plane - near_plane);
		ortho_mat.mat[3].z = near_plane / (near_plane - far_plane);

		return ortho_mat;
	}

	bool is_identity() const {
		const float epsilon = 1e-6f;

		if (std::abs(mat[0][0] - 1.0f) > epsilon ||
			std::abs(mat[1][1] - 1.0f) > epsilon ||
			std::abs(mat[2][2] - 1.0f) > epsilon ||
			std::abs(mat[3][3] - 1.0f) > epsilon) {
			return false;
		}

		// Check non-diagonal elements
		for (int i = 0; i < 4; ++i) {
			for (int j = 0; j < 4; ++j) {
				if (i != j) {
					if (std::abs(mat[i][j]) > epsilon) {
						return false;
					}
				}
			}
		}

		return true;
	}

	Vec3 transform_point(const Vec3& point) const;

	// Retrieve clip plane from projection matrices.
	// See http://gamedevs.org/uploads/fast-extraction-viewing-frustum-planes-from-world-view-projection-matrix.pdf
	void left_clip_plane(class Plane3d& out_plane) const;
	void right_clip_plane(class Plane3d& out_plane) const;
	void top_clip_plane(class Plane3d& out_plane) const;
	void bottom_clip_plane(class Plane3d& out_plane) const;
	void near_clip_plane(class Plane3d& out_plane) const;
	void far_clip_plane(class Plane3d& out_plane) const;

private:
	Vec4 mat[4];

public:
	const static Mat4 identity;
};