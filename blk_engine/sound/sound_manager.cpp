/// sound_manager.cpp
///
/// 2017 blk

#include "blk_core.h"
#include "entity_header.h"
#include "sound_manager.h"

//#include "level_component.h"

/// WaveFile::WaveFile
WaveFile::WaveFile() :
	m_pWaveFormat(nullptr),
	m_hMMio(nullptr),
	m_ck(MMCKINFO()),
	m_ckRiff(MMCKINFO()),
	m_dwSize(0),
	m_pWaveDataBuffer(nullptr),
	m_cbWaveSize(0) {

}

/// WaveFile::~WaveFile
WaveFile::~WaveFile() {}

/// WaveFile::load_internal
bool WaveFile::load_internal() {
	HRESULT hr;

	const LPSTR pFileName = (LPSTR)full_file_name().c_str();
	m_hMMio = mmioOpen(pFileName, nullptr, MMIO_ALLOCBUF | MMIO_READ);

	hr = ReadMMIO();
	blk::error_check(SUCCEEDED(hr), "WaveFile::Load_Internal() - Failed to load wave %s", full_file_name().c_str());

	hr = ResetFile();
	blk::error_check(SUCCEEDED(hr), "WaveFile::Load_Internal() - Failed to load wave %s", full_file_name().c_str());

	// After the reset, the size of the wav file is m_ck.cksize so store it now
	m_dwSize = m_ck.cksize;

	// Read the sample data into memory
	m_cbWaveSize = m_dwSize;
	m_pWaveDataBuffer = new BYTE[m_cbWaveSize];

	hr = Read(m_pWaveDataBuffer, m_cbWaveSize, &m_cbWaveSize);
	blk::error_check(SUCCEEDED(hr), "WaveFile::Load_Internal() - Failed to load wave %s", full_file_name().c_str());

	return true;
}

/// WaveFile::release_internal
void WaveFile::release_internal() {
	if (m_hMMio != nullptr) {
		mmioClose(m_hMMio, 0);
		m_hMMio = nullptr;
	}

	delete[] m_pWaveDataBuffer;
	m_pWaveDataBuffer = nullptr;
}

/// WaveFile::ReadMMIO
HRESULT	WaveFile::ReadMMIO() {
	MMCKINFO ckIn;           // chunk info. for general use.
	PCMWAVEFORMAT pcmWaveFormat;  // Temp PCM structure to load in.

	memset(&ckIn, 0, sizeof(ckIn));

	m_pWaveFormat = nullptr;

	MMRESULT MR = mmioDescend(m_hMMio, &m_ckRiff, NULL, 0);

	blk::error_check(MR == 0, "WaveFile::ReadMMIO() - Error");
	blk::error_check(m_ckRiff.ckid == FOURCC_RIFF && m_ckRiff.fccType == mmioFOURCC('W', 'A', 'V', 'E'), "WaveFile::ReadMMIO() - Error");

	// Search the input file for for the 'fmt ' chunk.
	ckIn.ckid = mmioFOURCC('f', 'm', 't', ' ');

	MR = mmioDescend(m_hMMio, &ckIn, &m_ckRiff, MMIO_FINDCHUNK);
	blk::error_check(MR == 0, "WaveFile::ReadMMIO() - Error");

	// Expect the 'fmt' chunk to be at least as large as <PCMWAVEFORMAT>;
	// if there are extra parameters at the end, we'll ignore them
	blk::error_check(ckIn.cksize >= (LONG)sizeof(PCMWAVEFORMAT), "WaveFile::ReadMMIO() - Error");

	LONG amtRead = mmioRead(m_hMMio, (HPSTR)&pcmWaveFormat, sizeof(pcmWaveFormat));
	blk::error_check(amtRead == sizeof(pcmWaveFormat), "WaveFile::ReadMMIO() - Error");

	// Allocate the waveformatex, but if its not pcm format, read the next
	// word, and thats how many extra bytes to allocate.
	if (pcmWaveFormat.wf.wFormatTag == WAVE_FORMAT_PCM) {
		m_pWaveFormat = reinterpret_cast<WAVEFORMATEX*>(new CHAR[sizeof(WAVEFORMATEX)]);
		blk::error_check(m_pWaveFormat != nullptr, "WaveFile::ReadMMIO() - Error");

		// Copy the bytes from the pcm structure to the waveformatex structure
		memcpy(m_pWaveFormat, &pcmWaveFormat, sizeof(pcmWaveFormat));
		m_pWaveFormat->cbSize = 0;
	} else
	{
		// Read in length of extra bytes.
		WORD cbExtraBytes = 0L;
		amtRead = mmioRead(m_hMMio, (CHAR*)&cbExtraBytes, sizeof(WORD));
		blk::error_check(amtRead == sizeof(WORD), "WaveFile::ReadMMIO() - Error");

		m_pWaveFormat = reinterpret_cast<WAVEFORMATEX*>(new CHAR[sizeof(WAVEFORMATEX) + cbExtraBytes]);
		blk::error_check(m_pWaveFormat != nullptr, "WaveFile::ReadMMIO() - Error");

		// Copy the bytes from the pcm structure to the waveformatex structure
		memcpy(m_pWaveFormat, &pcmWaveFormat, sizeof(pcmWaveFormat));
		m_pWaveFormat->cbSize = cbExtraBytes;

		// Now, read those extra bytes into the structure, if cbExtraAlloc != 0.
		amtRead = mmioRead(m_hMMio, (CHAR*)(((BYTE*)&(m_pWaveFormat->cbSize)) + sizeof(WORD)), cbExtraBytes);
		blk::error_check(amtRead == cbExtraBytes, "WaveFile::ReadMMIO() - Error");
	}

	MR = mmioAscend(m_hMMio, &ckIn, 0);
	blk::error_check(MR == 0, "WaveFile::ReadMMIO() - Error");

	return S_OK;
}

