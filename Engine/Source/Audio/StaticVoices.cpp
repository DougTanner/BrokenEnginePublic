#include "StaticVoices.h"

#if defined(BT_CLIENT)

#include "AudioUtility.h"
#include "Game.h"
#include "Ui/SoundSettingsWrappersBase.h"

namespace engine
{

void StaticVoices::Init(AudioEngine* pAudioEngine, const int64_t* piMasteringVoiceChannels)
{
	mpAudioEngine = pAudioEngine;
	mpiMasteringVoiceChannels = piMasteringVoiceChannels;
	mRandomEngine.TimeSeed();
	mVoices.reserve(kiMaxStaticVoices + kiMaxFadeOutPool);
}

void StaticVoices::PlayOneShot([[maybe_unused]] const game::Frame& rFrame, common::crc_t uiAudioCrc, bool b3d, float fVolume, float fPitch, float fPitchRange)
{
	ASSERT(rFrame.interpolate.frameFlags & FrameFlags::kPostRender);

	if (rFrame.interpolate.frameFlags & FrameFlags::kRecalculated)
	{
		return;
	}

	if (mbSuspended.load(std::memory_order_acquire))
	{
		return;
	}

	// Silent one-shots cull at the door so they never burn a 128-cap voice slot,
	// load a buffer, or take the lock.
	if (fVolume <= 0.0f)
	{
		return;
	}

	std::lock_guard<std::mutex> lock(mOneShotMutex);
	PlayOneShotLocked(uiAudioCrc, b3d, fVolume, fPitch, fPitchRange);
}

IXAudio2SourceVoice* StaticVoices::PlayOneShotLocked(common::crc_t uiAudioCrc, bool b3d, float fVolume, float& rfPitch, float fPitchRange)
{
	// Heap: AllocateVoice creates an XAudio2 source voice that persists until playback ends.
	// XAudio2 owns the allocation internally, so workbuffer and pre-allocation are not possible.
	ScopedSuppressAllocationTracking suppress;

	if (mpAudioEngine == nullptr || !mpAudioEngine->IsAudioDevicePresent()) [[unlikely]]
	{
		return nullptr;
	}

	IXAudio2SourceVoice* pIXAudio2SourceVoice = nullptr;
	LoadVoiceFlags_t loadFlags(LoadVoiceFlags::kOneShot);
	loadFlags.Set(LoadVoiceFlags::k3d, b3d);
	if (!StaticVoice::LoadXAudio2SourceVoice(mpAudioEngine, pIXAudio2SourceVoice, uiAudioCrc, loadFlags))
	{
		return nullptr;
	}

	if (fPitchRange > 0.0f)
	{
		rfPitch += common::Random(fPitchRange, mRandomEngine);
	}

	if (!b3d)
	{
		CHECK_HRESULT(pIXAudio2SourceVoice->SetVolume(VolumeToPower(gMasterVolume.Get(), gSoundVolume.Get(), fVolume)));
	}
	CHECK_HRESULT(pIXAudio2SourceVoice->SetFrequencyRatio(rfPitch));
	CHECK_HRESULT(pIXAudio2SourceVoice->Start(0, XAUDIO2_COMMIT_NOW));
	return pIXAudio2SourceVoice;
}

void XM_CALLCONV StaticVoices::PlayOneShot3d([[maybe_unused]] const game::Frame& rFrame, common::crc_t uiAudioCrc, FXMVECTOR vecPosition, float fVolume, float fPitch, float fPitchRange)
{
	ASSERT(rFrame.interpolate.frameFlags & FrameFlags::kPostRender);

	// Hoisted above the RNG advance and the lock so replay ticks never mutate audio state
	// (the documented replay invariant) and a suspended client does no extra work.
	if (rFrame.interpolate.frameFlags & FrameFlags::kRecalculated)
	{
		return;
	}

	if (mbSuspended.load(std::memory_order_acquire))
	{
		return;
	}

	if (mpAudioEngine == nullptr || !mpAudioEngine->IsAudioDevicePresent()) [[unlikely]]
	{
		return;
	}

	// Silent one-shots cull at the door — short-circuits before the Distance() call
	// and the lock acquisition.
	if (fVolume <= 0.0f)
	{
		return;
	}

	// Hard-cull inaudible one-shots before grabbing a voice slot. Uses the same curve
	// as the persistent priority pass so behaviour is consistent across both paths.
	float fCullDistance = common::Distance(vecPosition, mVecListenerPosition);
	if (ComputeAttenuatedVolume(fCullDistance, fVolume) < kfCullVolume)
	{
		return;
	}

	std::lock_guard<std::mutex> lock(mOneShotMutex);

	// PlayOneShotLocked randomizes fPitch in place so Apply3dVolume's SetFrequencyRatio
	// uses the same randomized ratio rather than overwriting it with the base pitch.
	IXAudio2SourceVoice* pIXAudio2SourceVoice = PlayOneShotLocked(uiAudioCrc, true, fVolume, fPitch, fPitchRange);
	if (pIXAudio2SourceVoice != nullptr)
	{
		Apply3dVolume(pIXAudio2SourceVoice, vecPosition, XMVectorZero(), fVolume, fPitch);
	}
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
		DestroyXAudio2SourceVoice(mpAudioEngine, rPooled.mpVoice);
	}
	mPooledVoices.clear();
}

