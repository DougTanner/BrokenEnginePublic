#include "AudioManager.h"

#include "File/FileManager.h"

#include "Game.h"

namespace engine
{

constexpr float kfCurveDistanceScaler = 10.0f;
constexpr float kfManualFadeStart = 0.0f;
constexpr float kfManualFadeEnd = 150.0f;
constexpr float kfManualFadeVolume = 0.05f;

AudioManager::AudioManager()
{
	gpAudioManager = this;

	LOG("\nAudioManager");

	try
	{
		// Find the id of the default audio endpoint
		Microsoft::WRL::ComPtr<IMMDeviceEnumerator> pMMDeviceEnumerator;
		CHECK_HRESULT(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(pMMDeviceEnumerator.GetAddressOf())));
		LOG("  Got MMDeviceEnumerator");

		Microsoft::WRL::ComPtr<IMMDevice> pDefaultAudioEndpoint;
		HRESULT hresult = pMMDeviceEnumerator->GetDefaultAudioEndpoint(eRender, eMultimedia, &pDefaultAudioEndpoint);
		if (hresult != S_OK)
		{
			CHECK_HRESULT(hresult);
			return;
		}
		LOG("  Got DefaultAudioEndpoint");

		LPWSTR pcDefaultDeviceId = nullptr;
		CHECK_HRESULT(pDefaultAudioEndpoint->GetId(&pcDefaultDeviceId));
		std::wstring defaultAudioEndpointId(pcDefaultDeviceId);
		LOG("    pcDeviceId: \"{}\"", defaultAudioEndpointId);
		common::ScopedLambda freeDefaultDeviceId([=]()
		{
			CoTaskMemFree(pcDefaultDeviceId);
		});

		Microsoft::WRL::ComPtr<IMMDeviceCollection> pMMDeviceCollection;
		CHECK_HRESULT(pMMDeviceEnumerator->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &pMMDeviceCollection));

		LOG("  Searching for default audio endpoint: {}", defaultAudioEndpointId);
		UINT uiCount = 0;
		CHECK_HRESULT(pMMDeviceCollection->GetCount(&uiCount));
		LOG("  uiCount: {}", uiCount);
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

			mpAudioEngine = std::make_unique<AudioEngine>(AudioEngine_Default, nullptr, audioEndpointId.c_str(), AudioCategory_GameEffects);
			LOG("    Found: {}", audioEndpointId);
			break;
		}

		if (mpAudioEngine == nullptr || !mpAudioEngine->IsAudioDevicePresent())
		{
			LOG("  mpAudioEngine == nullptr || !mpAudioEngine->IsAudioDevicePresent()");

			if (uiCount > 0)
			{
				Microsoft::WRL::ComPtr<IMMDevice> pMMDevice;
				CHECK_HRESULT(pMMDeviceCollection->Item(0, pMMDevice.GetAddressOf()));
				LPWSTR pcDeviceId = nullptr;
				CHECK_HRESULT(pMMDevice->GetId(&pcDeviceId));
				std::wstring audioEndpointId(pcDeviceId);
				LOG("    Using first in the list: {}", audioEndpointId);
				mpAudioEngine = std::make_unique<AudioEngine>(AudioEngine_Default, nullptr, audioEndpointId.c_str(), AudioCategory_GameEffects);
			}
		}

		if (mpAudioEngine != nullptr)
		{
		#if 0 // defined(BT_DEBUG)
			IXAudio2* pIXAudio2 = mpAudioEngine->GetInterface();
			XAUDIO2_DEBUG_CONFIGURATION debugConfiguration {XAUDIO2_LOG_ERRORS | XAUDIO2_LOG_WARNINGS | XAUDIO2_LOG_DETAIL | XAUDIO2_LOG_API_CALLS | XAUDIO2_LOG_FUNC_CALLS, XAUDIO2_LOG_ERRORS | XAUDIO2_LOG_WARNINGS, true, true, true, false};
			pIXAudio2->SetDebugConfiguration(&debugConfiguration);
		#endif

			// Register this AudioManager as a callback for voice notifications
			mpAudioEngine->RegisterNotify(this, false);

			LOG("    Audio engine: channels {} channel mask 0x{:X} rate {}", mpAudioEngine->GetOutputChannels(), mpAudioEngine->GetChannelMask(), mpAudioEngine->GetOutputSampleRate());

			WAVEFORMATEXTENSIBLE waveFormatExtensible = mpAudioEngine->GetOutputFormat();
			LOG("    Output format: channels {} channel mask 0x{:X} format {}", waveFormatExtensible.Format.nChannels, waveFormatExtensible.dwChannelMask, waveFormatExtensible.Format.wFormatTag);

			IXAudio2MasteringVoice* pIXAudio2MasteringVoice = mpAudioEngine->GetMasterVoice();
			XAUDIO2_VOICE_DETAILS voiceDetails {};
			pIXAudio2MasteringVoice->GetVoiceDetails(&voiceDetails);
			DWORD uiChannelMask = 0;
			CHECK_HRESULT(pIXAudio2MasteringVoice->GetChannelMask(&uiChannelMask));
			LOG("    MasteringVoice: channels {} channel mask 0x{:X} sample rate {}", voiceDetails.InputChannels, uiChannelMask, voiceDetails.InputSampleRate);
		}

		LOG("");
	}
	catch ([[maybe_unused]] const std::exception& rException)
	{
		LOG("Failed to create AudioManager: {}", rException.what());
		return;
	}
	catch (...)
	{
		LOG("Failed to create AudioManager");
		return;
	}
}