/// WaveFile::Read
HRESULT WaveFile::Read(BYTE* pBuffer, DWORD dwSizeToRead, DWORD* pdwSizeRead) {

	MMIOINFO mmioinfoIn; // current status of m_hMMio

	if (m_hMMio == nullptr) {
		return CO_E_NOTINITIALIZED;
	}

	blk::error_check(pBuffer != nullptr && pdwSizeRead != nullptr, "WaveFile::Read() - Error");

	*pdwSizeRead = 0;

	MMRESULT MR = mmioGetInfo(m_hMMio, &mmioinfoIn, 0);
	blk::error_check(MR == 0, "WaveFile::Read() - Error");


	UINT cbDataIn = dwSizeToRead;
	if (cbDataIn > m_ck.cksize) {
		cbDataIn = m_ck.cksize;
	}

	m_ck.cksize -= cbDataIn;

	for (DWORD cT = 0; cT < cbDataIn; cT++) {
		// Copy the bytes from the io to the buffer.
		if (mmioinfoIn.pchNext == mmioinfoIn.pchEndRead)
		{
			MR = mmioAdvance(m_hMMio, &mmioinfoIn, MMIO_READ);
			blk::error_check(MR == 0, "WaveFile::Read() - Error");
			blk::error_check(mmioinfoIn.pchNext != mmioinfoIn.pchEndRead, "WaveFile::Read() - Error");
		}

		// Actual copy.
		*((BYTE*)pBuffer + cT) = *((BYTE*)mmioinfoIn.pchNext);
		mmioinfoIn.pchNext++;
	}

	MR = mmioSetInfo(m_hMMio, &mmioinfoIn, 0);
	blk::error_check(MR == 0, "WaveFile::Read() - Error");

	*pdwSizeRead = cbDataIn;

	return S_OK;

}

/// WaveFile::ResetFile
HRESULT WaveFile::ResetFile() {

	if (m_hMMio == nullptr) {
		return CO_E_NOTINITIALIZED;
	}

	// Seek to the data
	LONG MR = mmioSeek(m_hMMio, m_ckRiff.dwDataOffset + sizeof(FOURCC), SEEK_SET);
	blk::error_check(MR != -1, "WaveFile::ResetFile()");

	// Search the input file for the 'data' chunk.
	m_ck.ckid = mmioFOURCC('d', 'a', 't', 'a');
	MR = mmioDescend(m_hMMio, &m_ck, &m_ckRiff, MMIO_FINDCHUNK);
	blk::error_check(MR == 0, "WaveFile::ResetFile()");

	return S_OK;
}

/// SoundManager::SoundManager
SoundManager::SoundManager() :
	m_pXAudioEngine(nullptr),
	m_pMasteringVoice(nullptr),
	m_FrequencyRatio(1.f),
	m_MasterVolume(1.f),
	m_bInitialized(false) {
	blk::log("Creating Audio Engine");

	m_bInitialized = false;

	blk::error_check(
		XAudio2Create(&m_pXAudioEngine),
		"SoundManager::SoundManager() - Failed to create XAudio2"
	);

	if (!blk::warn_check(
		m_pXAudioEngine->CreateMasteringVoice(&m_pMasteringVoice),
		"SoundManager::SoundManager() - Failed to create a mastering voice")) {
		return;
	}

	m_FrequencyRatio = 1.0f;
	m_bInitialized = true;

	m_MasterVolume = 1.0f;
}

