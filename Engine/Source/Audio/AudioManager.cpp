#include "AudioManager.h"

#include "File/FileManager.h"

#include "Game.h"

namespace engine
{

// DT: GAMELOGIC
constexpr float kfCurveDistanceScaler = 10.0f;
constexpr float kfManualFadeStart = 0.0f;
constexpr float kfManualFadeEnd = 150.0f;
constexpr float kfManualFadeVolume = 0.05f;

#if defined(BT_DEBUG)
constexpr AUDIO_ENGINE_FLAGS kAudioEngineFlags = AudioEngine_UseMasteringLimiter | AudioEngine_Debug;
#else
constexpr AUDIO_ENGINE_FLAGS kAudioEngineFlags = AudioEngine_UseMasteringLimiter;
#endif

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
			return;
		}
		LOG("  Got DefaultAudioEndpoint");

		LPWSTR pcDefaultDeviceId = nullptr;
		CHECK_HRESULT(pDefaultAudioEndpoint->GetId(&pcDefaultDeviceId));
		if (pcDefaultDeviceId == nullptr)
		{
			LOG("  GetId returned nullptr");
			return;
		}
		std::wstring defaultAudioEndpointId(pcDefaultDeviceId);
		LOG("    pcDeviceId: \"{}\"", defaultAudioEndpointId);
		common::ScopedLambda freeDefaultDeviceId([=]()
		{
			CoTaskMemFree(pcDefaultDeviceId);
		});

		Microsoft::WRL::ComPtr<IMMDeviceCollection> pMMDeviceCollection;
		CHECK_HRESULT(pMMDeviceEnumerator->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &pMMDeviceCollection));
		if (pMMDeviceCollection == nullptr)
		{
			LOG("  EnumAudioEndpoints returned nullptr");
			return;
		}

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

			mpAudioEngine = std::make_unique<AudioEngine>(kAudioEngineFlags, nullptr, audioEndpointId.c_str(), AudioCategory_GameEffects);
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
				common::ScopedLambda freeDeviceId([=]()
				{
					CoTaskMemFree(pcDeviceId);
				});
				std::wstring audioEndpointId(pcDeviceId);
				LOG("    Using first in the list: {}", audioEndpointId);
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

			LOG("    Audio engine: channels {} channel mask 0x{:X} rate {}", mpAudioEngine->GetOutputChannels(), mpAudioEngine->GetChannelMask(), mpAudioEngine->GetOutputSampleRate());
			LOG("    Output format: channels {} channel mask 0x{:X} format {}", mpAudioEngine->GetOutputFormat().Format.nChannels, mpAudioEngine->GetOutputFormat().dwChannelMask, mpAudioEngine->GetOutputFormat().Format.wFormatTag);
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
	if (mpAudioEngine != nullptr)
	{
		mpAudioEngine->UnregisterNotify(this, false, false);
	}

	mStaticVoices.clear();

	{
		std::lock_guard<std::recursive_mutex> lock(mMusicStreamRecursiveMutex);

		mpCurrentMusicStream.reset();
		mPreviousStreams.clear();
	}

	if (mpAudioEngine != nullptr)
	{
		mpAudioEngine->Update();
	}

	gpAudioManager = nullptr;
}

void AudioManager::SetNextMusicTrackCallback(std::function<common::crc_t()> callback)
{
	std::lock_guard<std::recursive_mutex> lock(mMusicStreamRecursiveMutex);

	mGetNextMusicTrack = callback;
}

