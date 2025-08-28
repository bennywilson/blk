/// gaussian_splat.cpp
///
/// 2025 blk 1.0

#include "blk_core.h"
#include "entity_header.h"
#include "gaussian_splat.h"
#include "renderer_dx12.h"

KB_DEFINE_COMPONENT(GaussianSplatComponent)

/// GaussianSplatComponent
void GaussianSplatComponent::Constructor() {
	m_model = nullptr;
}

/// ~GaussianSplatComponent
GaussianSplatComponent::~GaussianSplatComponent() {
}

/// GaussianSplatComponent::editor_change
void GaussianSplatComponent::editor_change(const std::string& propertyName) {
	Super::editor_change(propertyName);
}

/// GaussianSplatComponent::enable_internal
void GaussianSplatComponent::enable_internal(const bool isEnabled) {
	Super::enable_internal(isEnabled);

	if (isEnabled) {
		if (g_renderer) {
			g_renderer->add_render_component(this);
		} 
	} else {
		if (g_renderer) {
			g_renderer->add_render_component(this);
		}
	}
}

/// GaussianSplatComponent::update_internal
void GaussianSplatComponent::update_internal(const float DeltaTime) {
	Super::update_internal(DeltaTime);
}