AudioManager::~AudioManager()
{
	// Unregister from callbacks before destroying
	if (mpAudioEngine != nullptr)
	{
		mpAudioEngine->UnregisterNotify(this, false, false);
	}

	mStaticVoices.clear();
	mpCurrentMusicStream = nullptr;
	mPreviousStreams.clear();

	gpAudioManager = nullptr;
}

void AudioManager::SetNextTrackCallback(std::function<common::crc_t()> callback)
{
	// Lock mutex for thread safety
	std::lock_guard<std::mutex> lock(mMusicStreamMutex);
	mGetNextMusicTrack = callback;
}

void AudioManager::PlayMusic(common::crc_t audioCrc)
{
	// Lock mutex for thread safety during music state changes
	std::lock_guard<std::mutex> lock(mMusicStreamMutex);

	// Move current stream to previous list for fade out
	if (mpCurrentMusicStream)
	{
		mpCurrentMusicStream->mfTargetVolume = 0.0f;
		mpCurrentMusicStream->mfFadeProgress = 0.0f;
		mPreviousStreams.push_back(std::move(mpCurrentMusicStream));
	}

	// Load new track as current
	LOG_STREAMING_VOICES("Music streaming: Playing new track, CRC: {:#018x}", audioCrc);
	mpCurrentMusicStream = std::make_unique<StreamingVoice>(audioCrc);
}

void AudioManager::UpdateMusicStreams(float fDeltaTime)
{
	// Note: This is called with mutex already locked by Update()

	// Update current stream volume (fade in)
	if (mpCurrentMusicStream && mpCurrentMusicStream->mpVoice != nullptr)
	{
		mpCurrentMusicStream->UpdateVolume(fDeltaTime, gMasterVolume.Get(), gMusicVolume.Get());
	}

	// Update previous streams (fade out) and remove completed ones
	for (auto it = mPreviousStreams.begin(); it != mPreviousStreams.end();)
	{
		if ((*it)->UpdateVolume(fDeltaTime, gMasterVolume.Get(), gMusicVolume.Get()))
		{
			// Fade out complete, remove stream
			LOG_STREAMING_VOICES("Music streaming: Previous stream fade out complete, removing");
			it = mPreviousStreams.erase(it);
		}
		else
		{
			++it;
		}
	}
}

