#pragma once

#include "Audio/StaticVoice.h"
#include "Audio/StreamingVoice.h"

#define LOG_STATIC_VOICES(a, ...) ((void)0)
#define LOG_STREAMING_VOICES(a, ...) ((void)0)

namespace game
{

struct Frame;

}

namespace engine
{

constexpr float VolumeToPower(float fMasterVolume, float fSoundVolume, float fLocalVolume = 1.0f)
{
	// More natural-feeling volume controls
	float fVolume = fMasterVolume * fSoundVolume * fLocalVolume;
	return fVolume * fVolume;
}

class AudioManager : public IVoiceNotify
{
public:

	AudioManager();
	virtual ~AudioManager();

	void Update(const game::Frame& rFrame);

	IXAudio2SourceVoice* PlayOneShot(const game::Frame& rFrame, common::crc_t audioCrc, bool b3d, float fVolume, float fPitch = 1.0f);
	void XM_CALLCONV PlayOneShot3d(const game::Frame& rFrame, common::crc_t audioCrc, FXMVECTOR vecPosition, float fVolume, float fPitch = 1.0f);

	void PlayMusic(common::crc_t audioCrc);
	void SetNextMusicTrackCallback(std::function<common::crc_t()> callback);

	void Set3dSettings(float fCurveDistanceScaler, float fManualFadeStart, float fManualFadeEnd, float fManualFadeVolume);

	void ClearVoices();

	// IVoiceNotify
	virtual void OnBufferEnd() {}
	virtual void OnCriticalError();
	virtual void OnReset();
	virtual void OnUpdate() {};
	virtual void OnDestroyEngine() noexcept;
	virtual void OnTrim();
	virtual void GatherStatistics([[maybe_unused]] AudioStatistics& stats) const {}
	virtual void OnDestroyParent() noexcept;

	common::Timer mRealTime;

	mutable std::recursive_mutex mMusicStreamRecursiveMutex;

	std::unique_ptr<AudioEngine> mpAudioEngine;

private:

	void XM_CALLCONV Apply3dVolume(IXAudio2SourceVoice* pVoice, FXMVECTOR vecPosition, FXMVECTOR vecVelocity, float fVolume, float fPitch);

	int64_t miNextId = 1;
	std::unordered_map<sound_t, StaticVoice> mStaticVoices;

	XMVECTOR mVecListenerPosition {};
	X3DAUDIO_LISTENER mX3dAudioListener
	{
		.OrientFront = {0.0f, 0.0f, -1.0f},
		.OrientTop = {0.0f, -1.0f, 0.0f},
		.Position = {0.0f, 0.0f, 0.0f},
		.Velocity = {0.0f, 0.0f, 0.0f},
	};

	void UpdateMusicStreams(float fDeltaTime);

	float mfCurveDistanceScaler = 10.0f;
	float mfManualFadeStart = 0.0f;
	float mfManualFadeEnd = 150.0f;
	float mfManualFadeVolume = 0.05f;

	std::unique_ptr<StreamingVoice> mpCurrentMusicStream;
	std::vector<std::unique_ptr<StreamingVoice>> mPreviousStreams;
	std::function<common::crc_t()> mGetNextMusicTrack;
};

inline AudioManager* gpAudioManager = nullptr;

} // namespace engine
