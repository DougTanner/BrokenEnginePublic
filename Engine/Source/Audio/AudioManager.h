#pragma once

#if defined(BT_CLIENT)

namespace game
{

struct Frame;

}

namespace engine
{

enum class AudioManagerFlags : uint8_t
{
	// True once a live output graph produced a pinned format with real native channels. False for a
	// silent-start engine (no device at construction, or a startup pin that left the graph silent):
	// mPinnedOutputFormat holds no valid channel count yet, so Update recovery must reset to the device
	// default to discover native channels before it can pin.
	kPinnedFormatValid = 0x01,

	// Silent-start recovery log-once gate: emits the "device absent" warning a single time until a device
	// is found (re-armed on recovery for a future loss). The frame counter throttling the probe frequency
	// is separate (miSilentRecoveryFrameCounter) — a permanently-deviceless machine would otherwise warn +
	// full-reset every frame.
	kSilentRecoveryLogged = 0x02,

	// Set only across the silent-recovery pin/fallback Reset window, where the default Reset already brought
	// the graph live so DirectXTK's Reset synchronously delivers the migration OnCriticalError. That
	// notification is expected here, so OnCriticalError early-outs while this bit is set instead of logging a
	// false critical error and clearing voices. Single-threaded main-thread synchronous window.
	kExpectedResetInProgress = 0x04,
};
using AudioManagerFlags_t = common::Flags<AudioManagerFlags>;

class StaticVoices;
class StreamingVoices;

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

	void SkipNextStaticVoiceInvalidation();

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

private:

	std::wstring GetEndpointId(IMMDevice* pDevice);
	void CreateAudioEngineForEndpoint(const std::wstring& rEndpointId);
	std::wstring InitializeAudioEndpoint();
	void InitializeAudioSubsystems(const std::wstring& rSelectedDeviceId);
	WAVEFORMATEX MakePinnedOutputFormat(WORD uiChannels) const;
	void ConfigureLiveGraph(const std::wstring& rSelectedDeviceId);
	void FinishDeviceReset();
	void AttemptSilentEngineRecovery();
	void CacheMasteringVoiceChannels();
	void ClearVoices(bool bNullVoicesBeforeDestroy);

	common::Timer mRealTime;
	std::unique_ptr<AudioEngine> mpAudioEngine;
	// Declared after mpAudioEngine so both are destroyed first: they retain its raw pointer.
	std::unique_ptr<StaticVoices> mpStaticVoices;
	std::unique_ptr<StreamingVoices> mpStreamingVoices;

	std::atomic<bool> mbSuspended = false;
	std::atomic<bool> mbClearVoicesRequested = false;
	int64_t miMasteringVoiceChannels = 0;

	// Mastering voice pinned to this format (native channels, kiMasteringSampleRate). Reused by the
	// device-reset path so the rate pin survives Reset — DirectXTK does not cache the ctor wfx.
	WAVEFORMATEX mPinnedOutputFormat {};

	// State bits (pinned-format validity, silent-recovery log-once, expected-reset suppression); see the
	// AudioManagerFlags enumerators for each bit's contract.
	AudioManagerFlags_t mFlags;

	// Silent-start recovery throttle counter: gates probe frequency (kiSilentRecoveryRetryFrames) so a
	// permanently-deviceless machine does not warn + full-reset every frame.
	int64_t miSilentRecoveryFrameCounter = 0;
};

inline AudioManager* gpAudioManager = nullptr;

} // namespace engine

#endif // BT_CLIENT
