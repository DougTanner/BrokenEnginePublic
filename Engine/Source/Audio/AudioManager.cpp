#include "AudioManager.h"

#if defined(BT_CLIENT)

#include "Memory/MemoryManager.h"

#include "Game.h"
#include "Frame/Collections/Players/Players.h"
#include "Profile/ProfileManager.h"

namespace engine
{

constexpr AUDIO_ENGINE_FLAGS kAudioEngineFlags = AudioEngine_UseMasteringLimiter; //  | AudioEngine_Debug;

AudioManager::AudioManager()
{
	gpAudioManager = this;
	mRandomEngine.TimeSeed();
	mStaticVoices.reserve(kiMaxStaticVoices);

	Log("\nAudioManager");

	try
	{
		// Find the id of the default audio endpoint
		Microsoft::WRL::ComPtr<IMMDeviceEnumerator> pMMDeviceEnumerator;
		CHECK_HRESULT(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(pMMDeviceEnumerator.GetAddressOf())));
		Log("  Got MMDeviceEnumerator");

		Microsoft::WRL::ComPtr<IMMDevice> pDefaultAudioEndpoint;
		HRESULT hresult = pMMDeviceEnumerator->GetDefaultAudioEndpoint(eRender, eMultimedia, &pDefaultAudioEndpoint);
		if (hresult != S_OK)
		{
			return;
		}
		Log("  Got DefaultAudioEndpoint");

		LPWSTR pcDefaultDeviceId = nullptr;
		CHECK_HRESULT(pDefaultAudioEndpoint->GetId(&pcDefaultDeviceId));
		if (pcDefaultDeviceId == nullptr)
		{
			Log("  GetId returned nullptr");
			return;
		}
		std::wstring defaultAudioEndpointId(pcDefaultDeviceId);
		Log("    pcDeviceId: \"{}\"", defaultAudioEndpointId);
		common::ScopedLambda freeDefaultDeviceId([=]()
		{
			CoTaskMemFree(pcDefaultDeviceId);
		});

		Microsoft::WRL::ComPtr<IMMDeviceCollection> pMMDeviceCollection;
		CHECK_HRESULT(pMMDeviceEnumerator->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &pMMDeviceCollection));
		if (pMMDeviceCollection == nullptr)
		{
			Log("  EnumAudioEndpoints returned nullptr");
			return;
		}

		Log("  Searching for default audio endpoint: {}", defaultAudioEndpointId);
		UINT uiCount = 0;
		CHECK_HRESULT(pMMDeviceCollection->GetCount(&uiCount));
		Log("  uiCount: {}", uiCount);
		for (UINT i = 0; i < uiCount; ++i)
		{
			Microsoft::WRL::ComPtr<IMMDevice> pMMDevice;
			CHECK_HRESULT(pMMDeviceCollection->Item(i, pMMDevice.GetAddressOf()));
			LPWSTR pcDeviceId = nullptr;
			CHECK_HRESULT(pMMDevice->GetId(&pcDeviceId));
			std::wstring audioEndpointId(pcDeviceId);
			common::ScopedLambda freeDeviceId([=]()
			{
				CoTaskMemFree(pcDeviceId);
			});

			if (audioEndpointId.find(defaultAudioEndpointId) == std::wstring::npos)
			{
				continue;
			}

			mpAudioEngine = std::make_unique<AudioEngine>(kAudioEngineFlags, nullptr, audioEndpointId.c_str(), AudioCategory_GameEffects);
			Log("    Found: {}", audioEndpointId);
			break;
		}

		if (mpAudioEngine == nullptr || !mpAudioEngine->IsAudioDevicePresent())
		{
			Log("  mpAudioEngine == nullptr || !mpAudioEngine->IsAudioDevicePresent()");

			if (uiCount > 0)
			{
				Microsoft::WRL::ComPtr<IMMDevice> pMMDevice;
				CHECK_HRESULT(pMMDeviceCollection->Item(0, pMMDevice.GetAddressOf()));
				LPWSTR pcDeviceId = nullptr;
				CHECK_HRESULT(pMMDevice->GetId(&pcDeviceId));
				common::ScopedLambda freeDeviceId([=]()
				{
					CoTaskMemFree(pcDeviceId);
				});
				std::wstring audioEndpointId(pcDeviceId);
				Log("    Using first in the list: {}", audioEndpointId);
				mpAudioEngine = std::make_unique<AudioEngine>(kAudioEngineFlags, nullptr, audioEndpointId.c_str(), AudioCategory_GameEffects);
			}
		}

