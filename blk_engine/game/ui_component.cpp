/// ui_component.cpp
///
/// 2019 blk

#include "blk_containers.h"
#include "entity_header.h"
#include "ui_component.h"

GameEntity& GetUIGameEntity() {
	static GameEntity m_GameEnt;
	return m_GameEnt;
}

/// UIComponent::Constructor
void UIComponent::Constructor() {
	m_AuthoredWidth = 128;
	m_AuthoredHeight = 128;
	m_NormalizedAnchorPoint.set(0.05f, 0.05f, 0.0f);
	m_UIToScreenSizeRatio.set(0.1f, 0.0f, 0.0f);
	m_NormalizedScreenSize.set(0.f, 0.0f, 0.0f);

	m_pStaticRenderComponent = nullptr;
}

/// UIComponent::~UIComponent
UIComponent::~UIComponent() {
	m_AuthoredWidth = 128;
	m_AuthoredHeight = 128;
	m_NormalizedAnchorPoint.set(0.05f, 0.05f, 0.0f);
	m_UIToScreenSizeRatio.set(0.1f, 0.0f, 0.0f);
}

/// UIComponent::RegisterEventListener
void UIComponent::RegisterEventListener(IUIWidgetListener* const pListener) {
	m_EventListeners.push_back(pListener);
}

/// UIComponent::UnregisterEventListener
void UIComponent::UnregisterEventListener(IUIWidgetListener* const pListener) {
	blk::std_remove_swap(m_EventListeners, pListener);
}

/// UIComponent::SetMaterialParamVector
void UIComponent::set_material_param_vec4(const std::string& paramName, const Vec4& paramValue) {
	blk::error_check(m_pStaticRenderComponent != nullptr, "bUIComponent::set_material_param_vec4() - m_pStaticRenderComponent is NULL");

	m_pStaticRenderComponent->set_material_param_vec4(0, paramName, paramValue);
}

/// UIComponent::SetMaterialParamTexture
void UIComponent::set_material_param_texture(const std::string& paramName, Texture* const pTexture) {
	blk::error_check(m_pStaticRenderComponent != nullptr, "bUIComponent::set_material_param_texture() - m_pStaticRenderComponent is NULL");

	m_pStaticRenderComponent->set_material_param_texture(0, paramName, pTexture);
}

/// UIComponent::FireEvent
void UIComponent::FireEvent(const Input_t* const pInput) {

	for (int i = 0; i < m_EventListeners.size(); i++) {
		m_EventListeners[i]->WidgetEventCB(nullptr, pInput);
	}
}

/// UIComponent::EditorChange
void UIComponent::editor_change(const std::string& propertyName) {
	Super::editor_change(propertyName);
	FindStaticRenderComponent();
	RefreshMaterial();
}

/// UIComponent::enable_internal
void UIComponent::enable_internal(const bool bEnable) {
	Super::enable_internal(bEnable);

	if (bEnable) {
		FindStaticRenderComponent();

		if (m_pStaticRenderComponent != nullptr) {
			m_pStaticRenderComponent->Enable(true);
		}
		RefreshMaterial();

		g_pInputManager->RegisterInputListener(this);
	} else {

		if (m_pStaticRenderComponent != nullptr) {
			m_pStaticRenderComponent->Enable(false);
			m_pStaticRenderComponent = nullptr;
		}

		g_pInputManager->UnregisterInputListener(this);
	}
}

/// UIComponent:FindStaticRenderComponent
void UIComponent::FindStaticRenderComponent() {
	m_pStaticRenderComponent = GetOwner()->component<RenderComponent>();
}

/// UIComponent:RefreshMaterial