void StaticVoices::UpdateLifecycle(const game::Frame& rFrame, float fDeltaTime)
{
	ASSERT(rFrame.interpolate.frameFlags & FrameFlags::kPostRender);

	// Skipping replay ticks (kRecalculated) also skips the consumption of mbSkipNextInvalidation
	// below; that is intentional. The flag is raised by game::ClientReconciler::Run after a
	// bAnyFullReplay — a full network reconciliation replay can rewrite post-render sound IDs
	// mid-tick, which would otherwise cause the invalidation pass to fade-out voices whose IDs
	// no longer match the replayed state. By suppressing one tick of invalidation, UpdateVolumes
	// can re-establish the replayed IDs before the next invalidation pass runs.
	if (rFrame.interpolate.frameFlags & FrameFlags::kRecalculated)
	{
		return;
	}

	const SoundsInterpolate& rSoundsInterpolate = rFrame.interpolate.sounds;
	const SoundsPostRender& rSoundsPostRender = rFrame.postRender.sounds;

	bool bSkipInvalidation = mbSkipNextInvalidation;
	mbSkipNextInvalidation = false;

	// Invalidation pass: an mVoices entry whose ID is no longer in the frame state
	// transitions to fade-out (or erases outright if it's already silent / inactive).
	// Fade advance and the kFadingOut → kInactive transition live in pass 4 so all
	// three sources of fade (invalidation, range-deactivation, budget-eviction)
	// share one ramp-and-cleanup path.
	auto FadeOutCount = [this]() -> int64_t
	{
		int64_t iCount = 0;
		for (const StaticVoice& rOther : mVoices)
		{
			if (rOther.mFlags & StaticVoiceFlags::kFadingOut)
			{
				++iCount;
			}
		}
		return iCount;
	};

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
			if (rVoice.mFlags & StaticVoiceFlags::kInactive)
			{
				// Inactive voices have mpVoice == nullptr. Drop the entry directly.
				bDestroy = true;
			}
			else if (rVoice.mFlags & StaticVoiceFlags::kFadingOut)
			{
				// Pass 4 advances the fade; next-frame invalidation erases once it transitions to kInactive.
			}
			else if (rVoice.mfVolume <= 0.0f)
			{
				// Already silent; skip the fade and erase directly.
				if (rVoice.mpVoice != nullptr)
				{
					ReturnVoiceToPool(rVoice.mAudioCrc, rVoice.mpVoice);
					rVoice.mpVoice = nullptr;
				}
				bDestroy = true;
			}
			else if (FadeOutCount() >= kiMaxFadeOutPool)
			{
				// FadeOutPool saturated. Skip; next-frame invalidation retries once a slot frees.
				DEBUG_BREAK();
			}
			else
			{
				rVoice.mFlags.Set(StaticVoiceFlags::kFadingOut);
				rVoice.mfFadeOutVolume = 1.0f;
			}

			if (bDestroy)
			{
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

	// Priority + cull pass: rank candidates by attenuated volume so the closest /
	// loudest sounds win the kiMaxStaticVoices slots. Out-of-range candidates (below
	// the hysteresis floor) are skipped entirely; matching active voices get
	// deactivated below.
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
						if (!StaticVoice::LoadXAudio2SourceVoice(mpAudioEngine, pVoice, uiCrc, LoadVoiceFlags::k3d))
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
				else if ((pExistingVoice->mFlags & StaticVoiceFlags::kFadingOut) && fAttenuated >= kfCullVolume)
				{
					// Cancel fade-out: voice is still playing. Clearing the flag lets the
					// fade-in ramp below pick mfFadeOutVolume up from wherever it had reached
					// and ramp it back to 1.0. No Start() / SetVolume(0) / click.
					pExistingVoice->mFlags.Clear(StaticVoiceFlags::kFadingOut);
					LOG(kAudio, kDebug, "voice FADE CANCELLED id={} fadeVol={}", id, common::Wb(pExistingVoice->mfFadeOutVolume, 2));
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
			// Active + kInactive entries count toward kiMaxStaticVoices; kFadingOut entries
			// live in the FadeOutPool overflow capacity above the primary cap.
			if (static_cast<int64_t>(mVoices.size()) - FadeOutCount() >= kiMaxStaticVoices)
			{
				LOG(kAudio, kDebug, "Max static voices reached ({}), deferring add", kiMaxStaticVoices);
				continue;
			}

			common::crc_t uiCrc = rSoundsInterpolate.puiCrcs[iIndex];
			IXAudio2SourceVoice* pVoice = AcquireVoiceFromPool(uiCrc);
			if (pVoice == nullptr)
			{
				if (!StaticVoice::LoadXAudio2SourceVoice(mpAudioEngine, pVoice, uiCrc, LoadVoiceFlags::k3d))
				{
					continue;
				}
			}
			float fFadeOutTime = rSoundsInterpolate.pfFadeOutTimes[iIndex];
			mVoices.push_back(StaticVoice(pVoice, id, fSoundVolume, fPitch, fFadeOutTime, vecPosition, vecVelocity, uiCrc));
			// Establish the attenuated 3D mix this same pass — the ctor now starts the voice silent
			// (SetVolume(0)), so without this the first quantum would be inaudible until the next
			// UpdateVolumes. Matches what UpdateVolumes computes (mfFadeOutVolume initializes to 1.0).
			Apply3dVolume(pVoice, vecPosition, vecVelocity, fSoundVolume, fPitch);
			pActivatedIds[iActivatedCount++] = id;
		}
	}

	// Deactivation pass: any active voice not given a slot this frame (past cap or
	// below the activate threshold) enters fade-out. The XAudio2 voice keeps playing
	// while mfFadeOutVolume ramps to zero (pass 4 below advances and finalizes), so
	// the cut is graceful instead of a mid-sample Stop() click. Runs unconditionally
	// so iSoundCount==0 also drains stale active voices.
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
			if (FadeOutCount() >= kiMaxFadeOutPool)
			{
				// Pool saturated. Voice stays active; deactivation pass retries next
				// frame once a slot frees. One extra frame of intended-gone audio,
				// but no crackle and no over-budget pool occupancy.
				DEBUG_BREAK();
				continue;
			}
			rVoice.mFlags.Set(StaticVoiceFlags::kFadingOut);
			rVoice.mfFadeOutVolume = 1.0f;
			LOG(kAudio, kDebug, "voice FADING id={} dist={}", rVoice.mId, common::Wb(common::Distance(rVoice.mVecPosition, mVecListenerPosition), 2));
		}
	}

	// Pass 4: advance kFadingOut entries' mfFadeOutVolume. On completion, return the
	// XAudio2 voice to the per-crc pool and transition to kInactive — the entry stays
	// in mVoices so a re-entering sound can reactivate via the existing kInactive
	// path. The invalidation pass erases owner-removed kInactive entries next frame.
	for (StaticVoice& rVoice : mVoices)
	{
		if (!(rVoice.mFlags & StaticVoiceFlags::kFadingOut))
		{
			continue;
		}
		rVoice.mfFadeOutVolume -= fDeltaTime / rVoice.mfFadeOutTime;
		if (rVoice.mfFadeOutVolume <= 0.0f)
		{
			if (rVoice.mpVoice != nullptr)
			{
				ReturnVoiceToPool(rVoice.mAudioCrc, rVoice.mpVoice);
				rVoice.mpVoice = nullptr;
			}
			rVoice.mFlags.Clear(StaticVoiceFlags::kFadingOut);
			rVoice.mFlags.Set(StaticVoiceFlags::kInactive);
			rVoice.mfFadeOutVolume = 0.0f;
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
}

void StaticVoices::UpdateListenerPosition([[maybe_unused]] const game::Frame& rFrame)
{
	// Two listener points, decoupled by purpose:
	//   * mVecListenerPosition (camera eye, full XYZ) — drives the manual fade distance so
	//     altitude inflates the listener-to-emitter distance.
	//   * mX3dAudioListener.Position (camera look-at on the world plane at gBaseHeight) —
	//     drives X3DAudio pan/Doppler. Pinning the X3DAudio listener to the world plane
	//     keeps the listener-to-ground-emitter vector XY-only, so the pan azimuth reflects
	//     'left-of-screen → left-ear' instead of being collapsed near-center by altitude.
	// Velocity is zero — the RTS camera moves slowly enough that listener-motion Doppler is
	// negligible, and zeroing it avoids artifacts during snap-to-unit / jump-easing.
	mVecListenerPosition = game::gpCamera->mVecEyePosition;
	XMVECTOR vecPanListener = XMVectorSetZ(game::gpCamera->mVecPosition, gBaseHeight.Get());
	XMFLOAT3A f3PanPosition {};
	XMStoreFloat3A(&f3PanPosition, vecPanListener);
	mX3dAudioListener.OrientFront = {0.0f, 0.0f, -1.0f};
	mX3dAudioListener.OrientTop = {0.0f, -1.0f, 0.0f};
	mX3dAudioListener.Position = f3PanPosition;
	mX3dAudioListener.Velocity = {0.0f, 0.0f, 0.0f};

	// Fade band + X3DAudio curve are camera-height-lerped per the canonical "Camera-Height-
	// Conditional Uniforms" pattern (see Engine/Source/Graphics/Render/CLAUDE.md). Each
	// quantity owns four wrappers (StartHeight, EndHeight, Low, High) in SoundSettingsWrappersBase
	// and is exposed in the Sound > Tweaks sub-tab. Distances on the consumer side
	// (Apply3dVolume / ComputeAttenuatedVolume) are 3D against mVecListenerPosition (camera
	// eye), so altitude naturally pushes ground emitters into the fade band as the camera climbs.
	const float fEyeHeight = game::gpCamera->mfCameraEyeHeight;
	mfEffectiveFadeStart = gListenerDistanceStart.Resolve(fEyeHeight);
	mfEffectiveFadeEnd = gListenerDistanceEnd.Resolve(fEyeHeight);
	mfCurveDistanceScaler = gListenerCurve.Resolve(fEyeHeight);
	mfManualFadeVolume = gListenerAudibleFloor.Resolve(fEyeHeight);
}

void StaticVoices::UpdateVolumes()
{
	for (const StaticVoice& rVoice : mVoices)
	{
		if (rVoice.mFlags & StaticVoiceFlags::kInactive)
		{
			continue; // No XAudio2 voice attached — skip mix.
		}
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
			DestroyXAudio2SourceVoice(mpAudioEngine, rVoice.mpVoice);
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
	else if (fDistance >= mfEffectiveFadeStart)
	{
		float fBand = std::max(mfEffectiveFadeEnd - mfEffectiveFadeStart, 0.0001f);
		float fPercent = std::clamp((fDistance - mfEffectiveFadeStart) / fBand, 0.0f, 1.0f);
		fDistanceVolume = (1.0f - fPercent) * fSoundVolume;
	}
	return fDistanceVolume;
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

	CHECK_HRESULT(pVoice->SetOutputMatrix(mpAudioEngine->GetMasterVoice(), 1, static_cast<UINT32>(iMasteringVoiceChannels), x3dAudioDspSettings.pMatrixCoefficients));

	// Apply custom volume with distance-based attenuation. The natural curve goes to
	// zero at fade-end (used for cull priority); the audible mix clamps to a fVolume-
	// relative floor so in-range sounds never drop below an audible floor while
	// silent-in voices (fVolume=0) stay silent-out.
	float fDistance = common::Distance(vecPosition, mVecListenerPosition);
	float fAttenuated = ComputeAttenuatedVolume(fDistance, fVolume);
	float fAudibleFloor = mfManualFadeVolume * fVolume;
	float fDistanceVolume = std::max(fAttenuated, fAudibleFloor);

	float fFinalPower = VolumeToPower(gMasterVolume.Get(), gSoundVolume.Get(), fDistanceVolume);
	CHECK_HRESULT(pVoice->SetVolume(fFinalPower));
	CHECK_HRESULT(pVoice->SetFrequencyRatio(x3dAudioDspSettings.DopplerFactor * fPitch));
}

} // namespace engine

#endif // defined(BT_CLIENT)
