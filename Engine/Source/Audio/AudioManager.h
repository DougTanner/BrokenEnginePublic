#pragma once

#include "Audio/StaticVoice.h"
#include "Audio/StreamingVoice.h"

#define LOG_STATIC_VOICES(a, ...) ((void)0)
#define LOG_STREAMING_VOICES LOG // (a, ...) ((void)0)

namespace game
{

struct Frame;

}

namespace engine
{

enum class CrossFadeState : uint8_t
{
	kNone,
	kStarting,
	kActive
};

inline float CalculateVolume(float fMasterVolume, float fSoundVolume, float fLocalVolume = 1.0f)
{
	return std::pow(fMasterVolume, 2.0f) * std::pow(fSoundVolume, 2.0f) * fLocalVolume;
}

class AudioManager : public IVoiceNotify
{
public:

	AudioManager();
	virtual ~AudioManager();

	void Update(const game::Frame& rFrame);
	IXAudio2SourceVoice* PlayOneShot(common::crc_t audioCrc, bool b3d, float fVolume, float fPitch = 1.0f);
	void XM_CALLCONV PlayOneShot3d(common::crc_t audioCrc, FXMVECTOR vecPosition, float fVolume, float fPitch = 1.0f);
	void SetMusicPlaylist(const std::vector<common::crc_t>& playlist);

	// IVoiceNotify
	virtual void OnBufferEnd() {}
	virtual void OnCriticalError();
	virtual void OnReset();
	virtual void OnUpdate();
	virtual void OnDestroyEngine() noexcept;
	virtual void OnTrim();
	virtual void GatherStatistics([[maybe_unused]] AudioStatistics& stats) const {}
	virtual void OnDestroyParent() noexcept;

	common::Timer mRealTime;

	mutable std::mutex mMusicStreamMutex;

	std::unique_ptr<AudioEngine> mpAudioEngine;

private:

	void XM_CALLCONV Apply3d(IXAudio2SourceVoice* pVoice, FXMVECTOR vecPosition, FXMVECTOR vecVelocity, float fVolume, float fPitch);

	int64_t miNextId = 1;
	std::vector<StaticVoice> mStaticVoices;

	// 3D positioning listener
	XMVECTOR mVecListenerPosition {};
	X3DAUDIO_LISTENER mX3dAudioListener
	{
		.OrientFront = {0.0f, 0.0f, -1.0f},
		.OrientTop = {0.0f, -1.0f, 0.0f},
		.Position = {0.0f, 0.0f, 0.0f},
		.Velocity = {0.0f, 0.0f, 0.0f},
	};

	// Music streaming
	void UpdateCrossFade(float fDeltaTime);

	int64_t miMusicIndex = 0;
	std::unique_ptr<StreamingVoice> mpCurrentMusicStream;
	std::unique_ptr<StreamingVoice> mpNextMusicStream;
	CrossFadeState mCrossFadeState = CrossFadeState::kNone;
	float mfCrossFadeProgress = 0.0f;
	std::vector<common::crc_t> mMusicPlaylist;
};

inline AudioManager* gpAudioManager = nullptr;

} // namespace engine
