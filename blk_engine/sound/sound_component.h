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
	class WaveFile* m_pWaveFile;
	float m_Radius;
	float m_Volume;

	bool m_bLooping;
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
	float m_MinStartDelay;
	float m_MaxStartDelay;
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
