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

	//void set_model(const class kbModel* pModel) { m_model = pModel; }
	//const kbModel* model() const { return m_model; }

	virtual void editor_change(const std::string& propertyName);

protected:
	virtual void enable_internal(const bool isEnabled) override;
	virtual void update_internal(const float DeltaTime) override;

private:
	const kbModel* m_model;
};