void XM_CALLCONV AudioManager::Apply3d(IXAudio2SourceVoice* pVoice, FXMVECTOR vecPosition, FXMVECTOR vecVelocity, float fVolume, float fPitch)
{
	if (mpAudioEngine == nullptr || !mpAudioEngine->IsAudioDevicePresent()) [[unlikely]]
	{
		return;
	}

	XMFLOAT3A f3Position {};
	XMStoreFloat3A(&f3Position, vecPosition);
	XMFLOAT3A f3Velocity {};
	XMStoreFloat3A(&f3Velocity, vecVelocity);

	X3DAUDIO_EMITTER x3dAudioEmitter
	{
		.Position = f3Position,
		.Velocity = f3Velocity,
		.ChannelCount = 1,
		.CurveDistanceScaler = kfCurveDistanceScaler,
		.DopplerScaler = kfCurveDistanceScaler,
	};

	IXAudio2MasteringVoice* pIXAudio2MasteringVoice = mpAudioEngine->GetMasterVoice();
	XAUDIO2_VOICE_DETAILS voiceDetails {};
	pIXAudio2MasteringVoice->GetVoiceDetails(&voiceDetails);
	WAVEFORMATEXTENSIBLE waveFormatExtensible = mpAudioEngine->GetOutputFormat();
	int64_t iMasteringVoiceChannels = std::min(static_cast<int64_t>(voiceDetails.InputChannels), static_cast<int64_t>(waveFormatExtensible.Format.nChannels));

	static constexpr int64_t kiMaxChannels = 2 * 18;
	FLOAT32 pfMatrixCoefficients[kiMaxChannels] {};
	FLOAT32 pfDelayTimes[kiMaxChannels] {};
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
	CHECK_HRESULT(pVoice->SetFrequencyRatio(x3dAudioDspSettings.DopplerFactor));

	/* Fails because AudioEngine doesn't set XAUDIO2_VOICE_USEFILTER
	XAUDIO2_FILTER_PARAMETERS filterParameters = {LowPassFilter, 2.0f * sinf(X3DAUDIO_PI / 6.0f * x3dAudioDspSettings.LPFDirectCoefficient), 1.0f};
	CHECK_HRESULT(pVoice->SetFilterParameters(&filterParameters));
	*/

	// Apply custom volume with distance-based attenuation
	float fDistance = common::Distance(vecPosition, mVecListenerPosition);
	float fDistanceVolume = fVolume;
	if (fDistance >= kfManualFadeEnd)
	{
		fDistanceVolume = kfManualFadeVolume;
	}
	else if (fDistance >= kfManualFadeStart)
	{
		float fPercent = std::clamp((fDistance - kfManualFadeStart) / (kfManualFadeEnd - kfManualFadeStart), 0.0f, 1.0f);
		fDistanceVolume = (1.0f - fPercent) * fVolume + fPercent * kfManualFadeVolume;
	}

	CHECK_HRESULT(pVoice->SetVolume(CalculateVolume(gMasterVolume.Get(), gSoundVolume.Get(), fDistanceVolume)));
	CHECK_HRESULT(pVoice->SetFrequencyRatio(fPitch));
}