void UIComponent::RefreshMaterial() {
/*	FindStaticRenderComponent();
	if (m_pStaticRenderComponent == nullptr) {
		return;
	}

	const float ScreenPixelWidth = (float)g_pRenderer->GetBackBufferWidth();
	const float ScreenPixelHeight = (float)g_pRenderer->GetBackBufferHeight();

	const float aspectRatio = (float)GetAuthoredWidth() / (float)GetAuthoredHeight();
	m_NormalizedScreenSize.x = GetUIToScreenSizeRatio().x;
	const float screenWidthPixel = m_NormalizedScreenSize.x * ScreenPixelWidth;
	const float screenHeightPixel = screenWidthPixel / aspectRatio;
	m_NormalizedScreenSize.y = screenHeightPixel / ScreenPixelHeight;
	m_NormalizedScreenSize.z = 1.0f;

	//	blk::log( "%f %f %f %f", m_NormalizedScreenSize.x, m_NormalizedScreenSize.y,m_NormalizedAnchorPoint.x - m_NormalizedScreenSize.x * 0.5f,m_NormalizedAnchorPoint.y - m_NormalizedScreenSize.y * 0.5f);
	static String normalizedScreenSize_Anchor("normalizedScreenSize_Anchor");

	const Vec4 sizeAndPos = Vec4(m_NormalizedScreenSize.x,
		m_NormalizedScreenSize.y,
		m_NormalizedAnchorPoint.x + m_NormalizedScreenSize.x * 0.5f,		// Upper left corner to anchor
		m_NormalizedAnchorPoint.y + m_NormalizedScreenSize.y * 0.5f);		// Upper left corner to anchor

	m_pStaticRenderComponent->set_material_param_vec4(0, normalizedScreenSize_Anchor.stl_str(), sizeAndPos);*/
}


/// UIWidgetComponent::Constructor
void UIWidgetComponent::Constructor() {

	m_StartingPosition.set(0.0f, 0.0f, 0.0f);
	m_StartingSize.set(0.5f, 0.5f, 1.0f);

	m_Anchor = UIWidgetComponent::MiddleLeft;
	m_AxisLock = UIWidgetComponent::LockAll;

	m_RelativePosition.set(0.0f, 0.0f, 0.0f);
	m_RelativeSize.set(0.5f, 0.5f, 1.0f);

	m_AbsolutePosition.set(0.0f, 0.0f, 0.0f);
	m_AbsoluteSize.set(0.5f, 0.5f, 1.0f);

	m_model = nullptr;

	m_bHasFocus = false;
}

/// UIWidgetComponent::RegisterEventListener
void UIWidgetComponent::RegisterEventListener(IUIWidgetListener* const pListener) {
	m_EventListeners.push_back(pListener);
}

/// UIWidgetComponent::UnregisterEventListener
void UIWidgetComponent::UnregisterEventListener(IUIWidgetListener* const pListener) {
	blk::std_remove_swap(m_EventListeners, pListener);
}

/// UIWidgetComponent::SetAdditiveTextureFactor
void UIWidgetComponent::SetAdditiveTextureFactor(const float factor) {

	static const String additiveTextureParams("additiveTextureParams");
	m_model->set_material_param_vec4(0, additiveTextureParams.stl_str(), Vec4(factor, 0.0f, 0.0f, 0.0f));
}

/// UIWidgetComponent::FireEvent
void UIWidgetComponent::FireEvent(const Input_t* const pInput) {

	for (int i = 0; i < m_EventListeners.size(); i++) {
		m_EventListeners[i]->WidgetEventCB(this, pInput);
	}
}

/// UIWidgetComponent::EditorChange
void UIWidgetComponent::editor_change(const std::string& propertyName) {

	Super::editor_change(propertyName);
}

/// UIWidgetComponent::InputCB
void UIWidgetComponent::InputCB(const Input_t& input) {
}

/// UIComponent::SetFocus
void UIWidgetComponent::SetFocus(const bool bHasFocus) {
	m_bHasFocus = bHasFocus;
}

/// UIWidgetComponent::SetRelativePosition
void UIWidgetComponent::SetRelativePosition(const Vec3& newPos) {
	m_RelativePosition = newPos;
	m_AbsolutePosition = m_CachedParentPosition + m_CachedParentSize * m_RelativePosition;

	for (size_t i = 0; i < m_ChildWidgets.size(); i++) {
		m_ChildWidgets[i].Recalculate(this, false);
	}
}