void AudioManager::ClearVoices()
{
	for (auto& rStaticVoice : mStaticVoices)
	{
		rStaticVoice.mpVoice = nullptr;
	}
	mStaticVoices.clear();

	{
		std::lock_guard<std::recursive_mutex> lock(mMusicStreamRecursiveMutex);

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

void AudioManager::PlayMusic(common::crc_t audioCrc)
{
	std::lock_guard<std::recursive_mutex> lock(mMusicStreamRecursiveMutex);

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

	LOG_STREAMING_VOICES("Music streaming: Playing new track, CRC: {:#018x}", audioCrc);

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

void XM_CALLCONV AudioManager::Apply3dVolume(IXAudio2SourceVoice* pVoice, FXMVECTOR vecPosition, FXMVECTOR vecVelocity, float fVolume, float fPitch)
{
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
	if (fDistance >= kfManualFadeEnd)
	{
		fDistanceVolume = kfManualFadeVolume;
	}
	else if (fDistance >= kfManualFadeStart)
	{
		float fPercent = std::clamp((fDistance - kfManualFadeStart) / (kfManualFadeEnd - kfManualFadeStart), 0.0f, 1.0f);
		fDistanceVolume = (1.0f - fPercent) * fVolume + fPercent * kfManualFadeVolume;
	}

	CHECK_HRESULT(pVoice->SetVolume(VolumeToPower(gMasterVolume.Get(), gSoundVolume.Get(), fDistanceVolume)));
	CHECK_HRESULT(pVoice->SetFrequencyRatio(x3dAudioDspSettings.DopplerFactor * fPitch));
}

void AudioManager::Update(const game::Frame& rFrame)
{
	if (mpAudioEngine != nullptr && !mpAudioEngine->IsAudioDevicePresent()) [[unlikely]]
	{
		LOG("Music streaming: Audio device not present, resetting audio engine");

		mpAudioEngine->Reset();

		// After Reset() is called, all XAudio2SourceVoices are destroyed internally to AudioEngine and their pointers must be set to nullptr
		ClearVoices();
	}

	if (mpAudioEngine == nullptr || !mpAudioEngine->IsAudioDevicePresent()) [[unlikely]]
	{
		return;
	}

	ASSERT(rFrame.eFrameType == FrameType::kFull);
		
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
				LOG_STREAMING_VOICES("Music streaming: Transitioning to next track! CRC: {:#018x}", nextTrackCrc);

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
			rVoice.mfFadeOutVolume -= fDeltaTime / rVoice.mfFadeOutTime;
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
			rVoice.mfFadeOutVolume = 1.0f;
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

		// DT: TODO Use std::map
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

		IXAudio2SourceVoice* pVoice = nullptr;
		if (StaticVoice::LoadXAudio2SourceVoice(mpAudioEngine.get(), pVoice, rSoundInfo.uiCrc, false, true))
		{
			// StaticVoice takes ownership of pVoice
			mStaticVoices.emplace_back(pVoice, rSoundInfo, rSound);
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
	f3Position.z += 5.0f; // DT: GAMELOGIC Should be constant in Gamelogic or based on 10 x base height or something
	XMFLOAT3A f3Velocity {};
	XMStoreFloat3A(&f3Velocity, rFrame.player.vecVelocity);
	mX3dAudioListener.OrientFront = {0.0f, 0.0f, -1.0f};
	mX3dAudioListener.OrientTop = {0.0f, -1.0f, 0.0f};
	mX3dAudioListener.Position = f3Position;
	mX3dAudioListener.Velocity = f3Velocity;

	for (const StaticVoice& rVoice : mStaticVoices)
	{
		Apply3dVolume(rVoice.mpVoice, rVoice.mVecPosition, rVoice.mVecVelocity, rVoice.mfFadeOutVolume * rVoice.mfVolume, rVoice.mfPitch);
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
	if (!StaticVoice::LoadXAudio2SourceVoice(mpAudioEngine.get(), pIXAudio2SourceVoice, audioCrc, true, b3d))
	{
		return nullptr;
	}

	CHECK_HRESULT(pIXAudio2SourceVoice->SetVolume(VolumeToPower(gMasterVolume.Get(), gSoundVolume.Get(), fVolume)));
	CHECK_HRESULT(pIXAudio2SourceVoice->SetFrequencyRatio(fPitch));
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
		Apply3dVolume(pIXAudio2SourceVoice, vecPosition, XMVectorZero(), fVolume, fPitch);
	}
}

void AudioManager::OnCriticalError()
{
	LOG("AudioManager::OnCriticalError()");
	ClearVoices();
}

void AudioManager::OnReset()
{
	LOG("AudioManager::OnReset()");
	ClearVoices();
}

void AudioManager::OnDestroyEngine() noexcept
{
	LOG("AudioManager::OnDestroyEngine()");
	ClearVoices();
}

void AudioManager::OnTrim()
{
	LOG("AudioManager::OnTrim()");
}

void AudioManager::OnDestroyParent() noexcept
{
	LOG("AudioManager::OnDestroyParent()");
}

} // namespace engine
