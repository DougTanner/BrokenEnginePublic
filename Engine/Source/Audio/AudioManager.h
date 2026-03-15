#pragma once

#if defined(BT_CLIENT)

#define LOG_STATIC_VOICES(a, ...) ((void)0)
#define LOG_STREAMING_VOICES(a, ...) ((void)0)

namespace game
{

struct Frame;

}

namespace engine
{

constexpr int64_t kiMaxStaticVoices = 128;

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
	~AudioManager() override;

	void Update(const game::Frame* pFrame);

	IXAudio2SourceVoice* PlayOneShot(const game::Frame& rFrame, common::crc_t audioCrc, bool b3d, float fVolume, float fPitch = 1.0f, float fPitchRange = 0.0f);
	void XM_CALLCONV PlayOneShot3d(const game::Frame& rFrame, common::crc_t audioCrc, FXMVECTOR vecPosition, float fVolume, float fPitch = 1.0f, float fPitchRange = 0.0f);

	void PlayMusic(common::crc_t audioCrc);
	void SetNextMusicTrackCallback(std::function<common::crc_t()> callback);

	void Set3dSettings(float fCurveDistanceScaler, float fManualFadeStart, float fManualFadeEnd, float fManualFadeVolume);

	void ClearVoices();

	// IVoiceNotify
	void OnBufferEnd() override {}
	void OnCriticalError() override;
	void OnReset() override;
	void OnUpdate() override {}
	void OnDestroyEngine() noexcept override;
	void OnTrim() override;
	void GatherStatistics([[maybe_unused]] AudioStatistics& rStats) const override {}
	void OnDestroyParent() noexcept override;

	common::Timer mRealTime;

	mutable std::recursive_mutex mMusicStreamRecursiveMutex;

	std::unique_ptr<AudioEngine> mpAudioEngine;

private:

	void XM_CALLCONV Apply3dVolume(IXAudio2SourceVoice* pVoice, FXMVECTOR vecPosition, FXMVECTOR vecVelocity, float fVolume, float fPitch);

	common::RandomEngine mRandomEngine;

	int64_t miNextId = 1;
	std::atomic<bool> mbClearVoicesRequested = false;
	std::vector<StaticVoice> mStaticVoices;

	XMVECTOR mVecListenerPosition {};
	X3DAUDIO_LISTENER mX3dAudioListener
	{
		.OrientFront = {0.0f, 0.0f, -1.0f},
		.OrientTop = {0.0f, -1.0f, 0.0f},
		.Position = {0.0f, 0.0f, 0.0f},
		.Velocity = {0.0f, 0.0f, 0.0f},
	};

	void UpdateMusicStreams(float fDeltaTime);
	void ClearStreamingVoices();

	float mfCurveDistanceScaler = 10.0f;
	float mfManualFadeStart = 0.0f;
	float mfManualFadeEnd = 150.0f;
	float mfManualFadeVolume = 0.05f;
	int64_t miMasteringVoiceChannels = 0;

	std::unique_ptr<StreamingVoice> mpCurrentMusicStream;
	std::vector<std::unique_ptr<StreamingVoice>> mPreviousStreams;
	std::function<common::crc_t()> mGetNextMusicTrack;
};

inline AudioManager* gpAudioManager = nullptr;

} // namespace engine

#endif // BT_CLIENT
