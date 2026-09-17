/// sound_component.h
///
/// 2017 blk

#pragma once

/// SoundData
class SoundData : public GameComponent {
	BLK_DECLARE_COMPONENT(SoundData, GameComponent);

public:
	virtual ~SoundData();

	void PlaySoundAtPosition(const Vec3& soundPosition);
	void StopSound();

	virtual void editor_change(const std::string& propertyName) override;

private:
	// Data
	BLK_PROPERTY()
	class WaveFile* m_pWaveFile;
	BLK_PROPERTY()
	float m_Radius;
	BLK_PROPERTY()
	float m_Volume;

	BLK_PROPERTY()
	bool m_bLooping;
	BLK_PROPERTY()
	bool m_bDebugPlaySound;

	// Runtime
	int m_SoundId;
};

/// PlaySoundComponent
class PlaySoundComponent : public GameComponent {
	BLK_DECLARE_COMPONENT(PlaySoundComponent, GameComponent);

protected:
	virtual void enable_internal(const bool isEnabled) override;
	virtual void update_internal(const float DeltaTime) override;

private:
	// Data
	BLK_PROPERTY()
	float m_MinStartDelay;
	BLK_PROPERTY()
	float m_MaxStartDelay;
	BLK_PROPERTY()
	std::vector<SoundData> m_SoundData;

	// Runtime
	float m_TimeToPlay;
};

/// PlayRandomSound
inline void PlayRandomSound(std::vector<SoundData>& soundData, const Vec3& pos = Vec3::zero) {
	if (soundData.empty()) {
		return;
	}

	soundData[rand() % soundData.size()].PlaySoundAtPosition(pos);
}
