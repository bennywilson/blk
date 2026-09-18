/// render_defs.cpp
///
/// 2025 blk

#include "blk_core.h"
#include "render_defs.h"

using namespace std;

/// make_render_camera
RenderCamera make_render_camera(const Vec3& position, const Quat4& rotation, const f32 fov, const f32 aspect, const f32 near_z, const f32 far_z) {
	RenderCamera camera;
	camera.view_position = position;
	camera.view_rotation = rotation;

	const Mat4 trans = Mat4::make_translation(-position);
	Mat4 rot = rotation.to_mat4();
	rot.transpose_self();
	camera.view_matrix = trans * rot;

	camera.projection_matrix.make_identity();
	camera.projection_matrix.create_perspective_matrix(fov, aspect, near_z, far_z);

	camera.view_projection_matrix = camera.view_matrix * camera.projection_matrix;

	// The general inverse, not `inverse_fast()`/`inverse_projection()`: a
	// view-projection is neither rigid nor the bare perspective layout. This
	// replaced `XMMatrixInverse` - the engine core's only DirectXMath call - so
	// native and web now produce the same matrix. It is also the more accurate
	// of the two here (4.7e-8 vs 7.7e-6 relative error against an exact double
	// reference), because it works in double and DirectXMath works in float.
	if (!camera.view_projection_matrix.inverse(camera.inv_view_projection_matrix)) {
		// Only reachable with a degenerate camera (zero aspect, near == far).
		// Identity keeps downstream reconstruction finite instead of NaN.
		blk::warn("make_render_camera - view-projection matrix is singular; using identity inverse");
		camera.inv_view_projection_matrix.make_identity();
	}

	return camera;
}

/// render_pass_in_mask
bool render_pass_in_mask(const ERenderPass pass, const ERenderPassMask& mask) {
	for (const ERenderPass p : mask) {
		if (p == pass) {
			return true;
		}
	}
	return false;
}

/// RenderBuffer::create_vertex_buffer
void RenderBuffer::create_vertex_buffer(const u32 num_verts) {
	m_num_elements = num_verts;
	m_size_bytes = m_num_elements * sizeof(vertexLayout);
	create_internal();
}

/// RenderBuffer::write_vertex_buffer
void RenderBuffer::write_vertex_buffer(const vector<vertexLayout>& vertices) {
	m_num_elements = (uint32_t)vertices.size();
	m_size_bytes = m_num_elements * sizeof(vertexLayout);
	create_internal();

	u8* const vb = map();
	memcpy(vb, vertices.data(), m_size_bytes);
	unmap();
}

/// RenderBuffer::create_index_buffer
void RenderBuffer::create_index_buffer(const u32 num_indices) {
	m_num_elements = num_indices;
	m_size_bytes = m_num_elements * sizeof(u16);
	create_internal();
}

/// RenderBuffer::write_index_buffer
void RenderBuffer::write_index_buffer(const vector<u16>& indices) {
	m_num_elements = (uint32_t)indices.size();
	m_size_bytes = m_num_elements * sizeof(u16);
	create_internal();

	u8* const ib = map();
	memcpy(ib, indices.data(), m_size_bytes);
	unmap();
}

/// BoneMatrix_t::Invert
void BoneMatrix_t::Invert() {
	Vec3 Trans(-m_Axis[3]);
	m_Axis[3].set(0.0f, 0.0f, 0.0f);

	TransposeUpper();

	Vec3 finalTrans;
	finalTrans.x = Trans.x * m_Axis[0].x + Trans.y * m_Axis[1].x + Trans.z * m_Axis[2].x + m_Axis[3].x;
	finalTrans.y = Trans.x * m_Axis[0].y + Trans.y * m_Axis[1].y + Trans.z * m_Axis[2].y + m_Axis[3].y;
	finalTrans.z = Trans.x * m_Axis[0].z + Trans.y * m_Axis[1].z + Trans.z * m_Axis[2].z + m_Axis[3].z;

	m_Axis[3].x = finalTrans.x;
	m_Axis[3].y = finalTrans.y;
	m_Axis[3].z = finalTrans.z;
}