/// UIWidgetComponent::SetRelativeSize
void UIWidgetComponent::SetRelativeSize(const Vec3& newSize) {

	m_RelativeSize = newSize;
	m_AbsoluteSize = m_CachedParentSize * m_RelativeSize;

	for (size_t i = 0; i < m_ChildWidgets.size(); i++) {
		m_ChildWidgets[i].Recalculate(this, false);
	}
}

/// UIWidgetComponent::RecalculateOld
void UIWidgetComponent::RecalculateOld(const UIComponent* const pParent, const bool bFull) {
	blk::error_check(pParent != nullptr, "UIWidgetComponent::UpdateFromParent() - null parent");

	/*	if ( m_model != nullptr && pParent != nullptr && pParent->GetStaticRenderComponent() != nullptr ) {
			blk::log( "Setting render oreder bias to %f", pParent->GetStaticRenderComponent()->render_order_bias() - 1.0f );
			m_model->set_render_order_bias( pParent->GetStaticRenderComponent()->render_order_bias() - 1.0f );
		}*/

	m_CachedParentPosition = pParent->GetNormalizedAnchorPt();
	m_CachedParentSize = pParent->GetNormalizedScreenSize();

	m_AbsolutePosition = m_CachedParentPosition + m_CachedParentSize * m_RelativePosition;
	m_AbsoluteSize = m_CachedParentSize * m_RelativeSize;

	set_render_order_bias(pParent->GetStaticRenderComponent()->render_order_bias() - 1.0f);

	for (int i = 0; i < m_ChildWidgets.size(); i++) {
		m_ChildWidgets[i].RecalculateOld(pParent, bFull);
	}
}

/// UIWidgetComponent::Recalculate
void UIWidgetComponent::Recalculate(const UIWidgetComponent* const pParent, const bool bFull) {

	if (pParent != nullptr) {
		m_CachedParentPosition = pParent->GetAbsolutePosition();
		m_CachedParentSize = pParent->GetAbsoluteSize();
		set_render_order_bias(pParent->render_order_bias() - 1.0f);
	} else {
		m_CachedParentPosition = Vec3::zero;
		m_CachedParentSize.set(1.0f, 1.0f, 1.0f);
		set_render_order_bias(0.0f);
	}

	m_AbsolutePosition = m_CachedParentPosition + m_CachedParentSize * m_RelativePosition;
	m_AbsoluteSize = m_CachedParentSize * m_RelativeSize;

	for (int i = 0; i < m_ChildWidgets.size(); i++) {
		m_ChildWidgets[i].Recalculate(this, bFull);
	}
}

/// UIWidgetComponent::SetRenderOrderBias
void UIWidgetComponent::set_render_order_bias(const float bias) {

	if (m_model != nullptr) {
		m_model->set_render_order_bias(bias);
	}
}

/// UIWidgetComponent::GetRenderOrderBias
float UIWidgetComponent::render_order_bias() const {

	if (m_model == nullptr) {
		return 0.0f;
	}

	return m_model->render_order_bias();
}

/// UIWidgetComponent::GetBaseTextureDimensions
Vec2i UIWidgetComponent::GetBaseTextureDimensions() const {
	Vec2i retDim(-1, -1);
	if (m_model == nullptr) {
		return retDim;
	}

	const ShaderParamComponent* const pComp = m_model->shader_param_component(0, String("baseTexture"));
	if (pComp == nullptr || pComp->texture() == nullptr) {
		return retDim;
	}

	retDim.x = pComp->texture()->width();
	retDim.y = pComp->texture()->height();
	return retDim;
}