void AudioManager::Update(const game::Frame& rFrame)
{
	if (mpAudioEngine != nullptr && !mpAudioEngine->IsAudioDevicePresent()) [[unlikely]]
	{
		LOG("Music streaming: Audio device not present, resetting audio engine");

		mpAudioEngine->Reset();

		// After Reset() is called, all voices are destroyed internally to AudioEngine and their pointers must be set to nullptr
		mStaticVoices.clear();

		{
			std::lock_guard<std::mutex> lock(mMusicStreamMutex);

			if (mpCurrentMusicStream)
			{
				mpCurrentMusicStream->mpVoice = nullptr;
			}
			mpCurrentMusicStream.reset();

			for (auto& pPreviousStream : mPreviousStreams)
			{
				if (pPreviousStream)
				{
					pPreviousStream->mpVoice = nullptr;
				}
			}
			mPreviousStreams.clear();
		}
	}

	if (mpAudioEngine == nullptr || !mpAudioEngine->IsAudioDevicePresent()) [[unlikely]]
	{
		return;
	}

	ASSERT(rFrame.eFrameType == FrameType::kFull);
		
	// Music - simplified single music system
	float fDeltaTime = common::NanosecondsToFloatSeconds<float>(mRealTime.GetDeltaNs(true));

	// Lock mutex for all music-related operations
	{
		std::lock_guard<std::mutex> lock(mMusicStreamMutex);

		// Check if we need to transition to next track
		if (mpCurrentMusicStream)
		{
			float fRemaining = mpCurrentMusicStream->GetRemainingTime();

			// Crossfade duration
			static constexpr float kfCrossfadeDuration = 2.0f;

			// Also check if stream has ended (position >= size or last buffer submitted)
			bool bShouldTransition = (fRemaining <= kfCrossfadeDuration) ||
				(mpCurrentMusicStream->miCurrentPosition >= mpCurrentMusicStream->mrLazyChunk.header.iSize) ||
				(mpCurrentMusicStream->mFlags & StreamingVoiceFlags::kLastBufferSubmitted);

			if (bShouldTransition && mGetNextMusicTrack)
			{
				// Query gamelogic for next track
				common::crc_t nextTrackCrc = mGetNextMusicTrack();

				if (nextTrackCrc != 0)
				{
					// Move current stream to previous list for fade out
					mpCurrentMusicStream->mfTargetVolume = 0.0f;
					mpCurrentMusicStream->mfFadeProgress = 0.0f;
					mPreviousStreams.push_back(std::move(mpCurrentMusicStream));

					// Load next track as current
					LOG_STREAMING_VOICES("Music streaming: Transitioning to next track! CRC: {:#018x}", nextTrackCrc);

					mpCurrentMusicStream = std::make_unique<StreamingVoice>(nextTrackCrc);
					if (mpCurrentMusicStream->mFlags & StreamingVoiceFlags::kStreamActive)
					{
						LOG_STREAMING_VOICES("Music streaming: Next track loaded successfully");
					}
					else
					{
						LOG_STREAMING_VOICES("Music streaming: ERROR - Failed to load next track");
					}
				}
			}
		}

		UpdateMusicStreams(fDeltaTime);
	}

	// Fade out and stop invalid static voices
	for (auto it = mStaticVoices.begin(); it != mStaticVoices.end();)
	{
		StaticVoice& rVoice = *it;

		bool bValid = false;
		for (decltype(rFrame.sounds.uiMaxIndex) i = 0; i <= rFrame.sounds.uiMaxIndex; ++i)
		{
			if (!rFrame.sounds.pbUsed[i])
			{
				continue;
			}

			const Sound& rSound = rFrame.sounds.pObjects[i];

			bValid |= rSound.iId == rVoice.miFrameId;
		}
		if (bValid)
		{
			++it;
			continue;
		}

		bool bDestroy = false;
		if (rVoice.mfVolume <= 0.0f)
		{
			LOG_STATIC_VOICES("Destroy invalid voice {}", rVoice.miFrameId);
			bDestroy = true;
		}
		if (rVoice.mFlags & StaticVoiceFlags::kFadingOut)
		{
			rVoice.mfFadeOutVolume = rVoice.mfFadeOutVolume - (fDeltaTime / rVoice.mfFadeOutTime) * rVoice.mfVolume;
			if (rVoice.mfFadeOutVolume <= 0.0f)
			{
				LOG_STATIC_VOICES("Destroy invalid voice {}", rVoice.miFrameId);
				bDestroy = true;
			}
		}
		else
		{
			LOG_STATIC_VOICES("Fade out invalid voice {}", rVoice.miFrameId);
			rVoice.mFlags |= StaticVoiceFlags::kFadingOut;
			rVoice.mfFadeOutVolume = rVoice.mfVolume;
		}

		if (bDestroy)
		{
			it = mStaticVoices.erase(it);
		}
		else
		{
			++it;
		}
	}

	// Add new voices
	for (decltype(rFrame.sounds.uiMaxIndex) i = 0; i <= rFrame.sounds.uiMaxIndex; ++i)
	{
		if (!rFrame.sounds.pbUsed[i])
		{
			continue;
		}

		const SoundInfo& rSoundInfo = rFrame.sounds.pObjectInfos[i];
		const Sound& rSound = rFrame.sounds.pObjects[i];

		if (rSoundInfo.fVolume <= 0.0f)
		{
			continue;
		}

		// DT: TEMP Use std::map
		bool bFound = false;
		for (const StaticVoice& rVoice : mStaticVoices)
		{
			bFound |= rVoice.miFrameId == rSound.iId;
		}
		if (bFound)
		{
			continue;
		}

		LOG_STATIC_VOICES("New voice {}", rSound.iId);

		StaticVoice voice(mpAudioEngine.get(), rSoundInfo, rSound);
		if (voice.mFlags & StaticVoiceFlags::kLoaded)
		{
			LOG_STATIC_VOICES("  New voice success", rSound.iId);
			mStaticVoices.push_back(std::move(voice));
		}
	}

	// Sync volume/positions
	for (decltype(rFrame.sounds.uiMaxIndex) i = 0; i <= rFrame.sounds.uiMaxIndex; ++i)
	{
		if (!rFrame.sounds.pbUsed[i])
		{
			continue;
		}

		const SoundInfo& rSoundInfo = rFrame.sounds.pObjectInfos[i];
		const Sound& rSound = rFrame.sounds.pObjects[i];

		StaticVoice* pVoice = nullptr;
		for (StaticVoice& rVoice : mStaticVoices)
		{
			if (rVoice.miFrameId == rSound.iId)
			{
				pVoice = &rVoice;
			}
		}
		if (pVoice == nullptr)
		{
			continue;
		}

		pVoice->mfVolume = rSoundInfo.fVolume;
		pVoice->mfPitch = rSoundInfo.fPitch;
		pVoice->mVecPosition = rSoundInfo.vecPosition;
		pVoice->mVecVelocity = rSoundInfo.vecVelocity;
	}

	// Calculate 3D volumes
	mVecListenerPosition = rFrame.player.vecPosition;
	XMFLOAT3A f3Position {};
	XMStoreFloat3A(&f3Position, rFrame.player.vecPosition);
	f3Position.z += 5.0f; // DT: TEMP Should be constant in Gamelogic or based on 10 x baseheight or something
	XMFLOAT3A f3Velocity {};
	XMStoreFloat3A(&f3Velocity, rFrame.player.vecVelocity);
	mX3dAudioListener.OrientFront = {0.0f, 0.0f, -1.0f};
	mX3dAudioListener.OrientTop = {0.0f, -1.0f, 0.0f};
	mX3dAudioListener.Position = f3Position;
	mX3dAudioListener.Velocity = f3Velocity;

	for (const StaticVoice& rVoice : mStaticVoices)
	{
		Apply3d(rVoice.mpVoice, rVoice.mVecPosition, rVoice.mVecVelocity, rVoice.mFlags & StaticVoiceFlags::kFadingOut ? rVoice.mfFadeOutVolume : rVoice.mfVolume, rVoice.mfPitch);
	}

	PROFILE_SET_COUNT(kCpuCounterSounds, mStaticVoices.size());

	// Update
	mpAudioEngine->Update();
}