/// SoundManager::~SoundManager
SoundManager::~SoundManager() {
	if (m_bInitialized == false) {
		return;
	}

	for (auto& voice : m_Voices) {
		if (voice.m_bInUse == false) {
			continue;
		}

		voice.m_pVoice->DestroyVoice();
		voice.m_pVoice = nullptr;
		voice.m_bInUse = false;
	}

	m_pMasteringVoice->DestroyVoice();
	m_pMasteringVoice = nullptr;
	m_pXAudioEngine->Release();
	CoUninitialize();

	blk::log("Audio Engine destroyed");
}

/// SoundManager::PlayWave
int SoundManager::PlayWave(WaveFile* const pWaveFile, const float inVolume, const bool bLoop) {
	if (m_bInitialized == false) {
		return -1;
	}

	const float finalVolume = inVolume * LevelComponent::GetGlobalVolumeScale();
	for (size_t i = 0; i < m_Voices.size(); i++) {
		VoiceData_t& voice = m_Voices[i];
		if (voice.m_bInUse == true) {
			continue;
		}

		blk::error_check(
			voice.m_pVoice == nullptr,
			"SoundManager::PlayWave() - Non null voice is in use."
		);

		voice.m_bInUse = true;

		// Create the source voice
		WAVEFORMATEX* const pwfx = pWaveFile->GetFormat();
		blk::error_check(
			m_pXAudioEngine->CreateSourceVoice(&voice.m_pVoice, pwfx),
			"SoundManager::PlayWave() - Failed to create a voice"
		);

		// Submit the wave sample data using an XAUDIO2_BUFFER structure
		XAUDIO2_BUFFER buffer = { 0 };
		buffer.pAudioData = pWaveFile->GetWaveData();//files[index].m_pWaveDataBuffer;
		buffer.Flags = XAUDIO2_END_OF_STREAM;  // tell the source voice not to expect any data after this buffer
		buffer.AudioBytes = pWaveFile->GetWaveSize();//files[index].m_cbWaveSize;

		if (bLoop) {
			buffer.LoopBegin = 0;
			buffer.LoopCount = XAUDIO2_LOOP_INFINITE;
		}

		HRESULT hr = voice.m_pVoice->SubmitSourceBuffer(&buffer);
		blk::error_check(SUCCEEDED(hr), "SoundManager::PlayWave() - Failed to submit audio buffer");

		voice.m_pVoice->SetVolume(finalVolume);
		voice.m_pVoice->SetFrequencyRatio(m_FrequencyRatio);
		blk::error_check(
			voice.m_pVoice->Start(0),
			"SoundManager::PlayWave() - Failed to submit start voice"
		);
		return (int32_t)i;
	}

	return -1;
}

/// SoundManager::StopWave
void SoundManager::StopWave(const int id) {
	if (!blk::warn_check(id >= 0 && id < MAX_VOICES,"SoundManager::StopWave() - Called with invalid wave id")) {
		return;
	}

	VoiceData_t* const pVoice = &m_Voices[id];
	pVoice->m_pVoice->Stop();
	pVoice->m_pVoice->DestroyVoice();
	pVoice->m_pVoice = nullptr;

	pVoice->m_bInUse = false;
}

/// SoundManager::Update
void SoundManager::Update() {
	if (m_bInitialized == false) {
		return;
	}

	for (auto& voice: m_Voices) {
		if (voice.m_bInUse == false) {
			continue;
		}

		XAUDIO2_VOICE_STATE state;
		voice.m_pVoice->GetState(&state);

		if (state.BuffersQueued <= 0) {
			voice.m_pVoice->Stop();
			voice.m_bInUse = false;
			voice.m_pVoice->DestroyVoice();
			voice.m_pVoice = nullptr;
		}
	}
}

/// SoundManager::SetFrequencyRatio
void SoundManager::SetFrequencyRatio(const float frequencyRatio) {

	if (m_bInitialized == false) {
		return;
	}

	m_FrequencyRatio = frequencyRatio;

	for (auto& voice : m_Voices) {
		if (voice.m_bInUse == false) {
			continue;
		}
		voice.m_pVoice->SetFrequencyRatio(m_FrequencyRatio);
	}
}

/// SoundManager::SetMasterVolume
void SoundManager::SetMasterVolume(const float newVolume) {
	m_MasterVolume = newVolume;
	m_pMasteringVoice->SetVolume(newVolume);
}