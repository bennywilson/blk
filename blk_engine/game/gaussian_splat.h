/// gaussian_splat.h
///
/// 2025 blk 1.0

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

	const std::vector<PointCloudData>* point_cloud() const { if (!m_model) return nullptr; return &m_model->point_cloud(); }

	virtual void editor_change(const std::string& propertyName);

protected:
	virtual void enable_internal(const bool isEnabled) override;
	virtual void update_internal(const float DeltaTime) override;

private:
	const kbModel* m_model;
};