		if (mpAudioEngine != nullptr)
		{
			IXAudio2* pIXAudio2 = mpAudioEngine->GetInterface();
			XAUDIO2_DEBUG_CONFIGURATION debugConfiguration {XAUDIO2_LOG_ERRORS | XAUDIO2_LOG_WARNINGS, XAUDIO2_LOG_ERRORS, true, true, true, false};
			pIXAudio2->SetDebugConfiguration(&debugConfiguration);

			mpAudioEngine->RegisterNotify(this, false);

			IXAudio2MasteringVoice* pIXAudio2MasteringVoice = mpAudioEngine->GetMasterVoice();
			XAUDIO2_VOICE_DETAILS voiceDetails {};
			pIXAudio2MasteringVoice->GetVoiceDetails(&voiceDetails);
			DWORD uiChannelMask = 0;
			CHECK_HRESULT(pIXAudio2MasteringVoice->GetChannelMask(&uiChannelMask));

			char pcHex[20] {};
			Log("    Audio engine: channels {} channel mask {} rate {}", mpAudioEngine->GetOutputChannels(), common::ToHex(std::span(pcHex), mpAudioEngine->GetChannelMask()), mpAudioEngine->GetOutputSampleRate());
			Log("    Output format: channels {} channel mask {} format {}", mpAudioEngine->GetOutputFormat().Format.nChannels, common::ToHex(std::span(pcHex), mpAudioEngine->GetOutputFormat().dwChannelMask), mpAudioEngine->GetOutputFormat().Format.wFormatTag);
			Log("    MasteringVoice: channels {} channel mask {} sample rate {}", voiceDetails.InputChannels, common::ToHex(std::span(pcHex), uiChannelMask), voiceDetails.InputSampleRate);

			WAVEFORMATEXTENSIBLE waveFormatExtensible = mpAudioEngine->GetOutputFormat();
			miMasteringVoiceChannels = std::min(static_cast<int64_t>(voiceDetails.InputChannels), static_cast<int64_t>(waveFormatExtensible.Format.nChannels));
		}
	}
	catch ([[maybe_unused]] const std::exception& rException)
	{
		Log("Failed to create AudioManager: {}", rException.what());
		return;
	}
	catch (...)
	{
		Log("Failed to create AudioManager");
		return;
	}
}

AudioManager::~AudioManager()
{
	if (mpAudioEngine != nullptr)
	{
		mpAudioEngine->UnregisterNotify(this, false, false);
	}

	mStaticVoices.clear();

	// Move streams to local storage while holding lock, destroy after releasing
	// This prevents deadlock with XAudio2 callbacks that also acquire the mutex
	std::unique_ptr<StreamingVoice> pCurrentStream;
	std::vector<std::unique_ptr<StreamingVoice>> previousStreams;

	{
		std::lock_guard<std::recursive_mutex> lock(mMusicStreamRecursiveMutex);

		pCurrentStream = std::move(mpCurrentMusicStream);
		previousStreams = std::move(mPreviousStreams);
	}

	// Destruction happens here, after mutex is released
	pCurrentStream.reset();
	previousStreams.clear();

	if (mpAudioEngine != nullptr)
	{
		mpAudioEngine->Update();
	}

	gpAudioManager = nullptr;
}

void AudioManager::Set3dSettings(float fCurveDistanceScaler, float fManualFadeStart, float fManualFadeEnd, float fManualFadeVolume)
{
	mfCurveDistanceScaler = fCurveDistanceScaler;
	mfManualFadeStart = fManualFadeStart;
	mfManualFadeEnd = fManualFadeEnd;
	mfManualFadeVolume = fManualFadeVolume;
}

void AudioManager::SetNextMusicTrackCallback(std::function<common::crc_t()> callback)
{
	std::lock_guard<std::recursive_mutex> lock(mMusicStreamRecursiveMutex);

	mGetNextMusicTrack = callback;
}

void AudioManager::ClearVoices()
{
	for (StaticVoice& rStaticVoice : mStaticVoices)
	{
		rStaticVoice.mpVoice = nullptr;
	}
	mStaticVoices.clear();

	ClearStreamingVoices();
}

