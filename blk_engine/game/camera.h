/// camera.h
///
///
/// 2016 blk

#pragma once

#include "matrix.h"
#include "quaternion.h"

/// Camera
class Camera {
public:
	friend class Editor;

	Camera();

	void Update();

//private:
	Vec3 m_position;
	Quat4 m_rotation;

	Mat4 m_EyeMats[2];

	Quat4 m_rotation_current;
	Quat4 m_rotation_target;
};