/// UIWidgetComponent::enable_internal
void UIWidgetComponent::enable_internal(const bool bEnable) {
	Super::enable_internal(bEnable);

	static Model* pUnitQuad = nullptr;
	if (pUnitQuad == nullptr) {
		pUnitQuad = (Model*)g_ResourceManager.resource("../blk_engine/assets/Models/UnitQuad.ms3d", true, true);
	}

	if (GetOwner() == nullptr) {
		return;
	}

	if (bEnable) {

		m_RelativePosition = m_StartingPosition;
		m_RelativeSize = m_StartingSize;

		if (m_model == nullptr) {
			m_model = new StaticModelComponent();
			GetUIGameEntity().add_component(m_model);
		}

		m_model->set_model(pUnitQuad);
		m_model->set_materials(m_Materials);
		m_model->set_render_pass(RP_UI);
		m_model->Enable(false);
		m_model->Enable(true);

		for (size_t i = 0; i < m_ChildWidgets.size(); i++) {
			GetUIGameEntity().add_component(&m_ChildWidgets[i]);  // Note these children are responsible for removing themselves when disabled (see code block below)
			m_ChildWidgets[i].Enable(false);
			m_ChildWidgets[i].Enable(true);
		}

		g_pInputManager->RegisterInputListener(this);

		if (GetOwner() != nullptr) {
			Recalculate(nullptr, true);
		}

	} else {
		if (m_model != nullptr) {
			m_model->Enable(false);
		}

		for (size_t i = 0; i < m_ChildWidgets.size(); i++) {
			m_ChildWidgets[i].Enable(false);
		}

		GetUIGameEntity().remove_component(this);
		GetUIGameEntity().remove_component(m_model);

		g_pInputManager->UnregisterInputListener(this);
	}
}

/// UIWidgetComponent::update_internal
void UIWidgetComponent::update_internal(const float dt) {
/*	Super::update_internal(dt);

	if (m_model == nullptr) {
		return;
	}

	static const String normalizedScreenSize_Anchor("normalizedScreenSize_Anchor");

	Vec3 parentEnd = Vec3::one;
	Vec3 widgetAbsPos = m_AbsolutePosition;
	Vec3 widgetAbsSize = m_AbsoluteSize;
	//	float renderOrderBias = 0.0f;

	f32 aspectRatio = 1.0f;
	const ShaderParamComponent* const pComp = m_model->shader_param_component(0, String("baseTexture"));
	if (pComp != nullptr) {
		const Texture* const pTex = pComp->texture();
		if (pTex != nullptr) {
			aspectRatio = (f32)pTex->width() / (f32)pTex->height();
		}
	}

	const f32 BackBufferWidth = (f32)g_pRenderer->GetBackBufferWidth();
	const f32 BackBufferHeight = (f32)g_pRenderer->GetBackBufferHeight();

	if (m_AxisLock == LockYAxis) {
		const f32 widgetPixelHeight = widgetAbsSize.y * BackBufferHeight;
		const f32 widgetPixelWidth = widgetPixelHeight * aspectRatio;
		widgetAbsSize.x = widgetPixelWidth / BackBufferWidth;
	}

	if (m_Anchor == UIWidgetComponent::MiddleRight) {
		widgetAbsPos.x -= widgetAbsSize.x;
	}

	m_model->set_material_param_vec4(0, normalizedScreenSize_Anchor.stl_str(),
		Vec4(widgetAbsSize.x,
			widgetAbsSize.y,
			widgetAbsPos.x + widgetAbsSize.x * 0.5f,
			widgetAbsPos.y + widgetAbsSize.y * 0.5f));

	m_model->refresh_materials(true);

	for (size_t i = 0; i < m_ChildWidgets.size(); i++) {
		m_ChildWidgets[i].update_internal(dt);
	}

	if (HasFocus()) {
		const Input_t& input = g_pInputManager->get_input();
		if (input.GamepadButtonStates[12].m_Action == Input_t::KA_JustPressed || input.WasNonCharKeyJustPressed(Input_t::Return)) {
			FireEvent(&input);
		}
	}*/
}