void AudioManager::ClearStreamingVoices()
{
	std::lock_guard<std::recursive_mutex> lock(mMusicStreamRecursiveMutex);

	if (mpCurrentMusicStream != nullptr)
	{
		mpCurrentMusicStream->mpVoice = nullptr;
	}
	mpCurrentMusicStream.reset();

	for (std::unique_ptr<StreamingVoice>& pPreviousStream : mPreviousStreams)
	{
		if (pPreviousStream != nullptr)
		{
			pPreviousStream->mpVoice = nullptr;
		}
	}
	mPreviousStreams.clear();
}

void AudioManager::PlayMusic(common::crc_t audioCrc)
{
	std::lock_guard<std::recursive_mutex> lock(mMusicStreamRecursiveMutex);

	// Heap: make_unique<StreamingVoice> (with triple buffers) and vector push_back for crossfade list.
	// These outlive the call (persist until fade-out completes), so workbuffer/pre-alloc won't work.
	ScopedSuppressAllocationTracking suppressAllocationTracking;

	// Move current stream to previous list for fade out
	if (mpCurrentMusicStream != nullptr)
	{
		mpCurrentMusicStream->mFlags.Clear(StreamingVoiceFlags::kFadingIn);
		mpCurrentMusicStream->mFlags.Set(StreamingVoiceFlags::kFadingOut);
		mPreviousStreams.push_back(std::move(mpCurrentMusicStream));
	}

	if (mpAudioEngine == nullptr || !mpAudioEngine->IsAudioDevicePresent()) [[unlikely]]
	{
		return;
	}

	// Load new track as current
	const LazyChunk& rLazyChunk = gpFileManager->GetLazyChunkMap().at(audioCrc);
	IXAudio2SourceVoice* pVoice = nullptr;
	mpAudioEngine->AllocateVoice(&rLazyChunk.header.audioHeader.waveFormat, SoundEffectInstance_Default, false, &pVoice);
	if (pVoice != nullptr)
	{
		// StreamingVoice takes ownership of pVoice
		mpCurrentMusicStream = std::make_unique<StreamingVoice>(pVoice, &rLazyChunk);
	}
}

void AudioManager::UpdateMusicStreams(float fDeltaTime)
{
	// Heap: Local vector collects faded-out streams for deferred destruction outside the mutex.
	// Must be a real vector (workbuffer can't run unique_ptr destructors), count varies per frame.
	ScopedSuppressAllocationTracking suppressAllocationTracking;

	// Collect streams to destroy outside the lock to prevent deadlock with XAudio2 callbacks
	std::vector<std::unique_ptr<StreamingVoice>> streamsToDestroy;

	{
		std::lock_guard<std::recursive_mutex> lock(mMusicStreamRecursiveMutex);

		// Update current stream volume (fade in)
		if (mpCurrentMusicStream != nullptr)
		{
			mpCurrentMusicStream->UpdateVolume(fDeltaTime);
		}

		// Update previous streams (fade out) and remove completed ones
		for (auto it = mPreviousStreams.begin(); it != mPreviousStreams.end();)
		{
			if ((*it)->UpdateVolume(fDeltaTime))
			{
				// Fade out complete, move to deferred destruction list
				streamsToDestroy.push_back(std::move(*it));
				it = mPreviousStreams.erase(it);
			}
			else
			{
				++it;
			}
		}
	}

	// Destruction happens here, after mutex is released
	// DestroyVoice() can now safely wait for OnBufferEnd callbacks
}

void XM_CALLCONV AudioManager::Apply3dVolume(IXAudio2SourceVoice* pVoice, FXMVECTOR vecPosition, FXMVECTOR vecVelocity, float fVolume, float fPitch)
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

	int64_t iMasteringVoiceChannels = miMasteringVoiceChannels;

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

