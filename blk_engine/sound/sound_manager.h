/// sound_manager.h
///
/// 2017 blk

#pragma once

#include <array>
#if defined(_WIN32)
	#include <mmsystem.h>
	#include <mmreg.h>
	#include <XAudio2.h>
#endif
#include "resource_manager.h"

/// Off Windows there is no audio backend yet: `WaveFile` doesn't load and
/// `SoundManager` plays nothing - see the end of `sound_manager.cpp`. Sound
/// can't simply be left out of such a build, because reflection instantiates
/// every component and `SoundData`/`PlaySoundComponent` are components.

/// WaveFile
class WaveFile : public Resource {
public:
	WaveFile();
	~WaveFile();

	virtual TypeInfoType_t type() const { return BLK_TYPEINFO_SOUNDWAVE; }

	BYTE* GetWaveData() const { return m_pWaveDataBuffer; }
	DWORD GetWaveSize() const { return m_cbWaveSize; }

#if defined(_WIN32)
	WAVEFORMATEX* GetFormat() { return m_pWaveFormat; }
#endif

private:
	virtual bool load_internal();
	virtual void release_internal();

#if defined(_WIN32)
	HRESULT ReadMMIO();

	HRESULT Read(BYTE* pBuffer, DWORD dwSizeToRead, DWORD* pdwSizeRead);
	HRESULT ResetFile();

	WAVEFORMATEX* m_pWaveFormat;
	HMMIO m_hMMio;
	MMCKINFO m_ck;
	MMCKINFO m_ckRiff;
	DWORD m_dwSize;
#endif
	DWORD m_cbWaveSize;
	BYTE* m_pWaveDataBuffer;
};

/// SoundManager
class SoundManager {
public:
	SoundManager();
	~SoundManager();

	int PlayWave(WaveFile* const pWaveFile, const float volume, const bool bLoop = false);
	void StopWave(const int id);

	void Update();

	void SetFrequencyRatio(const float frequencyRatio);

	float GetMasterVolume() const { return m_MasterVolume; }
	void SetMasterVolume(const float newVolume);

private:
#if defined(_WIN32)
	IXAudio2* m_pXAudioEngine;
	IXAudio2MasteringVoice* m_pMasteringVoice;
#endif

	float m_FrequencyRatio;

#if defined(_WIN32)
	static const int MAX_VOICES = 32;
	struct VoiceData_t {
		VoiceData_t() :
			m_pVoice(nullptr),
			m_bInUse(false) {}

		IXAudio2SourceVoice* m_pVoice;
		bool m_bInUse;
	};
	std::array<VoiceData_t, MAX_VOICES> m_Voices;
#endif

	float m_MasterVolume;
	bool m_bInitialized;
};
