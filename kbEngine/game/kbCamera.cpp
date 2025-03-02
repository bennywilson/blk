/// kbCamera.cpp
///
/// 2016-2025 blk 1.0

#include "kbCamera.h"

/// kbCamera
kbCamera::kbCamera() :
	m_position( 0.0f, 3.0f, 20.0f ),
	m_rotation( 0.0f, 0.0f, 0.0f, 1.0f ),
	m_rotationTarget( 0.0f, 0.0f, 0.0f, 1.0f ) {
}

/// kbCamera::Update
void kbCamera::Update() {
	m_rotation = m_rotationTarget;//Quat4::slerp(m_rotation, m_rotationTarget, 0.33f);
	m_rotation.normalize_self();
}