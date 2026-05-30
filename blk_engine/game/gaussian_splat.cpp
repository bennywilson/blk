/// gaussian_splat.cpp
///
/// 2025-2026 blk 1.0

#include "blk_core.h"
#include "entity_header.h"
#include "gaussian_splat.h"
#include "renderer_dx12.h"

KB_DEFINE_COMPONENT(GaussianSplatComponent)

/// GaussianSplatComponent
void GaussianSplatComponent::Constructor() {
	m_model = nullptr;

	m_splat_dirty = true;
	m_splat_falloff = 4.0f;
	m_splat_scale = 3.0f;
	m_gpu_sort = false;
	m_contrast = 1.0f;
}

/// ~GaussianSplatComponent
GaussianSplatComponent::~GaussianSplatComponent() {
}

/// GaussianSplatComponent::editor_change
void GaussianSplatComponent::editor_change(const std::string& property_name) {
	Super::editor_change(property_name);

	if (property_name == "Model" || property_name == "GpuSort") {
		if (IsEnabled()) {
			Enable(false);
			Enable(true);
		}
	}
}

/// GaussianSplatComponent::enable_internal
void GaussianSplatComponent::enable_internal(const bool enable) {
	const bool should_enable = m_model != nullptr;
	Super::enable_internal(enable && m_model);

	if (this->IsEnabled()) {
		if (g_renderer) {
			g_renderer->add_render_component(this);
		}
	} else {
		if (g_renderer) {
			g_renderer->remove_render_component(this);
		}
	}
}

/// GaussianSplatComponent::update_internal
void GaussianSplatComponent::update_internal(const float DeltaTime) {
	Super::update_internal(DeltaTime);
}