/// BoneMatrix_t::SetFromQuat
void BoneMatrix_t::SetFromQuat(const Quat4& srcQuat) {

	Mat4 mat;
	const float xx = srcQuat.x * srcQuat.x;
	const float xy = srcQuat.x * srcQuat.y;
	const float xz = srcQuat.x * srcQuat.z;
	const float xw = srcQuat.x * srcQuat.w;

	const float yy = srcQuat.y * srcQuat.y;
	const float yz = srcQuat.y * srcQuat.z;
	const float yw = srcQuat.y * srcQuat.w;

	const float zz = srcQuat.z * srcQuat.z;
	const float zw = srcQuat.z * srcQuat.w;

	m_Axis[0].x = 1 - 2 * (yy + zz);
	m_Axis[0].y = 2 * (xy - zw);
	m_Axis[0].z = 2 * (xz + yw);

	m_Axis[1].x = 2 * (xy + zw);
	m_Axis[1].y = 1 - 2 * (xx + zz);
	m_Axis[1].z = 2 * (yz - xw);

	m_Axis[2].x = 2 * (xz - yw);
	m_Axis[2].y = 2 * (yz + xw);
	m_Axis[2].z = 1 - 2 * (xx + yy);

	m_Axis[3] = Vec3::zero;
}

/// BoneMatrix_t::TransposeUpper()
void BoneMatrix_t::TransposeUpper() {
	BoneMatrix_t transposedMat;
	transposedMat.m_Axis[0].set(m_Axis[0].x, m_Axis[1].x, m_Axis[2].x);
	transposedMat.m_Axis[1].set(m_Axis[0].y, m_Axis[1].y, m_Axis[2].y);
	transposedMat.m_Axis[2].set(m_Axis[0].z, m_Axis[1].z, m_Axis[2].z);
	transposedMat.m_Axis[3] = Vec3::zero;
	*this = transposedMat;
}

/// BoneMatrix_t::operator*
Vec3 operator*(const Vec3& lhs, const BoneMatrix_t& rhs) {
	Vec3 returnValue;

	returnValue.x = (lhs.x * rhs.m_Axis[0].x) + (lhs.y * rhs.m_Axis[1].x) + (lhs.z * rhs.m_Axis[2].x) + rhs.m_Axis[3].x;
	returnValue.y = (lhs.x * rhs.m_Axis[0].y) + (lhs.y * rhs.m_Axis[1].y) + (lhs.z * rhs.m_Axis[2].y) + rhs.m_Axis[3].y;
	returnValue.z = (lhs.x * rhs.m_Axis[0].z) + (lhs.y * rhs.m_Axis[1].z) + (lhs.z * rhs.m_Axis[2].z) + rhs.m_Axis[3].z;

	return returnValue;
}

