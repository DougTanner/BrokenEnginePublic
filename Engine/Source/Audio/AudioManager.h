#pragma once

#include "Audio/Voice.h"

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

class AudioManager : public IVoiceNotify
{
public:

	AudioManager();
	virtual ~AudioManager();

	void Update(const game::Frame& rFrame);
	IXAudio2SourceVoice* PlayOneShot(common::crc_t audioCrc, bool b3d, float fVolume, float fPitch = 1.0f);
	void XM_CALLCONV PlayOneShot(common::crc_t audioCrc, FXMVECTOR vecPosition, float fVolume, float fPitch = 1.0f);
	void SetMusicPlaylist(const std::vector<common::crc_t>& playlist);

	// IVoiceNotify
	virtual void OnBufferEnd();
	virtual void OnCriticalError();
	virtual void OnReset();
	virtual void OnUpdate();
	virtual void OnDestroyEngine() noexcept;
	virtual void OnTrim();
	virtual void GatherStatistics([[maybe_unused]] AudioStatistics& stats) const {}
	virtual void OnDestroyParent() noexcept;

	std::unique_ptr<AudioEngine> mpAudioEngine;
	common::Timer mRealTime;

private:

	bool LoadVoice(IXAudio2SourceVoice*& rpVoice, common::crc_t audioCrc, bool bOneShot, bool b3d);
	void XM_CALLCONV Apply3d(IXAudio2SourceVoice* pVoice, FXMVECTOR vecPosition, FXMVECTOR vecVelocity, float fVolume, float fPitch);

	bool LoadMusicVoice(std::unique_ptr<Voice>& rpStream, common::crc_t audioCrc);

	int64_t miNextId = 1;
	std::vector<Voice> mVoices;

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

	mutable std::mutex mMusicStreamMutex;
	int64_t miMusicIndex = 0;
	std::unique_ptr<Voice> mpCurrentMusicStream;
	std::unique_ptr<Voice> mpNextMusicStream;
	CrossFadeState mCrossFadeState = CrossFadeState::kNone;
	float mfCrossFadeProgress = 0.0f;
	std::vector<common::crc_t> mMusicPlaylist;
};

inline AudioManager* gpAudioManager = nullptr;

} // namespace engine
