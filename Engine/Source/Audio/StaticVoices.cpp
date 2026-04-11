#include "StaticVoices.h"

#if defined(BT_CLIENT)

#include "Memory/MemoryManager.h"

#include "Game.h"
#include "Frame/Collections/Players/Players.h"

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
	ScopedSuppressAllocationTracking suppressAllocationTracking;

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
		if (mPooledVoices[i].first == audioCrc)
		{
			IXAudio2SourceVoice* pVoice = mPooledVoices[i].second;
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
	for (std::pair<common::crc_t, IXAudio2SourceVoice*>& rPooled : mPooledVoices)
	{
		DestroyXAudio2SourceVoice(rPooled.second);
	}
	mPooledVoices.clear();
}

void StaticVoices::UpdateLifecycle(const game::Frame& rFrame, float fDeltaTime)
{
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

			if (bDestroy)
			{
				ReturnVoiceToPool(rVoice.mAudioCrc, rVoice.mpVoice);
				rVoice.mpVoice = nullptr;
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

	// Add new voices and sync existing voice volume/positions
	for (int64_t i = 0; i < rSoundsPostRender.iCount; ++i)
	{
		sound_t id = rSoundsPostRender.puiIds[i];
		int64_t iIndex = rSoundsInterpolate.IdToIndex(id);
		float fVolume = rSoundsInterpolate.pfVolumes[iIndex];

		// Find existing voice
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
			// Sync existing voice
			pExistingVoice->mfVolume = fVolume;
			pExistingVoice->mfPitch = rSoundsInterpolate.pfPitches[iIndex];
			pExistingVoice->mVecPosition = rSoundsInterpolate.pVecPositions[iIndex];
			pExistingVoice->mVecVelocity = rSoundsInterpolate.pVecVelocities[iIndex];
			continue;
		}

		// Add new voice
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
}

void StaticVoices::UpdateVolumes()
{
	for (const StaticVoice& rVoice : mVoices)
	{
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
		for (std::pair<common::crc_t, IXAudio2SourceVoice*>& rPooled : mPooledVoices)
		{
			rPooled.second = nullptr;
		}
		mPooledVoices.clear();
	}
	else
	{
		ClearPool();
	}
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

	// Apply custom volume with distance-based attenuation
	float fDistance = common::Distance(vecPosition, mVecListenerPosition);
	float fDistanceVolume = fVolume;
	if (fDistance >= mfManualFadeEnd)
	{
		fDistanceVolume = mfManualFadeVolume;
	}
	else if (fDistance >= mfManualFadeStart)
	{
		float fPercent = std::clamp((fDistance - mfManualFadeStart) / (mfManualFadeEnd - mfManualFadeStart), 0.0f, 1.0f);
		fDistanceVolume = (1.0f - fPercent) * fVolume + fPercent * mfManualFadeVolume;
	}

	CHECK_HRESULT(pVoice->SetVolume(VolumeToPower(gMasterVolume.Get(), gSoundVolume.Get(), fDistanceVolume)));
	CHECK_HRESULT(pVoice->SetFrequencyRatio(x3dAudioDspSettings.DopplerFactor * fPitch));
}

} // namespace engine

#endif // defined(BT_CLIENT)
