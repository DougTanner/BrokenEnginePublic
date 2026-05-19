#pragma once

#if defined(BT_CLIENT)

namespace DirectX
{
class AudioEngine;
}

namespace game
{

struct Frame;

}

namespace engine
{

inline constexpr int64_t kiMaxStaticVoices = 128;

// FadeOutPool: bonus capacity above kiMaxStaticVoices for voices ramping out
// gracefully. Lets a budget-eviction free its primary slot to the new sound
// immediately while the displaced voice keeps playing through a fade. When at
// cap, mark attempts DEBUG_BREAK and skip; the deactivation pass retries the
// next frame once a slot frees.
inline constexpr int64_t kiMaxFadeOutPool = 128;

// Below this attenuated-volume threshold a sound is considered inaudible and is
// culled (one-shots: never spawned; persistent: voice released, entry kept).
// Hysteresis: candidacy floor is 0.8 * kfCullVolume so a sound right at the
// boundary doesn't thrash between active and inactive each frame.
inline constexpr float kfCullVolume = 0.01f;

class StaticVoices
{
public:

	void Init(AudioEngine* pAudioEngine, const int64_t* piMasteringVoiceChannels);

	IXAudio2SourceVoice* PlayOneShot(const game::Frame& rFrame, common::crc_t uiAudioCrc, bool b3d, float fVolume, float fPitch = 1.0f, float fPitchRange = 0.0f);
	void XM_CALLCONV PlayOneShot3d(const game::Frame& rFrame, common::crc_t uiAudioCrc, FXMVECTOR vecPosition, float fVolume, float fPitch = 1.0f, float fPitchRange = 0.0f);

	void UpdateLifecycle(const game::Frame& rFrame, float fDeltaTime);
	void UpdateListenerPosition(const game::Frame& rFrame);
	void UpdateVolumes();

	void Clear(bool bNullVoicesBeforeDestroy);

	int64_t GetVoiceCount() const { return static_cast<int64_t>(mVoices.size()); }

	void SetSuspended(bool bSuspended) { mbSuspended.store(bSuspended, std::memory_order_release); }

	void SkipNextInvalidation() { mbSkipNextInvalidation = true; }

private:

	void XM_CALLCONV Apply3dVolume(IXAudio2SourceVoice* pVoice, FXMVECTOR vecPosition, FXMVECTOR vecVelocity, float fVolume, float fPitch);

	float ComputeAttenuatedVolume(float fDistance, float fSoundVolume) const;

	struct PooledVoice
	{
		common::crc_t mAudioCrc;
		IXAudio2SourceVoice* mpVoice;
	};

	void ReturnVoiceToPool(common::crc_t audioCrc, IXAudio2SourceVoice* pVoice);
	IXAudio2SourceVoice* AcquireVoiceFromPool(common::crc_t audioCrc);
	void ClearPool();

	AudioEngine* mpAudioEngine = nullptr;
	const int64_t* mpiMasteringVoiceChannels = nullptr;

	std::recursive_mutex mOneShotRecursiveMutex; // PlayOneShot3d() re-enters via PlayOneShot()
	common::RandomEngine mRandomEngine;
	std::atomic<bool> mbSuspended = false;

	bool mbSkipNextInvalidation = false;
	std::vector<StaticVoice> mVoices;
	std::vector<PooledVoice> mPooledVoices;

	XMVECTOR mVecListenerPosition {};
	X3DAUDIO_LISTENER mX3dAudioListener
	{
		.OrientFront = {0.0f, 0.0f, -1.0f},
		.OrientTop = {0.0f, -1.0f, 0.0f},
		.Position = {0.0f, 0.0f, 0.0f},
		.Velocity = {0.0f, 0.0f, 0.0f},
	};

	float mfCurveDistanceScaler = 10.0f;
	float mfManualFadeVolume = 0.15f;

	float mfEffectiveFadeStart = 0.0f;
	float mfEffectiveFadeEnd = 150.0f;
};

} // namespace engine

#endif // BT_CLIENT