/// UISlider::Constructor
void UISlider::Constructor() {
	m_SliderBoundsMin.set(0.0f, 0.0f, 0.0f);
	m_SliderBoundsMax.set(1.0f, 1.0f, 1.0f);

	m_CalculatedSliderBoundsMin.set(0.0f, 0.0f, 0.0f);
	m_CalculatedSliderBoundsMax.set(1.0f, 1.0f, 1.0f);
}

/// UISlider::enable_internal
void UISlider::enable_internal(const bool bEnable) {

	Super::enable_internal(bEnable);
}

/// UISlider::RecalculateOld
void UISlider::RecalculateOld(const UIComponent* const pParent, const bool bFull) {

	blk::error_check(pParent != nullptr, "UIWidgetComponent::UpdateFromParent() - null parent");

	if (m_model != nullptr && pParent != nullptr && pParent->GetStaticRenderComponent() != nullptr) {

		m_model->set_render_order_bias(pParent->GetStaticRenderComponent()->render_order_bias() - 1.0f);
	}

	m_CachedParentPosition = pParent->GetNormalizedAnchorPt();
	m_CachedParentSize = pParent->GetNormalizedScreenSize();

	m_AbsolutePosition = m_CachedParentPosition + m_CachedParentSize * m_RelativePosition;
	m_AbsoluteSize = m_CachedParentSize * m_RelativeSize;

	set_render_order_bias(pParent->GetStaticRenderComponent()->render_order_bias() - 1.0f);

	for (int i = 0; i < m_ChildWidgets.size(); i++) {
		m_ChildWidgets[i].RecalculateOld(pParent, bFull);

		m_ChildWidgets[i].set_render_order_bias(pParent->GetStaticRenderComponent()->render_order_bias() - (1.0f + (float)i));
	}

	const float spaceBetweenLabelAndSlider = 0.05f;

	if (bFull) {
		Vec3 pos = m_RelativePosition;
		pos.x = m_RelativePosition.x + m_RelativeSize.x + spaceBetweenLabelAndSlider;
		m_ChildWidgets[1].SetRelativePosition(pos);
	} else {
		Vec3 pos = m_ChildWidgets[0].GetRelativePosition();
		pos.y = m_RelativePosition.y;

		pos = m_ChildWidgets[1].GetRelativePosition();
		pos.y = m_RelativePosition.y;
		m_ChildWidgets[1].SetRelativePosition(pos);
	}

	if (m_ChildWidgets.size() < 2) {
		m_CalculatedSliderBoundsMin.set(0.0f, 0.0f, 0.0f);
		m_CalculatedSliderBoundsMax.set(0.0f, 0.0f, 0.0f);
	} else {
		m_CalculatedSliderBoundsMin = GetRelativePosition() + spaceBetweenLabelAndSlider;
		m_CalculatedSliderBoundsMax = m_CalculatedSliderBoundsMin + m_ChildWidgets[0].GetRelativeSize() * 0.9f; // Hack

		m_ChildWidgets[0].SetRelativePosition(GetRelativePosition() + Vec3(spaceBetweenLabelAndSlider, 0.0f, 0.0f));

		if (bFull) {
			m_ChildWidgets[1].SetRelativePosition(GetRelativePosition() + Vec3(GetRelativeSize().x + 0.05f, 0.0f, 0.0f));
		}
	}
}