/// BoneMatrix_t::operator*=
void BoneMatrix_t::operator*=(const BoneMatrix_t& op2) {
	BoneMatrix_t temp = *this;
	m_Axis[0].x = temp.m_Axis[0].x * op2.m_Axis[0].x + temp.m_Axis[0].y * op2.m_Axis[1].x + temp.m_Axis[0].z * op2.m_Axis[2].x;
	m_Axis[1].x = temp.m_Axis[1].x * op2.m_Axis[0].x + temp.m_Axis[1].y * op2.m_Axis[1].x + temp.m_Axis[1].z * op2.m_Axis[2].x;
	m_Axis[2].x = temp.m_Axis[2].x * op2.m_Axis[0].x + temp.m_Axis[2].y * op2.m_Axis[1].x + temp.m_Axis[2].z * op2.m_Axis[2].x;
	m_Axis[3].x = temp.m_Axis[3].x * op2.m_Axis[0].x + temp.m_Axis[3].y * op2.m_Axis[1].x + temp.m_Axis[3].z * op2.m_Axis[2].x + op2.m_Axis[3].x;

	m_Axis[0].y = temp.m_Axis[0].x * op2.m_Axis[0].y + temp.m_Axis[0].y * op2.m_Axis[1].y + temp.m_Axis[0].z * op2.m_Axis[2].y;
	m_Axis[1].y = temp.m_Axis[1].x * op2.m_Axis[0].y + temp.m_Axis[1].y * op2.m_Axis[1].y + temp.m_Axis[1].z * op2.m_Axis[2].y;
	m_Axis[2].y = temp.m_Axis[2].x * op2.m_Axis[0].y + temp.m_Axis[2].y * op2.m_Axis[1].y + temp.m_Axis[2].z * op2.m_Axis[2].y;
	m_Axis[3].y = temp.m_Axis[3].x * op2.m_Axis[0].y + temp.m_Axis[3].y * op2.m_Axis[1].y + temp.m_Axis[3].z * op2.m_Axis[2].y + op2.m_Axis[3].y;

	m_Axis[0].z = temp.m_Axis[0].x * op2.m_Axis[0].z + temp.m_Axis[0].y * op2.m_Axis[1].z + temp.m_Axis[0].z * op2.m_Axis[2].z;
	m_Axis[1].z = temp.m_Axis[1].x * op2.m_Axis[0].z + temp.m_Axis[1].y * op2.m_Axis[1].z + temp.m_Axis[1].z * op2.m_Axis[2].z;
	m_Axis[2].z = temp.m_Axis[2].x * op2.m_Axis[0].z + temp.m_Axis[2].y * op2.m_Axis[1].z + temp.m_Axis[2].z * op2.m_Axis[2].z;
	m_Axis[3].z = temp.m_Axis[3].x * op2.m_Axis[0].z + temp.m_Axis[3].y * op2.m_Axis[1].z + temp.m_Axis[3].z * op2.m_Axis[2].z + op2.m_Axis[3].z;
}

/// BoneMatrix_t::operator*=
void BoneMatrix_t::operator*=(const Mat4& op2) {
	BoneMatrix_t temp = *this;
	m_Axis[0].x = temp.m_Axis[0].x * op2[0].x + temp.m_Axis[0].y * op2[1].x + temp.m_Axis[0].z * op2[2].x;
	m_Axis[1].x = temp.m_Axis[1].x * op2[0].x + temp.m_Axis[1].y * op2[1].x + temp.m_Axis[1].z * op2[2].x;
	m_Axis[2].x = temp.m_Axis[2].x * op2[0].x + temp.m_Axis[2].y * op2[1].x + temp.m_Axis[2].z * op2[2].x;
	m_Axis[3].x = temp.m_Axis[3].x * op2[0].x + temp.m_Axis[3].y * op2[1].x + temp.m_Axis[3].z * op2[2].x + op2[3].x;

	m_Axis[0].y = temp.m_Axis[0].x * op2[0].y + temp.m_Axis[0].y * op2[1].y + temp.m_Axis[0].z * op2[2].y;
	m_Axis[1].y = temp.m_Axis[1].x * op2[0].y + temp.m_Axis[1].y * op2[1].y + temp.m_Axis[1].z * op2[2].y;
	m_Axis[2].y = temp.m_Axis[2].x * op2[0].y + temp.m_Axis[2].y * op2[1].y + temp.m_Axis[2].z * op2[2].y;
	m_Axis[3].y = temp.m_Axis[3].x * op2[0].y + temp.m_Axis[3].y * op2[1].y + temp.m_Axis[3].z * op2[2].y + op2[3].y;

	m_Axis[0].z = temp.m_Axis[0].x * op2[0].z + temp.m_Axis[0].y * op2[1].z + temp.m_Axis[0].z * op2[2].z;
	m_Axis[1].z = temp.m_Axis[1].x * op2[0].z + temp.m_Axis[1].y * op2[1].z + temp.m_Axis[1].z * op2[2].z;
	m_Axis[2].z = temp.m_Axis[2].x * op2[0].z + temp.m_Axis[2].y * op2[1].z + temp.m_Axis[2].z * op2[2].z;
	m_Axis[3].z = temp.m_Axis[3].x * op2[0].z + temp.m_Axis[3].y * op2[1].z + temp.m_Axis[3].z * op2[2].z + op2[3].z;
}
