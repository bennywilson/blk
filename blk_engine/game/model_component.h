/// model_component.h
///
/// 2016 blk

#pragma once

#include "render_component.h"
#include "model.h"

class Animation;
class Model;

/// RenderComponent
class StaticModelComponent : public RenderComponent {
	BLK_DECLARE_COMPONENT(StaticModelComponent, RenderComponent);

public:
	virtual ~StaticModelComponent();

	void set_model(const class Model* pModel) { m_model = pModel; }
	const Model* model() const { return m_model; }

	virtual void editor_change(const std::string& propertyName);

protected:
	virtual void enable_internal(const bool isEnabled) override;
	virtual void update_internal(const float DeltaTime) override;

private:
	const Model* m_model;
};

/// AnimComponent
class AnimComponent : public GameComponent {
	friend class SkeletalModelComponent;

	BLK_DECLARE_COMPONENT(AnimComponent, GameComponent);

public:
	const String& animation_name() const { return m_animation_name; }

private:
	String m_animation_name;
	Animation* m_animation;
	float m_time_scale;
	bool m_is_looping;
	std::vector<AnimEvent> m_anim_events;

	float m_current_animation_time;
	String m_desired_next_animation;
	float m_desired_next_anim_blend_length;
};


/// SkeletalModelComponent
class SkeletalModelComponent : public RenderComponent {
	BLK_DECLARE_COMPONENT(SkeletalModelComponent, RenderComponent);

public:
	virtual ~SkeletalModelComponent();

	void set_model(class Model* const pModel);
	const Model* model() const { return m_model; }

	virtual void editor_change(const std::string& propertyName);

	int GetBoneIndex(const String& boneName);
	BoneMatrix_t GetBoneRefMatrix(const int index);

	bool GetBoneWorldPosition(const String& boneName, Vec3& outWorldPosition);
	bool GetBoneWorldMatrix(const String& boneName, BoneMatrix_t& boneMatrix);
	std::vector<BoneMatrix_t>& GetFinalBoneMatrices() { return m_BindToLocalSpaceMatrices; }
	const std::vector<BoneMatrix_t>& GetFinalBoneMatrices() const { return m_BindToLocalSpaceMatrices; }

	void SetAnimationTimeScaleMultiplier(const String& animationName, const f32 factor);

	// Animation
	void PlayAnimation(const String& AnimationName, const f32 BlendLength, bool bRestartIfAlreadyPlaying, const String desiredNextAnimation = String::EmptyString, const f32 desiredNextAnimBlendLength = 0.0f);
	bool IsPlaying(const String& AnimationName) const;

	bool HasFinishedAnimation() const { return IsTransitioningAnimations() == false && (m_CurrentAnimation == -1 || m_Animations[m_CurrentAnimation].m_animation == NULL || m_Animations[m_CurrentAnimation].m_current_animation_time >= m_Animations[m_CurrentAnimation].m_animation->GetLengthInSeconds()); }
	bool IsTransitioningAnimations() const { return m_CurrentAnimation != -1 && m_NextAnimation != -1; }

	bool is_breakable() const {
		return m_is_breakable;
	}

	float GetCurAnimTimeSeconds() const {
		if (m_CurrentAnimation == -1) {
			return -1.0f;
		}
		return m_Animations[m_CurrentAnimation].m_current_animation_time;
	}
	float GetNormalizedAnimTime() const { return GetCurAnimTimeSeconds() / GetCurAnimLengthSeconds(); }
	float GetCurAnimLengthSeconds() const {
		if (m_CurrentAnimation == -1 || m_Animations[m_CurrentAnimation].m_animation == nullptr) {
			return -1.0f;
		}
		return m_Animations[m_CurrentAnimation].m_animation->GetLengthInSeconds();
	}

	const String* GetCurAnimationName() const;
	const String* GetNextAnimationName() const;

	void RegisterAnimEventListener(IAnimEventListener* const pListener);
	void UnregisterAnimEventListener(IAnimEventListener* const pListener);

	void RegisterSyncSkelModel(SkeletalModelComponent* const pSkelModel);
	void UnregisterSyncSkelModel(SkeletalModelComponent* const pSkelModel);

protected:
	virtual void enable_internal(const bool isEnabled) override;
	virtual void update_internal(const float DeltaTime) override;

	std::vector<IAnimEventListener*> m_AnimEventListeners;

	// Editor
	class Model* m_model;
	std::vector<AnimComponent> m_Animations;

	// Game
	std::vector<BoneMatrix_t> m_BindToLocalSpaceMatrices;

	i32 m_CurrentAnimation;
	i32 m_NextAnimation;
	f32 m_BlendStartTime;
	f32 m_BlendLength;

	std::vector<f32> m_AnimationTimeScaleMultipliers;

	std::vector<SkeletalModelComponent*> m_SyncedSkelModels;
	SkeletalModelComponent* m_pSyncParent;

	bool m_is_breakable = false;

	// Debug
	i32 m_DebugAnimIdx;
	f32 m_DebugAnimTime;
};

/// FlingPhysicsComponent
class FlingPhysicsComponent : public GameComponent {
	BLK_DECLARE_COMPONENT(FlingPhysicsComponent, GameComponent);

public:
	void ResetToStartPos() {
		if (m_bOwnerStartSet) {
			SetOwnerPosition(m_OwnerStartPos);
			SetOwnerRotation(m_OwnerStartRotation);
		}
	}

protected:
	virtual void enable_internal(const bool isEnabled) override;
	virtual void update_internal(const float DeltaTime) override;

private:
	// Editor
	Vec3 m_min_linear_vel;
	Vec3 m_max_linear_vel;
	float m_MinAngularSpeed;
	float m_MaxAngularSpeed;
	Vec3 m_gravity;

	// Run time
	Vec3 m_OwnerStartPos;
	Quat4 m_OwnerStartRotation;

	Vec3 m_velocity;
	Vec3 m_rotation_axis;

	float m_cur_rotation_angle;
	float m_rotation_speed;

	float m_FlingStartTime;

	bool m_bOwnerStartSet;
};