IXAudio2SourceVoice* AudioManager::PlayOneShot(common::crc_t audioCrc, bool b3d, float fVolume, float fPitch)
{
#if defined(BT_DEBUG)
	ASSERT(gCurrentFrameTypeProcessing == FrameType::kFull);
#endif

	if (mpAudioEngine == nullptr || !mpAudioEngine->IsAudioDevicePresent()) [[unlikely]]
	{
		return nullptr;
	}

	IXAudio2SourceVoice* pIXAudio2SourceVoice = nullptr;
	if (!StaticVoice::LoadVoice(mpAudioEngine.get(), pIXAudio2SourceVoice, audioCrc, true, b3d))
	{
		return nullptr;
	}

	pIXAudio2SourceVoice->SetVolume(CalculateVolume(gMasterVolume.Get(), gSoundVolume.Get(), fVolume));
	pIXAudio2SourceVoice->SetFrequencyRatio(fPitch);
	CHECK_HRESULT(pIXAudio2SourceVoice->Start(0, XAUDIO2_COMMIT_NOW));
	return pIXAudio2SourceVoice;
}

void XM_CALLCONV AudioManager::PlayOneShot3d(common::crc_t audioCrc, FXMVECTOR vecPosition, float fVolume, float fPitch)
{
#if defined(BT_DEBUG)
	ASSERT(gCurrentFrameTypeProcessing == FrameType::kFull);
#endif

	if (mpAudioEngine == nullptr || !mpAudioEngine->IsAudioDevicePresent()) [[unlikely]]
	{
		return;
	}

	IXAudio2SourceVoice* pIXAudio2SourceVoice = PlayOneShot(audioCrc, true, fVolume, fPitch);
	if (pIXAudio2SourceVoice != nullptr)
	{
		Apply3d(pIXAudio2SourceVoice, vecPosition, XMVectorZero(), fVolume, fPitch);
	}
}

void AudioManager::OnCriticalError()
{
	LOG("AudioManager::OnCriticalError() - Critical audio error occurred!");
}

void AudioManager::OnReset()
{
	LOG("AudioManager::OnReset() - Audio engine reset");
}

void AudioManager::OnUpdate()
{
	// This would be called very frequently if enabled, so only log once
	static bool sbLogged = false;
	if (!sbLogged)
	{
		LOG("AudioManager::OnUpdate() - Per-frame update callback (only logging once)");
		sbLogged = true;
	}
}

void AudioManager::OnDestroyEngine() noexcept
{
	LOG("AudioManager::OnDestroyEngine() - Audio engine being destroyed");
}

void AudioManager::OnTrim()
{
	LOG("AudioManager::OnTrim() - Trimming audio resources");
}

void AudioManager::OnDestroyParent() noexcept
{
	LOG("AudioManager::OnDestroyParent() - Parent being destroyed");
}

} // namespace engine