void AudioManager::Update(const game::Frame* pFrame)
{
	ScopedCpuProfile scopedCpuProfile(kCpuTimerAudio);

	// Heap: Voice map emplace/erase, make_unique<StreamingVoice> for track transitions, and
	// XAudio2 internal allocations (AllocateVoice, Update). Not controllable or pre-allocatable.
	ScopedSuppressAllocationTracking suppressAllocationTracking;

	if (mbClearVoicesRequested.exchange(false, std::memory_order_acquire))
	{
		for (StaticVoice& rStaticVoice : mStaticVoices)
		{
			rStaticVoice.mpVoice = nullptr;
		}
		mStaticVoices.clear();
	}

	if (mpAudioEngine != nullptr && !mpAudioEngine->IsAudioDevicePresent()) [[unlikely]]
	{
		Log("Music streaming: Audio device not present, resetting audio engine");

		mpAudioEngine->Reset();

		// Re-cache mastering voice channels after device reset
		{
			IXAudio2MasteringVoice* pMasterVoice = mpAudioEngine->GetMasterVoice();
			XAUDIO2_VOICE_DETAILS voiceDetails {};
			pMasterVoice->GetVoiceDetails(&voiceDetails);
			WAVEFORMATEXTENSIBLE waveFormat = mpAudioEngine->GetOutputFormat();
			miMasteringVoiceChannels = std::min(static_cast<int64_t>(voiceDetails.InputChannels), static_cast<int64_t>(waveFormat.Format.nChannels));
		}

		// After Reset() is called, all XAudio2SourceVoices are destroyed internally to AudioEngine and their pointers must be set to nullptr
		ClearVoices();
	}

	if (mpAudioEngine == nullptr || !mpAudioEngine->IsAudioDevicePresent()) [[unlikely]]
	{
		return;
	}

	float fDeltaTime = common::NanosecondsToFloatSeconds<float>(mRealTime.GetDeltaNs(true));

	{
		std::lock_guard<std::recursive_mutex> lock(mMusicStreamRecursiveMutex);

		// Check if current stream has ended (position >= size or last buffer submitted)
		if (mpCurrentMusicStream != nullptr)
		{
			float fRemaining = mpCurrentMusicStream->GetRemainingTime();

			bool bShouldTransition = (fRemaining <= kfCrossfadeDuration) || (mpCurrentMusicStream->miCurrentPosition >= mpCurrentMusicStream->mpLazyChunk->header.iSize) || (mpCurrentMusicStream->mFlags & StreamingVoiceFlags::kLastBufferSubmitted);
			if (bShouldTransition && mGetNextMusicTrack)
			{
				common::crc_t nextTrackCrc = mGetNextMusicTrack();

				// Move current stream to previous list for fade out
				mpCurrentMusicStream->mFlags.Clear(StreamingVoiceFlags::kFadingIn);
				mpCurrentMusicStream->mFlags.Set(StreamingVoiceFlags::kFadingOut);
				mPreviousStreams.push_back(std::move(mpCurrentMusicStream));

				// Load next track as current
				const LazyChunk& rLazyChunk = gpFileManager->GetLazyChunkMap().at(nextTrackCrc);
				IXAudio2SourceVoice* pVoice = nullptr;
				mpAudioEngine->AllocateVoice(&rLazyChunk.header.audioHeader.waveFormat, SoundEffectInstance_Default, false, &pVoice);
				if (pVoice != nullptr)
				{
					// StreamingVoice takes ownership of pVoice
					mpCurrentMusicStream = std::make_unique<StreamingVoice>(pVoice, &rLazyChunk);
				}
			}
		}
	}

	UpdateMusicStreams(fDeltaTime);

	if (pFrame != nullptr)
	{
		const game::Frame& rFrame = *pFrame;
		const SoundsInterpolate& rSoundsInterpolate = rFrame.interpolate.sounds;
		const SoundsPostRender& rSoundsPostRender = rFrame.postRender.sounds;

		// Fade out and stop invalid static voices
		for (int64_t i = 0; i < static_cast<int64_t>(mStaticVoices.size());)
		{
			StaticVoice& rVoice = mStaticVoices[i];

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
				if (i < static_cast<int64_t>(mStaticVoices.size()) - 1)
				{
					mStaticVoices[i] = std::move(mStaticVoices.back());
				}
				mStaticVoices.pop_back();
			}
			else
			{
				++i;
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
			for (StaticVoice& rExisting : mStaticVoices)
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
			if (static_cast<int64_t>(mStaticVoices.size()) >= kiMaxStaticVoices)
			{
				continue;
			}

			common::crc_t uiCrc = rSoundsInterpolate.puiCrcs[iIndex];
			IXAudio2SourceVoice* pVoice = nullptr;
			if (StaticVoice::LoadXAudio2SourceVoice(mpAudioEngine.get(), pVoice, uiCrc, false, true))
			{
				float fPitch = rSoundsInterpolate.pfPitches[iIndex];
				float fFadeOutTime = rSoundsInterpolate.pfFadeOutTimes[iIndex];
				XMVECTOR vecPosition = rSoundsInterpolate.pVecPositions[iIndex];
				XMVECTOR vecVelocity = rSoundsInterpolate.pVecVelocities[iIndex];
				mStaticVoices.push_back(StaticVoice(pVoice, id, uiCrc, fVolume, fPitch, fFadeOutTime, vecPosition, vecVelocity));
			}
		}

		// Update listener position from human player
		XMVECTOR vecListenerPos = XMVectorZero();
		XMVECTOR vecListenerVel = XMVectorZero();
		std::optional<int64_t> oHumanIndex = game::gpGame->HumanPlayerIndex(*rFrame.interpolate.pPlayers);
		if (oHumanIndex.has_value())
		{
			int64_t iHumanIndex = oHumanIndex.value();
			vecListenerPos = rFrame.interpolate.pPlayers->pVecPositions[iHumanIndex];

			// Find matching index in postRender for velocity
			game::player_t humanId = game::gpGame->HumanPlayerId();
			const game::PlayersPostRender& rPostRenderPlayers = *rFrame.postRender.pPlayers;
			for (int64_t i = 0; i < rPostRenderPlayers.iCount; ++i)
			{
				if (rPostRenderPlayers.puiIds[i] == humanId)
				{
					vecListenerVel = rPostRenderPlayers.pVecVelocities[i];
					break;
				}
			}
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

	// 3D volume calculation uses cached mVecListenerPosition — runs always
	for (const StaticVoice& rVoice : mStaticVoices)
	{
		Apply3dVolume(rVoice.mpVoice, rVoice.mVecPosition, rVoice.mVecVelocity, rVoice.mfFadeOutVolume * rVoice.mfVolume, rVoice.mfPitch);
	}

	gpProfileManager->SetCount(kCpuCounterSounds, mStaticVoices.size());

	// Update
	mpAudioEngine->Update();
}

IXAudio2SourceVoice* AudioManager::PlayOneShot([[maybe_unused]] const game::Frame& rFrame, common::crc_t audioCrc, bool b3d, float fVolume, float fPitch, float fPitchRange)
{
	ASSERT(rFrame.interpolate.frameFlags & FrameFlags::kPostRender);

	if (rFrame.interpolate.frameFlags & FrameFlags::kRecalculated)
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
	if (!StaticVoice::LoadXAudio2SourceVoice(mpAudioEngine.get(), pIXAudio2SourceVoice, audioCrc, true, b3d))
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

void XM_CALLCONV AudioManager::PlayOneShot3d([[maybe_unused]] const game::Frame& rFrame, common::crc_t audioCrc, FXMVECTOR vecPosition, float fVolume, float fPitch, float fPitchRange)
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

	IXAudio2SourceVoice* pIXAudio2SourceVoice = PlayOneShot(rFrame, audioCrc, true, fVolume, fPitch);
	if (pIXAudio2SourceVoice != nullptr)
	{
		Apply3dVolume(pIXAudio2SourceVoice, vecPosition, XMVectorZero(), fVolume, fPitch);
	}
}

void AudioManager::OnCriticalError()
{
	Log("AudioManager::OnCriticalError()");
	mbClearVoicesRequested.store(true, std::memory_order_release);
	ClearStreamingVoices();
}

void AudioManager::OnReset()
{
	Log("AudioManager::OnReset()");
	mbClearVoicesRequested.store(true, std::memory_order_release);
	ClearStreamingVoices();
}

void AudioManager::OnDestroyEngine() noexcept
{
	Log("AudioManager::OnDestroyEngine()");
	mbClearVoicesRequested.store(true, std::memory_order_release);
	ClearStreamingVoices();
}

void AudioManager::OnTrim()
{
	Log("AudioManager::OnTrim()");
}

void AudioManager::OnDestroyParent() noexcept
{
	Log("AudioManager::OnDestroyParent()");
}

} // namespace engine

#endif // BT_CLIENT
