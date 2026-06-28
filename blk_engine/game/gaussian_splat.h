/// gaussian_splat.h
///
/// 2025-2026 blk 1.0

#pragma once

#include "render_component.h"
#include "model.h"

class kbAnimation;
class Model;

/// GaussianSplat
class GaussianSplatComponent : public RenderComponent {
	KB_DECLARE_COMPONENT(GaussianSplatComponent, RenderComponent);

public:
	virtual ~GaussianSplatComponent();

	const std::vector<PointCloudSample>* point_cloud() const { if (!m_model) return nullptr; return &m_model->point_cloud(); }

	virtual void editor_change(const std::string& propertyName);

	f32 splat_falloff() const { return m_splat_falloff; }
	f32 splat_scale() const { return m_splat_scale; }
	f32 contrast() const { return m_contrast; }
	i32 max_sh_degree() const { return m_max_sh_degree; }

	bool gpu_sort() const { return m_gpu_sort; }

	bool splat_dirty() const { return m_splat_dirty; }
	void set_splat_dirty(const bool new_dirty) { m_splat_dirty = new_dirty; }

protected:
	virtual void enable_internal(const bool isEnabled) override;
	virtual void update_internal(const float DeltaTime) override;

private:
	const kbModel* m_model;
	f32 m_splat_falloff;
	f32 m_splat_scale;
	f32 m_contrast;
	i32 m_max_sh_degree;

	bool m_gpu_sort;

	bool m_splat_dirty;
};