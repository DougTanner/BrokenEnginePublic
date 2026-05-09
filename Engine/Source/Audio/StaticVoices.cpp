#include "StaticVoices.h"

#if defined(BT_CLIENT)

#include "Memory/MemoryManager.h"

#include "Game.h"
#include "Frame/Collections/Players/Players.h"
#include "Ui/SoundSettingsWrappersBase.h"

namespace engine
{

void StaticVoices::Init(AudioEngine* pAudioEngine, const int64_t* piMasteringVoiceChannels)
{
	mpAudioEngine = pAudioEngine;
	mpiMasteringVoiceChannels = piMasteringVoiceChannels;
	mRandomEngine.TimeSeed();
	mVoices.reserve(kiMaxStaticVoices);
}

IXAudio2SourceVoice* StaticVoices::PlayOneShot([[maybe_unused]] const game::Frame& rFrame, common::crc_t uiAudioCrc, bool b3d, float fVolume, float fPitch, float fPitchRange)
{
	ASSERT(rFrame.interpolate.frameFlags & FrameFlags::kPostRender);

	if (rFrame.interpolate.frameFlags & FrameFlags::kRecalculated)
	{
		return nullptr;
	}

	if (mbSuspended.load(std::memory_order_acquire))
	{
		return nullptr;
	}

	std::lock_guard<std::recursive_mutex> lock(mOneShotRecursiveMutex);

	// Heap: AllocateVoice creates an XAudio2 source voice that persists until playback ends.
	// XAudio2 owns the allocation internally, so workbuffer and pre-allocation are not possible.
	ScopedSuppressAllocationTracking suppress;

	if (mpAudioEngine == nullptr || !mpAudioEngine->IsAudioDevicePresent()) [[unlikely]]
	{
		return nullptr;
	}

	IXAudio2SourceVoice* pIXAudio2SourceVoice = nullptr;
	if (!StaticVoice::LoadXAudio2SourceVoice(mpAudioEngine, pIXAudio2SourceVoice, uiAudioCrc, true, b3d))
	{
		return nullptr;
	}

	if (fPitchRange > 0.0f)
	{
		fPitch += common::Random(fPitchRange, mRandomEngine);
	}

	if (!b3d)
	{
		CHECK_HRESULT(pIXAudio2SourceVoice->SetVolume(VolumeToPower(gMasterVolume.Get(), gSoundVolume.Get(), fVolume)));
	}
	CHECK_HRESULT(pIXAudio2SourceVoice->SetFrequencyRatio(fPitch));
	CHECK_HRESULT(pIXAudio2SourceVoice->Start(0, XAUDIO2_COMMIT_NOW));
	return pIXAudio2SourceVoice;
}

void XM_CALLCONV StaticVoices::PlayOneShot3d([[maybe_unused]] const game::Frame& rFrame, common::crc_t uiAudioCrc, FXMVECTOR vecPosition, float fVolume, float fPitch, float fPitchRange)
{
	ASSERT(rFrame.interpolate.frameFlags & FrameFlags::kPostRender);

	if (mpAudioEngine == nullptr || !mpAudioEngine->IsAudioDevicePresent()) [[unlikely]]
	{
		return;
	}

#if BT_AUDIO_PRIORITY_CULL
	// Hard-cull inaudible one-shots before grabbing a voice slot. Uses the same curve
	// as the persistent priority pass so behaviour is consistent across both paths.
	float fCullDistance = common::Distance(vecPosition, mVecListenerPosition);
	if (ComputeAttenuatedVolume(fCullDistance, fVolume) < kfCullVolume)
	{
		return;
	}
#endif

	std::lock_guard<std::recursive_mutex> lock(mOneShotRecursiveMutex);

	if (fPitchRange > 0.0f)
	{
		fPitch += common::Random(fPitchRange, mRandomEngine);
	}

	IXAudio2SourceVoice* pIXAudio2SourceVoice = PlayOneShot(rFrame, uiAudioCrc, true, fVolume, fPitch);
	if (pIXAudio2SourceVoice != nullptr)
	{
		Apply3dVolume(pIXAudio2SourceVoice, vecPosition, XMVectorZero(), fVolume, fPitch);
	}
}

void StaticVoices::Set3dSettings(float fCurveDistanceScaler, float fManualFadeStart, float fManualFadeEnd, float fManualFadeVolume)
{
	mfCurveDistanceScaler = fCurveDistanceScaler;
	mfManualFadeStart = fManualFadeStart;
	mfManualFadeEnd = fManualFadeEnd;
	mfManualFadeVolume = fManualFadeVolume;
}

void StaticVoices::ReturnVoiceToPool(common::crc_t audioCrc, IXAudio2SourceVoice* pVoice)
{
	pVoice->Stop(0, XAUDIO2_COMMIT_NOW);
	mPooledVoices.push_back({audioCrc, pVoice});
}

IXAudio2SourceVoice* StaticVoices::AcquireVoiceFromPool(common::crc_t audioCrc)
{
	for (size_t i = 0; i < mPooledVoices.size(); ++i)
	{
		if (mPooledVoices[i].mAudioCrc == audioCrc)
		{
			IXAudio2SourceVoice* pVoice = mPooledVoices[i].mpVoice;
			if (i < mPooledVoices.size() - 1)
			{
				mPooledVoices[i] = mPooledVoices.back();
			}
			mPooledVoices.pop_back();
			return pVoice;
		}
	}
	return nullptr;
}

void StaticVoices::ClearPool()
{
	for (PooledVoice& rPooled : mPooledVoices)
	{
		DestroyXAudio2SourceVoice(rPooled.mpVoice);
	}
	mPooledVoices.clear();
}

void StaticVoices::UpdateLifecycle(const game::Frame& rFrame, float fDeltaTime)
{
	ASSERT(rFrame.interpolate.frameFlags & FrameFlags::kPostRender);

	// Skipping replay ticks also skips the consumption of mbSkipNextInvalidation below;
	// that is intentional — if a device-reset callback raises the flag mid-storm we want
	// the first real post-storm tick to still suppress invalidation of the cleared voices.
	if (rFrame.interpolate.frameFlags & FrameFlags::kRecalculated)
	{
		return;
	}

	const SoundsInterpolate& rSoundsInterpolate = rFrame.interpolate.sounds;
	const SoundsPostRender& rSoundsPostRender = rFrame.postRender.sounds;

	bool bSkipInvalidation = mbSkipNextInvalidation;
	mbSkipNextInvalidation = false;

	// Fade out and stop invalid static voices
	if (!bSkipInvalidation)
	{
		for (int64_t i = 0; i < static_cast<int64_t>(mVoices.size());)
		{
			StaticVoice& rVoice = mVoices.at(i);

			bool bValid = rSoundsInterpolate.idToIndexMap.contains(rVoice.mId);
			if (bValid)
			{
				++i;
				continue;
			}

			bool bDestroy = false;
#if BT_AUDIO_PRIORITY_CULL
			if (rVoice.mFlags & StaticVoiceFlags::kInactive)
			{
				// Inactive voices have mpVoice == nullptr — no audio to fade out, and
				// the pool already received the voice when we deactivated. Drop the
				// entry directly without touching kFadingOut / mfFadeOutVolume.
				bDestroy = true;
			}
			else
#endif
			{
				if (rVoice.mfVolume <= 0.0f)
				{
					bDestroy = true;
				}
				if (rVoice.mFlags & StaticVoiceFlags::kFadingOut)
				{
					rVoice.mfFadeOutVolume -= fDeltaTime / rVoice.mfFadeOutTime;
					if (rVoice.mfFadeOutVolume <= 0.0f)
					{
						bDestroy = true;
					}
				}
				else
				{
					rVoice.mFlags.Set(StaticVoiceFlags::kFadingOut);
					rVoice.mfFadeOutVolume = 1.0f;
				}
			}

			if (bDestroy)
			{
				if (rVoice.mpVoice != nullptr)
				{
					ReturnVoiceToPool(rVoice.mAudioCrc, rVoice.mpVoice);
					rVoice.mpVoice = nullptr;
				}
				if (i < static_cast<int64_t>(mVoices.size()) - 1)
				{
					mVoices.at(i) = std::move(mVoices.back());
				}
				mVoices.pop_back();
			}
			else
			{
				++i;
			}
		}
	}

#if BT_AUDIO_PRIORITY_CULL
	// Priority + cull pass: rank candidates by attenuated volume so the closest /
	// loudest sounds win the kiMaxStaticVoices slots. Out-of-range candidates (below
	// the hysteresis floor) are skipped entirely; matching active voices get
	// deactivated below. Replaces the prior first-come-first-served add loop.
	const int64_t iSoundCount = rSoundsPostRender.iCount;
	common::Workbuffer& rWorkbuffer = common::gpThreadLocal->mWorkbuffer;
	// pActivatedIds is allocated unconditionally so the deactivation pass can run
	// every frame even when iSoundCount==0 (drains stale active voices).
	auto pActivatedIdsScratch = rWorkbuffer.PushBuffer<sound_t*>(kiMaxStaticVoices * static_cast<int64_t>(sizeof(sound_t)));
	sound_t* pActivatedIds = pActivatedIdsScratch;
	int64_t iActivatedCount = 0;
	static constexpr float kfDeactivateFloor = 0.8f * kfCullVolume;

	if (iSoundCount > 0)
	{
		struct PriorityEntry { float fAttenuated; int64_t iSoundIndex; };
		// RAII handle keeps the workbuffer frame alive; raw pointer below avoids
		// std::sort deducing _RanIt from the ScopedWorkbufferAllocation type.
		auto pPriorityScratch = rWorkbuffer.PushBuffer<PriorityEntry*>(iSoundCount * static_cast<int64_t>(sizeof(PriorityEntry)));
		PriorityEntry* pPriority = pPriorityScratch;

		// Hysteresis floor: candidates below 0.8 * cull never enter the priority list,
		// so a sound oscillating around the boundary doesn't toggle slots each frame.
		int64_t iCandidateCount = 0;
		for (int64_t i = 0; i < iSoundCount; ++i)
		{
			sound_t id = rSoundsPostRender.puiIds[i];
			int64_t iIndex = rSoundsInterpolate.IdToIndex(id);
			float fSoundVolume = rSoundsInterpolate.pfVolumes[iIndex];
			float fDistance = common::Distance(rSoundsInterpolate.pVecPositions[iIndex], mVecListenerPosition);
			float fAttenuated = ComputeAttenuatedVolume(fDistance, fSoundVolume);
			if (fAttenuated < kfDeactivateFloor)
			{
				continue;
			}
			pPriority[iCandidateCount++] = {fAttenuated, i};
		}

		std::sort(pPriority, pPriority + iCandidateCount, [](const PriorityEntry& rEntryA, const PriorityEntry& rEntryB)
		{
			return rEntryA.fAttenuated > rEntryB.fAttenuated;
		});

		// Walk in priority order; allocate or sync up to kiMaxStaticVoices slots.
		const int64_t iSlotCap = std::min(iCandidateCount, kiMaxStaticVoices);
		for (int64_t iSlot = 0; iSlot < iSlotCap; ++iSlot)
		{
			float fAttenuated = pPriority[iSlot].fAttenuated;
			int64_t i = pPriority[iSlot].iSoundIndex;
			sound_t id = rSoundsPostRender.puiIds[i];
			int64_t iIndex = rSoundsInterpolate.IdToIndex(id);
			float fSoundVolume = rSoundsInterpolate.pfVolumes[iIndex];
			float fPitch = rSoundsInterpolate.pfPitches[iIndex];
			XMVECTOR vecPosition = rSoundsInterpolate.pVecPositions[iIndex];
			XMVECTOR vecVelocity = rSoundsInterpolate.pVecVelocities[iIndex];

			StaticVoice* pExistingVoice = nullptr;
			for (StaticVoice& rExisting : mVoices)
			{
				if (rExisting.mId == id)
				{
					pExistingVoice = &rExisting;
					break;
				}
			}

			if (pExistingVoice != nullptr)
			{
				// Sync data first — covers both the active-sync case and the pre-Start
				// sync needed before reactivating, since SOA values may have drifted
				// while the voice was inactive.
				pExistingVoice->mfVolume = fSoundVolume;
				pExistingVoice->mfPitch = fPitch;
				pExistingVoice->mVecPosition = vecPosition;
				pExistingVoice->mVecVelocity = vecVelocity;

				if ((pExistingVoice->mFlags & StaticVoiceFlags::kInactive) && fAttenuated >= kfCullVolume)
				{
					// Reactivate: re-acquire from the per-crc pool, restart the source voice,
					// and ramp volume in via mfFadeOutVolume to mask the click.
					common::crc_t uiCrc = pExistingVoice->mAudioCrc;
					IXAudio2SourceVoice* pVoice = AcquireVoiceFromPool(uiCrc);
					if (pVoice == nullptr)
					{
						if (!StaticVoice::LoadXAudio2SourceVoice(mpAudioEngine, pVoice, uiCrc, false, true))
						{
							continue;
						}
					}
					pExistingVoice->mpVoice = pVoice;
					pExistingVoice->mfFadeOutVolume = 0.0f;
					pExistingVoice->mFlags.Clear(StaticVoiceFlags::kInactive);
					// SetVolume(0) before Start() prevents an audible click — pooled voices
					// retain whatever volume Apply3dVolume last set on them, which may be
					// loud. Apply3dVolume in the next UpdateVolumes will re-establish the
					// ramped volume.
					CHECK_HRESULT(pVoice->SetVolume(0.0f));
					CHECK_HRESULT(pVoice->Start(0, XAUDIO2_COMMIT_NOW));
					LOG(kAudio, kDebug, "voice REACTIVATED id={} dist={} atten={}", id, common::Wb(common::Distance(vecPosition, mVecListenerPosition), 2), common::Wb(fAttenuated, 4));
				}

				// Mark as activated only when the voice is genuinely audible this frame.
				// An existing-active voice in the hysteresis band [kfDeactivateFloor, kfCullVolume)
				// is intentionally NOT marked, so the deactivation pass picks it up.
				if (fAttenuated >= kfCullVolume)
				{
					pActivatedIds[iActivatedCount++] = id;
				}
				continue;
			}

			// New voice: only spawn if attenuated volume is above the activate threshold.
			// Anything in [kfDeactivateFloor, kfCullVolume) waits for an existing voice
			// to drop it before allocating a slot.
			if (fAttenuated < kfCullVolume)
			{
				continue;
			}
			if (static_cast<int64_t>(mVoices.size()) >= kiMaxStaticVoices)
			{
				LOG(kAudio, kDebug, "Max static voices reached ({}), deferring add", kiMaxStaticVoices);
				continue;
			}

			common::crc_t uiCrc = rSoundsInterpolate.puiCrcs[iIndex];
			IXAudio2SourceVoice* pVoice = AcquireVoiceFromPool(uiCrc);
			if (pVoice == nullptr)
			{
				if (!StaticVoice::LoadXAudio2SourceVoice(mpAudioEngine, pVoice, uiCrc, false, true))
				{
					continue;
				}
			}
			float fFadeOutTime = rSoundsInterpolate.pfFadeOutTimes[iIndex];
			mVoices.push_back(StaticVoice(pVoice, id, fSoundVolume, fPitch, fFadeOutTime, vecPosition, vecVelocity, uiCrc));
			pActivatedIds[iActivatedCount++] = id;
		}
	}

	// Deactivation pass: any active voice not given a slot this frame (past cap or
	// below the activate threshold) releases its XAudio2 voice back to the pool. The
	// mVoices entry stays so the next frame can reactivate when the listener moves
	// closer. Runs unconditionally so iSoundCount==0 also drains stale voices.
	// The owner-driven Remove path (kFadingOut) is handled in the invalidation block.
	for (StaticVoice& rVoice : mVoices)
	{
		if (rVoice.mFlags & StaticVoiceFlags::kInactive)
		{
			continue;
		}
		if (rVoice.mFlags & StaticVoiceFlags::kFadingOut)
		{
			continue;
		}
		bool bActivated = false;
		for (int64_t i = 0; i < iActivatedCount; ++i)
		{
			if (pActivatedIds[i] == rVoice.mId)
			{
				bActivated = true;
				break;
			}
		}
		if (!bActivated)
		{
			rVoice.mFlags.Set(StaticVoiceFlags::kInactive);
			if (rVoice.mpVoice != nullptr)
			{
				ReturnVoiceToPool(rVoice.mAudioCrc, rVoice.mpVoice);
				rVoice.mpVoice = nullptr;
			}
			LOG(kAudio, kDebug, "voice DEACTIVATED id={} dist={}", rVoice.mId, common::Wb(common::Distance(rVoice.mVecPosition, mVecListenerPosition), 2));
		}
	}

	// Fade-in ramp for voices whose mfFadeOutVolume is below 1.0 — used after
	// reactivation to mask the Start() click. 150ms full ramp.
	static constexpr float kfFadeInTime = 0.15f;
	for (StaticVoice& rVoice : mVoices)
	{
		if (rVoice.mFlags & StaticVoiceFlags::kInactive)
		{
			continue;
		}
		if (rVoice.mFlags & StaticVoiceFlags::kFadingOut)
		{
			continue;
		}
		if (rVoice.mfFadeOutVolume < 1.0f)
		{
			rVoice.mfFadeOutVolume = std::min(1.0f, rVoice.mfFadeOutVolume + fDeltaTime / kfFadeInTime);
		}
	}
#else // BT_AUDIO_PRIORITY_CULL
	// Legacy add-loop: first-come-first-served, no priority sort, no cull,
	// no deactivation. Drops new voices when the 128-cap is hit.
	for (int64_t i = 0; i < rSoundsPostRender.iCount; ++i)
	{
		sound_t id = rSoundsPostRender.puiIds[i];
		int64_t iIndex = rSoundsInterpolate.IdToIndex(id);
		float fVolume = rSoundsInterpolate.pfVolumes[iIndex];

		StaticVoice* pExistingVoice = nullptr;
		for (StaticVoice& rExisting : mVoices)
		{
			if (rExisting.mId == id)
			{
				pExistingVoice = &rExisting;
				break;
			}
		}

		if (pExistingVoice != nullptr)
		{
			pExistingVoice->mfVolume = fVolume;
			pExistingVoice->mfPitch = rSoundsInterpolate.pfPitches[iIndex];
			pExistingVoice->mVecPosition = rSoundsInterpolate.pVecPositions[iIndex];
			pExistingVoice->mVecVelocity = rSoundsInterpolate.pVecVelocities[iIndex];
			continue;
		}

		if (fVolume <= 0.0f)
		{
			continue;
		}
		if (static_cast<int64_t>(mVoices.size()) >= kiMaxStaticVoices)
		{
			LOG(kAudio, kDebug, "Max static voices reached ({}), skipping", kiMaxStaticVoices);
			continue;
		}

		common::crc_t uiCrc = rSoundsInterpolate.puiCrcs[iIndex];
		IXAudio2SourceVoice* pVoice = AcquireVoiceFromPool(uiCrc);
		if (pVoice == nullptr)
		{
			if (!StaticVoice::LoadXAudio2SourceVoice(mpAudioEngine, pVoice, uiCrc, false, true))
			{
				continue;
			}
		}
		float fPitch = rSoundsInterpolate.pfPitches[iIndex];
		float fFadeOutTime = rSoundsInterpolate.pfFadeOutTimes[iIndex];
		XMVECTOR vecPosition = rSoundsInterpolate.pVecPositions[iIndex];
		XMVECTOR vecVelocity = rSoundsInterpolate.pVecVelocities[iIndex];
		mVoices.push_back(StaticVoice(pVoice, id, fVolume, fPitch, fFadeOutTime, vecPosition, vecVelocity, uiCrc));
	}
	(void)fDeltaTime; // unused in legacy path
#endif // BT_AUDIO_PRIORITY_CULL
}

void StaticVoices::UpdateListenerPosition(const game::Frame& rFrame)
{
	XMVECTOR vecListenerPos = XMVectorZero();
	XMVECTOR vecListenerVel = XMVectorZero();
	std::optional<int64_t> oClientIndex = game::gpGame->ClientPlayerIndex(*rFrame.postRender.pPlayers);
	if (oClientIndex.has_value())
	{
		int64_t iClientIndex = oClientIndex.value();
		vecListenerPos = rFrame.interpolate.pPlayers->pVecPositions[iClientIndex];
		vecListenerVel = rFrame.postRender.pPlayers->pVecVelocities[iClientIndex];
	}
	mVecListenerPosition = vecListenerPos;
	XMFLOAT3A f3Position {};
	XMStoreFloat3A(&f3Position, vecListenerPos);
	XMFLOAT3A f3Velocity {};
	XMStoreFloat3A(&f3Velocity, vecListenerVel);
	mX3dAudioListener.OrientFront = {0.0f, 0.0f, -1.0f};
	mX3dAudioListener.OrientTop = {0.0f, -1.0f, 0.0f};
	mX3dAudioListener.Position = f3Position;
	mX3dAudioListener.Velocity = f3Velocity;

	float fEyeHeight = game::gpCamera->mfCameraEyeHeight;
	const XMFLOAT4& rArea = game::gpCamera->f4RenderVisibleArea;
	float fVisibleWidth = rArea.z - rArea.x;
	if (mfReferenceVisibleWidth == 0.0f && fVisibleWidth > 0.0f)
	{
		mfReferenceVisibleWidth = fVisibleWidth;
	}
	mfChannelBleedT = std::clamp((fEyeHeight - game::Camera::kfCameraEyeHeightDefault) / game::Camera::kfCameraEyeHeightDefault, 0.0f, 1.0f);
	float fScale = (mfReferenceVisibleWidth > 0.0f) ? (fVisibleWidth / mfReferenceVisibleWidth) : 1.0f;
	static constexpr float kfAudibleDistanceMultiplier = 2.0f;
	mfEffectiveFadeEnd = kfAudibleDistanceMultiplier * mfManualFadeEnd * std::max(1.0f, fScale);
}

void StaticVoices::UpdateVolumes()
{
	for (const StaticVoice& rVoice : mVoices)
	{
#if BT_AUDIO_PRIORITY_CULL
		if (rVoice.mFlags & StaticVoiceFlags::kInactive)
		{
			continue; // No XAudio2 voice attached — skip mix.
		}
#endif
		Apply3dVolume(rVoice.mpVoice, rVoice.mVecPosition, rVoice.mVecVelocity, rVoice.mfFadeOutVolume * rVoice.mfVolume, rVoice.mfPitch);
	}
}

void StaticVoices::Clear(bool bNullVoicesBeforeDestroy)
{
	if (bNullVoicesBeforeDestroy)
	{
		for (StaticVoice& rVoice : mVoices)
		{
			rVoice.mpVoice = nullptr;
		}
	}
	else
	{
		for (StaticVoice& rVoice : mVoices)
		{
			DestroyXAudio2SourceVoice(rVoice.mpVoice);
			rVoice.mpVoice = nullptr;
		}
	}
	mVoices.clear();

	if (bNullVoicesBeforeDestroy)
	{
		for (PooledVoice& rPooled : mPooledVoices)
		{
			rPooled.mpVoice = nullptr;
		}
		mPooledVoices.clear();
	}
	else
	{
		ClearPool();
	}
}

float StaticVoices::ComputeAttenuatedVolume(float fDistance, float fSoundVolume) const
{
	// Natural distance curve from fSoundVolume at fadeStart to 0 at fadeEnd (no audible
	// floor — that's a separate Apply3dVolume concern). Used by the priority/cull pass
	// so out-of-range sounds compute as effectively-silent and get culled.
	float fDistanceVolume = fSoundVolume;
	if (fDistance >= mfEffectiveFadeEnd)
	{
		fDistanceVolume = 0.0f;
	}
	else if (fDistance >= mfManualFadeStart)
	{
		float fPercent = std::clamp((fDistance - mfManualFadeStart) / (mfEffectiveFadeEnd - mfManualFadeStart), 0.0f, 1.0f);
		fDistanceVolume = (1.0f - fPercent) * fSoundVolume;
	}
	static constexpr float kfMinHeightVolumeScale = 0.75f;
	return fDistanceVolume * std::lerp(1.0f, kfMinHeightVolumeScale, mfChannelBleedT);
}

void XM_CALLCONV StaticVoices::Apply3dVolume(IXAudio2SourceVoice* pVoice, FXMVECTOR vecPosition, FXMVECTOR vecVelocity, float fVolume, float fPitch)
{
	XMFLOAT3A f3Position {};
	XMStoreFloat3A(&f3Position, vecPosition);
	XMFLOAT3A f3Velocity {};
	XMStoreFloat3A(&f3Velocity, vecVelocity);

	X3DAUDIO_EMITTER x3dAudioEmitter
	{
		.OrientFront = {0.0f, 0.0f, 1.0f},
		.Position = f3Position,
		.Velocity = f3Velocity,
		.ChannelCount = 1,
		.CurveDistanceScaler = mfCurveDistanceScaler,
		.DopplerScaler = 1.0f,
	};

	int64_t iMasteringVoiceChannels = *mpiMasteringVoiceChannels;

	FLOAT32 pfMatrixCoefficients[XAUDIO2_MAX_AUDIO_CHANNELS] {};
	FLOAT32 pfDelayTimes[XAUDIO2_MAX_AUDIO_CHANNELS] {};
	X3DAUDIO_DSP_SETTINGS x3dAudioDspSettings
	{
		.pMatrixCoefficients = pfMatrixCoefficients,
		.pDelayTimes = pfDelayTimes,
		.SrcChannelCount = 1,
		.DstChannelCount = static_cast<UINT32>(iMasteringVoiceChannels),
	};
	X3DAUDIO_HANDLE& rX3dAudioHandle = mpAudioEngine->Get3DHandle();
	X3DAudioCalculate(rX3dAudioHandle, &mX3dAudioListener, &x3dAudioEmitter, X3DAUDIO_CALCULATE_MATRIX | X3DAUDIO_CALCULATE_LPF_DIRECT | X3DAUDIO_CALCULATE_DOPPLER, &x3dAudioDspSettings);

	if (iMasteringVoiceChannels >= 2 && mfChannelBleedT > 0.0f)
	{
		// 0.5 cap would collapse L and R to identical (L+R)/2 — full mono. Stay below that.
		static constexpr float kfMaxChannelBleedFactor = 0.25f;
		float fBleedFactor = kfMaxChannelBleedFactor * mfChannelBleedT;
		float fLeft = pfMatrixCoefficients[0];
		float fRight = pfMatrixCoefficients[1];
		pfMatrixCoefficients[0] = (1.0f - fBleedFactor) * fLeft + fBleedFactor * fRight;
		pfMatrixCoefficients[1] = (1.0f - fBleedFactor) * fRight + fBleedFactor * fLeft;
	}

	CHECK_HRESULT(pVoice->SetOutputMatrix(mpAudioEngine->GetMasterVoice(), 1, static_cast<UINT32>(iMasteringVoiceChannels), x3dAudioDspSettings.pMatrixCoefficients));

#if BT_AUDIO_PRIORITY_CULL
	// Apply custom volume with distance-based attenuation. The natural curve goes to
	// zero at fade-end (used for cull priority); the audible mix clamps to mfManualFadeVolume
	// so in-range sounds never drop below an audible floor.
	float fDistance = common::Distance(vecPosition, mVecListenerPosition);
	float fAttenuated = ComputeAttenuatedVolume(fDistance, fVolume);
	static constexpr float kfMinHeightVolumeScale = 0.75f;
	float fAudibleFloor = mfManualFadeVolume * std::lerp(1.0f, kfMinHeightVolumeScale, mfChannelBleedT);
	float fDistanceVolume = std::max(fAttenuated, fAudibleFloor);
#else
	// Legacy mix: lerp from fVolume toward mfManualFadeVolume across the fade band,
	// flat-clamp to mfManualFadeVolume past fade-end. No cull contract.
	float fDistance = common::Distance(vecPosition, mVecListenerPosition);
	float fDistanceVolume = fVolume;
	if (fDistance >= mfEffectiveFadeEnd)
	{
		fDistanceVolume = mfManualFadeVolume;
	}
	else if (fDistance >= mfManualFadeStart)
	{
		float fPercent = std::clamp((fDistance - mfManualFadeStart) / (mfEffectiveFadeEnd - mfManualFadeStart), 0.0f, 1.0f);
		fDistanceVolume = (1.0f - fPercent) * fVolume + fPercent * mfManualFadeVolume;
	}
	static constexpr float kfMinHeightVolumeScale = 0.75f;
	fDistanceVolume *= std::lerp(1.0f, kfMinHeightVolumeScale, mfChannelBleedT);
#endif

	float fFinalPower = VolumeToPower(gMasterVolume.Get(), gSoundVolume.Get(), fDistanceVolume);
	CHECK_HRESULT(pVoice->SetVolume(fFinalPower));
	CHECK_HRESULT(pVoice->SetFrequencyRatio(x3dAudioDspSettings.DopplerFactor * fPitch));
}

} // namespace engine

#endif // defined(BT_CLIENT)
