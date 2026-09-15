/// debug_component.h
///
/// 2018 blk

#pragma once

class Model;

/// DebugSphereCollision
class DebugSphereCollision : public GameComponent {
	BLK_DECLARE_COMPONENT(DebugSphereCollision, GameComponent);

private:
	virtual void enable_internal(const bool bEnable) override;
	virtual void update_internal(const float DeltaTime) override;

	Model* m_pCollisionModel;
	RenderObject m_render_object;
};
