#include "AudioManager.h"

#include "File/FileManager.h"
#include "Memory/MemoryManager.h"
#include "Profile/ProfileManager.h"

#include "Game.h"

namespace engine
{

// DT: GAMELOGIC
constexpr float kfCurveDistanceScaler = 10.0f;
constexpr float kfManualFadeStart = 0.0f;
constexpr float kfManualFadeEnd = 150.0f;
constexpr float kfManualFadeVolume = 0.05f;

constexpr AUDIO_ENGINE_FLAGS kAudioEngineFlags = AudioEngine_UseMasteringLimiter; //  | AudioEngine_Debug;

AudioManager::AudioManager()
{
	gpAudioManager = this;

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

			Log("    Audio engine: channels {} channel mask 0x{:X} rate {}", mpAudioEngine->GetOutputChannels(), mpAudioEngine->GetChannelMask(), mpAudioEngine->GetOutputSampleRate());
			Log("    Output format: channels {} channel mask 0x{:X} format {}", mpAudioEngine->GetOutputFormat().Format.nChannels, mpAudioEngine->GetOutputFormat().dwChannelMask, mpAudioEngine->GetOutputFormat().Format.wFormatTag);
			Log("    MasteringVoice: channels {} channel mask 0x{:X} sample rate {}", voiceDetails.InputChannels, uiChannelMask, voiceDetails.InputSampleRate);
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
	std::unique_ptr<StreamingVoice> currentStream;
	std::vector<std::unique_ptr<StreamingVoice>> previousStreams;

	{
		std::lock_guard<std::recursive_mutex> lock(mMusicStreamRecursiveMutex);

		currentStream = std::move(mpCurrentMusicStream);
		previousStreams = std::move(mPreviousStreams);
	}

	// Destruction happens here, after mutex is released
	currentStream.reset();
	previousStreams.clear();

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
	for (auto& [id, rStaticVoice] : mStaticVoices)
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

		for (std::unique_ptr<StreamingVoice>& pPreviousStream : mPreviousStreams)
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

	ScopedSuppressAllocationTracking suppressTracking;

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
	ScopedSuppressAllocationTracking suppressTracking;

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
	ASSERT(rFrame.interpolate.eFrameType == FrameType::kPostRender);

	ScopedSuppressAllocationTracking suppressTracking;

	if (mpAudioEngine != nullptr && !mpAudioEngine->IsAudioDevicePresent()) [[unlikely]]
	{
		Log("Music streaming: Audio device not present, resetting audio engine");

		mpAudioEngine->Reset();

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

	const SoundsInterpolate& rSoundsInterpolate = rFrame.interpolate.sounds;
	const SoundsPostRender& rSoundsPostRender = rFrame.postRender.sounds;

	// Fade out and stop invalid static voices
	for (auto it = mStaticVoices.begin(); it != mStaticVoices.end();)
	{
		StaticVoice& rVoice = it->second;

		bool bValid = false;
		for (int64_t i = 0; i < rSoundsPostRender.iCount; ++i)
		{
			bValid |= rSoundsPostRender.puiIds[i] == rVoice.mId;
		}
		if (bValid)
		{
			++it;
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
			it = mStaticVoices.erase(it);
		}
		else
		{
			++it;
		}
	}

	// Add new voices
	for (int64_t i = 0; i < rSoundsPostRender.iCount; ++i)
	{
		sound_t id = rSoundsPostRender.puiIds[i];
		uint64_t uiIndex = rSoundsInterpolate.IdToIndex(id);

		float fVolume = rSoundsInterpolate.pfVolumes[uiIndex];
		if (fVolume <= 0.0f)
		{
			continue;
		}

		if (mStaticVoices.contains(id))
		{
			continue;
		}

		common::crc_t uiCrc = rSoundsInterpolate.puiCrcs[uiIndex];
		IXAudio2SourceVoice* pVoice = nullptr;
		if (StaticVoice::LoadXAudio2SourceVoice(mpAudioEngine.get(), pVoice, uiCrc, false, true))
		{
			// StaticVoice takes ownership of pVoice
			float fPitch = rSoundsInterpolate.pfPitches[uiIndex];
			float fFadeOutTime = rSoundsInterpolate.pfFadeOutTimes[uiIndex];
			XMVECTOR vecPosition = rSoundsInterpolate.pVecPositions[uiIndex];
			XMVECTOR vecVelocity = rSoundsInterpolate.pVecVelocities[uiIndex];
			mStaticVoices.emplace(id, StaticVoice(pVoice, id, uiCrc, fVolume, fPitch, fFadeOutTime, vecPosition, vecVelocity));
		}
	}

	// Sync volume/positions
	for (int64_t i = 0; i < rSoundsPostRender.iCount; ++i)
	{
		sound_t id = rSoundsPostRender.puiIds[i];
		uint64_t uiIndex = rSoundsInterpolate.IdToIndex(id);

		auto itVoice = mStaticVoices.find(id);
		if (itVoice == mStaticVoices.end())
		{
			continue;
		}
		StaticVoice* pVoice = &itVoice->second;

		pVoice->mfVolume = rSoundsInterpolate.pfVolumes[uiIndex];
		pVoice->mfPitch = rSoundsInterpolate.pfPitches[uiIndex];
		pVoice->mVecPosition = rSoundsInterpolate.pVecPositions[uiIndex];
		pVoice->mVecVelocity = rSoundsInterpolate.pVecVelocities[uiIndex];
	}

	// Calculate 3D volumes
	mVecListenerPosition = rFrame.interpolate.player.vecPosition;
	XMFLOAT3A f3Position {};
	XMStoreFloat3A(&f3Position, rFrame.interpolate.player.vecPosition);
	f3Position.z += 5.0f; // DT: GAMELOGIC Should be constant in Gamelogic or based on 10 x base height or something
	XMFLOAT3A f3Velocity {};
	XMStoreFloat3A(&f3Velocity, rFrame.postRender.player.vecVelocity);
	mX3dAudioListener.OrientFront = {0.0f, 0.0f, -1.0f};
	mX3dAudioListener.OrientTop = {0.0f, -1.0f, 0.0f};
	mX3dAudioListener.Position = f3Position;
	mX3dAudioListener.Velocity = f3Velocity;

	for (const auto& [id, rVoice] : mStaticVoices)
	{
		Apply3dVolume(rVoice.mpVoice, rVoice.mVecPosition, rVoice.mVecVelocity, rVoice.mfFadeOutVolume * rVoice.mfVolume, rVoice.mfPitch);
	}

	gpProfileManager->SetCount(kCpuCounterSounds, mStaticVoices.size());

	// Update
	mpAudioEngine->Update();
}

IXAudio2SourceVoice* AudioManager::PlayOneShot([[maybe_unused]] const game::Frame& rFrame, common::crc_t audioCrc, bool b3d, float fVolume, float fPitch)
{
	ASSERT(rFrame.interpolate.eFrameType == FrameType::kPostRender);

	ScopedSuppressAllocationTracking suppressTracking;

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

void XM_CALLCONV AudioManager::PlayOneShot3d([[maybe_unused]] const game::Frame& rFrame, common::crc_t audioCrc, FXMVECTOR vecPosition, float fVolume, float fPitch)
{
	ASSERT(rFrame.interpolate.eFrameType == FrameType::kPostRender);

	if (mpAudioEngine == nullptr || !mpAudioEngine->IsAudioDevicePresent()) [[unlikely]]
	{
		return;
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
	ClearVoices();
}

void AudioManager::OnReset()
{
	Log("AudioManager::OnReset()");
	ClearVoices();
}

void AudioManager::OnDestroyEngine() noexcept
{
	Log("AudioManager::OnDestroyEngine()");
	ClearVoices();
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
