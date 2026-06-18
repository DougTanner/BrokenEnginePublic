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

	void PlayOneShot(const game::Frame& rFrame, common::crc_t uiAudioCrc, bool b3d, float fVolume, float fPitch = 1.0f, float fPitchRange = 0.0f);
	void XM_CALLCONV PlayOneShot3d(const game::Frame& rFrame, common::crc_t uiAudioCrc, FXMVECTOR vecPosition, float fVolume, float fPitch = 1.0f, float fPitchRange = 0.0f);

	void PlayMusic(common::crc_t uiAudioCrc);
	void SetNextMusicTrackCallback(std::function<common::crc_t()> callback);

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

} // namespace engine

#endif // BT_CLIENT
