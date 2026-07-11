#pragma once

#if defined(BT_CLIENT)

#include "StaticVoice.h"

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

	void PlayOneShot(const game::Frame& rFrame, common::crc_t uiAudioCrc, bool b3d, float fVolume, float fPitch = 1.0f, float fPitchRange = 0.0f);
	void XM_CALLCONV PlayOneShot3d(const game::Frame& rFrame, common::crc_t uiAudioCrc, FXMVECTOR vecPosition, float fVolume, float fPitch = 1.0f, float fPitchRange = 0.0f);

	void UpdateLifecycle(const game::Frame& rFrame, float fDeltaTime);
	void UpdateListenerPosition();
	void UpdateVolumes();

	void Clear(bool bNullVoicesBeforeDestroy);

	int64_t GetVoiceCount() const { return static_cast<int64_t>(mVoices.size()); }

	void SkipNextInvalidation() { mbSkipNextInvalidation = true; }

private:

	// Unlocked one-shot body shared by both public paths; callers hold mOneShotMutex.
	// rfPitch is in/out: the pitch-randomization result flows back so the 3D path can
	// pass the randomized ratio on to Apply3dVolume.
	IXAudio2SourceVoice* PlayOneShotLocked(common::crc_t uiAudioCrc, bool b3d, float fVolume, float& rfPitch, float fPitchRange);

	void XM_CALLCONV Apply3dVolume(IXAudio2SourceVoice* pVoice, FXMVECTOR vecPosition, FXMVECTOR vecVelocity, float fVolume, float fPitch);

	float ComputeAttenuatedVolume(float fDistance, float fSoundVolume) const;

	// UpdateLifecycle passes, run in fixed order each frame.
	void InvalidationPass(const SoundsInterpolate& rSoundsInterpolate);
	void PriorityPass(const SoundsInterpolate& rSoundsInterpolate, const SoundsPostRender& rSoundsPostRender);
	void DeactivationPass();
	void AdvanceFadeOut(float fDeltaTime);
	void AdvanceFadeIn(float fDeltaTime);

	struct PooledVoice
	{
		common::crc_t mAudioCrc;
		IXAudio2SourceVoice* mpVoice;
	};
	struct AcquiredVoice
	{
		IXAudio2SourceVoice* pVoice = nullptr;
		bool bFromPool = false;
	};

	void ReturnVoiceToPool(common::crc_t audioCrc, IXAudio2SourceVoice* pVoice);
	IXAudio2SourceVoice* AcquireVoiceFromPool(common::crc_t audioCrc);
	AcquiredVoice AcquireOrLoadVoice(common::crc_t uiAudioCrc);
	void ClearPool();
	void BeginFadeOut(StaticVoice& rVoice);
	void EndFadeOut(StaticVoice& rVoice);
	void ActivateVoice(StaticVoice& rVoice, IXAudio2SourceVoice* pVoice);
	void RetireVoice(StaticVoice& rVoice);

	AudioEngine* mpAudioEngine = nullptr;
	const int64_t* mpiMasteringVoiceChannels = nullptr;

	std::mutex mOneShotMutex; // 3D path locks once and calls the unlocked PlayOneShotLocked helper (no re-entry)
	common::RandomEngine mRandomEngine;

	bool mbSkipNextInvalidation = false;
	std::vector<StaticVoice> mVoices;
	std::vector<PooledVoice> mPooledVoices;
	int64_t miFadeOutCount = 0; // Maintained count of mVoices entries flagged kFadingOut; kept in sync at every Set/Clear so the budget checks stay O(1). Reset in Clear().

	// Listener/fade state below (through mfEffectiveFadeEnd) is written only by main-thread
	// UpdateListenerPosition (audio step) and read LOCK-FREE by the one-shot path
	// (PlayOneShot3d → ComputeAttenuatedVolume / Apply3dVolume's X3DAudioCalculate), which game
	// sim code reaches from worker threads inside Dispatch() regions. Race-free purely by
	// temporal exclusion: the main loop strictly sequences ClientUpdate (all dispatch workers
	// join) → Render → AudioManager::Update (Main.cpp), so no worker is alive when the audio
	// step writes. The same sequencing is why UpdateLifecycle / UpdateVolumes / Clear may
	// touch mVoices / mPooledVoices without taking mOneShotMutex.
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
