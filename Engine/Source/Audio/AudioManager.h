#pragma once

#if defined(BT_CLIENT)

#include "StaticVoices.h"
#include "StreamingVoices.h"

namespace game
{

struct Frame;

}

namespace engine
{

class AudioManager : public IVoiceNotify
{
public:

	AudioManager();
	~AudioManager() override;

	void Update(const game::Frame* pFrame);

	IXAudio2SourceVoice* PlayOneShot(const game::Frame& rFrame, common::crc_t uiAudioCrc, bool b3d, float fVolume, float fPitch = 1.0f, float fPitchRange = 0.0f);
	void XM_CALLCONV PlayOneShot3d(const game::Frame& rFrame, common::crc_t uiAudioCrc, FXMVECTOR vecPosition, float fVolume, float fPitch = 1.0f, float fPitchRange = 0.0f);

	void PlayMusic(common::crc_t uiAudioCrc);
	void SetNextMusicTrackCallback(std::function<common::crc_t()> callback);

	void Set3dSettings(float fCurveDistanceScaler, float fManualFadeStart, float fManualFadeEnd, float fManualFadeVolume);
	void SkipNextStaticVoiceInvalidation() { mStaticVoices.SkipNextInvalidation(); }

	void ClearVoices();

	void Suspend();
	void Resume();

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

	std::unique_ptr<AudioEngine> mpAudioEngine;

	StaticVoices mStaticVoices;
	StreamingVoices mStreamingVoices;

private:

	std::atomic<bool> mbSuspended = false;
	std::atomic<bool> mbClearVoicesRequested = false;
	std::atomic<bool> mbClearStreamingVoicesRequested = false;
	int64_t miMasteringVoiceChannels = 0;
};

inline AudioManager* gpAudioManager = nullptr;

inline void DestroyXAudio2SourceVoice(IXAudio2SourceVoice*& rpVoice)
{
	if (rpVoice != nullptr)
	{
		rpVoice->Stop(0, XAUDIO2_COMMIT_NOW);
		rpVoice->FlushSourceBuffers();
		if (gpAudioManager->mpAudioEngine != nullptr)
		{
			std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
			gpAudioManager->mpAudioEngine->DestroyVoice(rpVoice);
			std::chrono::steady_clock::duration elapsed = std::chrono::steady_clock::now() - start;
			if (elapsed > std::chrono::milliseconds(100))
			{
				LOG(kAudio, kWarning, "DestroyXAudio2SourceVoice took {}ms", std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count());
			}
		}
		rpVoice = nullptr;
	}
}

} // namespace engine

#endif // BT_CLIENT