/// UISlider::Recalculate
void UISlider::Recalculate(const UIWidgetComponent* const pParent, const bool bFull) {

	if (pParent == nullptr) {
		return;
	}

	// blk::error_check( pParent != nullptr, "UIWidgetComponent::UpdateFromParent() - null parent" );

	if (m_model != nullptr && pParent != nullptr && pParent->GetStaticModel() != nullptr) {
		m_model->set_render_order_bias(pParent->GetStaticModel()->render_order_bias() - 1.0f);
		blk::log("Slider: Setting render oreder bias to %f", pParent->GetStaticModel()->render_order_bias() - 1.0f);
	}

	m_CachedParentPosition = pParent->GetAbsolutePosition();
	m_CachedParentSize = pParent->GetAbsoluteSize();

	m_AbsolutePosition = m_CachedParentPosition + m_CachedParentSize * m_RelativePosition;
	m_AbsoluteSize = m_CachedParentSize * m_RelativeSize;

	for (int i = 0; i < m_ChildWidgets.size(); i++) {
		m_ChildWidgets[i].Recalculate(pParent, bFull);
	}

	if (bFull) {
		Vec3 pos = m_RelativePosition;
		pos.x = m_RelativePosition.x + m_RelativeSize.x + 0.05f;

		m_ChildWidgets[0].SetRelativePosition(pos);
		m_ChildWidgets[1].SetRelativePosition(pos);
	} else {
		Vec3 pos = m_ChildWidgets[0].GetRelativePosition();
		pos.y = m_RelativePosition.y;

		pos = m_ChildWidgets[1].GetRelativePosition();
		pos.y = m_RelativePosition.y;
		m_ChildWidgets[1].SetRelativePosition(pos);
	}

	if (m_ChildWidgets.size() < 2) {
		m_CalculatedSliderBoundsMin.set(0.0f, 0.0f, 0.0f);
		m_CalculatedSliderBoundsMax.set(0.0f, 0.0f, 0.0f);
	} else {
		m_CalculatedSliderBoundsMin = GetRelativePosition() + GetRelativeSize() + 0.05f;
		m_CalculatedSliderBoundsMax = m_CalculatedSliderBoundsMin + m_ChildWidgets[0].GetRelativeSize();

		m_ChildWidgets[0].SetRelativePosition(GetRelativePosition() + Vec3(GetRelativeSize().x + 0.05f, 0.0f, 0.0f));

		if (bFull) {
			m_ChildWidgets[1].SetRelativePosition(GetRelativePosition() + Vec3(GetRelativeSize().x + 0.05f, 0.0f, 0.0f));
		}
	}
}

/// UISlider::update_internal
void UISlider::update_internal(const float dt) {

	Super::update_internal(dt);

	if (HasFocus()) {
		if (m_ChildWidgets.size() > 1) {
			Vec3 curPos = m_ChildWidgets[1].GetRelativePosition();
			bool bMove = 0.0f;

			bool bFireEvent = false;
			const Input_t& input = g_pInputManager->get_input();
			if (input.IsArrowPressedOrDown(Input_t::Left) || input.IsKeyPressedOrDown('A') || input.m_LeftStick.x < -0.5f) {
				curPos.x -= 0.01f;
				bFireEvent = true;
			}

			if (input.IsArrowPressedOrDown(Input_t::Right) || input.IsKeyPressedOrDown('D') || input.m_LeftStick.x > 0.5f) {
				curPos.x += 0.01f;
				bFireEvent = true;
			}

			curPos.x = blk::clamp(curPos.x, m_CalculatedSliderBoundsMin.x, m_CalculatedSliderBoundsMax.x);
			m_ChildWidgets[1].SetRelativePosition(curPos);

			if (bFireEvent) {
				FireEvent();
			}
		}
	}
}


/// UISlider::GetNormalizedValue
float UISlider::GetNormalizedValue() {

	if (m_ChildWidgets.size() < 2) {
		return 0.0f;
	}

	const float sliderPos = m_ChildWidgets[1].GetRelativePosition().x;
	return (sliderPos - m_CalculatedSliderBoundsMin.x) / (m_CalculatedSliderBoundsMax.x - m_CalculatedSliderBoundsMin.x);
}


/// UISlider::SetNormalizedValue
void UISlider::SetNormalizedValue(const float newValue) {

	if (m_ChildWidgets.size() < 2) {
		return;
	}


	Vec3 relativePos = m_ChildWidgets[1].GetRelativePosition();
	relativePos.x = m_CalculatedSliderBoundsMin.x + (m_CalculatedSliderBoundsMax.x - m_CalculatedSliderBoundsMin.x) * newValue;

	m_ChildWidgets[1].SetRelativePosition(relativePos);
}
