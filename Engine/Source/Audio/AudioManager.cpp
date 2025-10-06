#include "AudioManager.h"

#include "File/FileManager.h"

#include "Game.h"

namespace engine
{

using enum VoiceFlags;

static constexpr int64_t kiBufferSize = 16 * 1024;
static constexpr float kfCrossfadeDuration = 2.0f;

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
			LOG("  mpAudioEngine == nullptr");

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
	
	gpAudioManager = nullptr;
}

void AudioManager::SetMusicPlaylist(const std::vector<common::crc_t>& playlist)
{
	// Lock mutex for thread safety during music state changes
	std::lock_guard<std::mutex> lock(mMusicStreamMutex);
	
	// Copy the new playlist
	mMusicPlaylist = playlist;
	
	// Reset all music state
	mpCurrentMusicStream.reset();
	mpNextMusicStream.reset();
	mCrossFadeState = CrossFadeState::kNone;
	miMusicIndex = 0;
}

void AudioManager::UpdateCrossFade(float fDeltaTime)
{
	[[maybe_unused]] float fOldProgress = mfCrossFadeProgress; // DT: TEMP

	// Note: This is called with mutex already locked by Update()
	float fMusicVolume = std::pow(gMasterVolume.Get(), 2.0f) * std::pow(gMusicVolume.Get(), 2.0f);
	if (mpCurrentMusicStream && mpCurrentMusicStream->mpVoice != nullptr)
	{
		CHECK_HRESULT(mpCurrentMusicStream->mpVoice->SetVolume(fMusicVolume));
	}

	// Log current cross-fade state
	const char* pszStateName = "Unknown";
	switch (mCrossFadeState)
	{
	case CrossFadeState::kNone: pszStateName = "None"; break;
	case CrossFadeState::kStarting: pszStateName = "Starting"; break;
	case CrossFadeState::kActive: pszStateName = "Active"; break;
	}
	// LOG("Cross-fade: State={}, Progress={:.2f} ({:.3f}s/2.0s), DeltaTime={:.3f}, MusicVolume={:.3f}", pszStateName, mfCrossFadeProgress, mfCrossFadeProgress * 2.0f, fDeltaTime, fMusicVolume);

	switch (mCrossFadeState)
	{
	case CrossFadeState::kStarting:
		// Start the next music voice
		if (mpNextMusicStream && mpNextMusicStream->mpVoice != nullptr)
		{
			// Ensure next voice starts at zero volume
			CHECK_HRESULT(mpNextMusicStream->mpVoice->SetVolume(0.0f));
			CHECK_HRESULT(mpNextMusicStream->mpVoice->Start());
			LOG("Cross-fade: Started next music voice, beginning 2-second cross-fade transition");
			mCrossFadeState = CrossFadeState::kActive;
		}
		else
		{
			// Failed to load next voice, reset state
			LOG("Cross-fade: ERROR - Failed to load next voice, resetting to None state");
			mCrossFadeState = CrossFadeState::kNone;
		}
		break;

	case CrossFadeState::kActive:
		// Update cross-fade progress
		mfCrossFadeProgress += fDeltaTime / 2.0f; // 2 second fade duration
		
		if (mfCrossFadeProgress >= 1.0f)
		{
			// Cross-fade complete
			mfCrossFadeProgress = 1.0f;
			
			LOG("Cross-fade: Complete! Swapping streams, old index={}, new index={}", 
				miMusicIndex, (miMusicIndex + 1) % mMusicPlaylist.size());
			
			// Swap current and next (old stream destructor will clean up voice)
			mpCurrentMusicStream = std::move(mpNextMusicStream);
			mpNextMusicStream.reset();
			
			// Advance music index
			miMusicIndex = (miMusicIndex + 1) % mMusicPlaylist.size();
			
			// Reset cross-fade state
			mCrossFadeState = CrossFadeState::kNone;
			mfCrossFadeProgress = 0.0f;
			
			LOG("Cross-fade: Reset complete, now playing track index {}", miMusicIndex);
		}
		else
		{
			// Calculate and apply cross-fade volumes
			// Using cosine interpolation for smooth fade
			float fCurrentMultiplier = std::cos(mfCrossFadeProgress * XM_PIDIV2); // PI/2 for 0 to 90 degrees
			float fNextMultiplier = std::sin(mfCrossFadeProgress * XM_PIDIV2);
			
			// LOG("Cross-fade: Progress {:.2f}->{:.2f} ({:.3f}s of 2.0s), CurrentVol={:.3f}, NextVol={:.3f}", fOldProgress, mfCrossFadeProgress,  mfCrossFadeProgress * 2.0f, fMusicVolume * fCurrentMultiplier, fMusicVolume * fNextMultiplier);
			
			if (mpCurrentMusicStream && mpCurrentMusicStream->mpVoice != nullptr)
			{
				CHECK_HRESULT(mpCurrentMusicStream->mpVoice->SetVolume(fMusicVolume * fCurrentMultiplier));
			}
			
			if (mpNextMusicStream && mpNextMusicStream->mpVoice != nullptr)
			{
				CHECK_HRESULT(mpNextMusicStream->mpVoice->SetVolume(fMusicVolume * fNextMultiplier));
			}
		}
		break;

	default:
		break;
	}
}

bool AudioManager::LoadMusicVoice(std::unique_ptr<Voice>& rpStream, common::crc_t audioCrc)
{
	// Note: This function is called with mMusicStreamMutex already locked
	if (mpAudioEngine == nullptr || !mpAudioEngine->IsAudioDevicePresent()) [[unlikely]]
	{
		return false;
	}

	// Get lazy chunk and wave format
	const LazyChunk& rLazyChunk = gpFileManager->GetLazyChunkMap().at(audioCrc);
	ASSERT(rLazyChunk.data.size() == 0);
	const WAVEFORMATEX* pWaveFormat = &rLazyChunk.header.audioHeader.waveFormat;

	LOG("Music streaming: Creating stream for CRC {:#018x}", audioCrc);

	// Create new music voice
	rpStream = std::make_unique<Voice>();
	Voice& rVoice = *rpStream;

	// Create voice for streaming
	mpAudioEngine->AllocateVoice(pWaveFormat, SoundEffectInstance_Default, false, &rVoice.mpVoice);
	CHECK_HRESULT(rVoice.mpVoice->SetVolume(0.0f));

	// Initialize streaming data and allocate buffers
	return rVoice.InitializeMusicStream(audioCrc, this);
}

bool AudioManager::LoadVoice(IXAudio2SourceVoice*& rpVoice, common::crc_t audioCrc, bool bOneShot, bool b3d)
{
	if (mpAudioEngine == nullptr || !mpAudioEngine->IsAudioDevicePresent()) [[unlikely]]
	{
		return false;
	}

	// Check if audio chunk is ready with lazy loading
	if (!gpFileManager->IsChunkReady(audioCrc))
	{
		gpFileManager->RequestChunkLoad(audioCrc, engine::LoadPriority::kNormal);
		return false;
	}

	const LazyChunk& rLazyChunk = gpFileManager->GetLazyChunkMap().at(audioCrc);
	
	// Get WAVEFORMATEX from AudioHeader
	const WAVEFORMATEX* pWaveFormat = &rLazyChunk.header.audioHeader.waveFormat;
	
	if (b3d)
	{
		// 3d sounds should have only one channel, re-export the sound as mono
		ASSERT(rLazyChunk.header.audioHeader.waveFormat.nChannels == 1);
	}
	
	// Data is now at the beginning of the chunk data (no offset needed)
	const BYTE* pData = reinterpret_cast<const BYTE*>(rLazyChunk.data.data());

	// Create voice if needed
	if (rpVoice == nullptr)
	{
		mpAudioEngine->AllocateVoice(pWaveFormat, SoundEffectInstance_Default, bOneShot, &rpVoice);
		CHECK_HRESULT(rpVoice->SetVolume(0.0f));
	}

	// Submit the entire sound buffer
	XAUDIO2_BUFFER xaudio2Buffer
	{
		.Flags = XAUDIO2_END_OF_STREAM,
		.AudioBytes = static_cast<UINT32>(rLazyChunk.header.iSize),
		.pAudioData = pData,
		.PlayBegin = 0,
		.PlayLength = 0,
		.LoopBegin = 0,
		.LoopLength = 0,
		.LoopCount = bOneShot ? 0u : XAUDIO2_LOOP_INFINITE,
		.pContext = nullptr,
	};
	// Error 0x88960001 here can mean mono/stereo .wav on same voice
	CHECK_HRESULT(rpVoice->SubmitSourceBuffer(&xaudio2Buffer));

	return true;
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

	// Apply custom volume
	float fSoundVolume = std::pow(gMasterVolume.Get(), 2.0f) * std::pow(gSoundVolume.Get(), 2.0f);
	float fDistance = common::Distance(vecPosition, mVecListenerPosition);
	if (fDistance < kfManualFadeStart)
	{
		CHECK_HRESULT(pVoice->SetVolume(fSoundVolume * fVolume));
	}
	else if (fDistance < kfManualFadeEnd)
	{
		float fPercent = std::clamp((fDistance - kfManualFadeStart) / (kfManualFadeEnd - kfManualFadeStart), 0.0f, 1.0f);
		CHECK_HRESULT(pVoice->SetVolume(fSoundVolume *  ((1.0f - fPercent) * fVolume + fPercent * kfManualFadeVolume)));
	}
	else
	{
		CHECK_HRESULT(pVoice->SetVolume(fSoundVolume * kfManualFadeVolume));
	}

	CHECK_HRESULT(pVoice->SetFrequencyRatio(fPitch));
}

void AudioManager::Update(const game::Frame& rFrame)
{
	if (mpAudioEngine != nullptr && !mpAudioEngine->IsAudioDevicePresent())
	{
		LOG("Music streaming: Audio device not present, resetting audio engine");

		mpAudioEngine->Reset();

		// After Reset() is called, all voices are destroyed internally to AudioEngine and their pointers must be set to nullptr
		mVoices.clear();

		{
			std::lock_guard<std::mutex> lock(mMusicStreamMutex);

			if (mpCurrentMusicStream)
			{
				mpCurrentMusicStream->mpVoice = nullptr;
			}
			mpCurrentMusicStream.reset();

			if (mpNextMusicStream)
			{
				mpNextMusicStream->mpVoice = nullptr;
			}
			mpNextMusicStream.reset();

			mCrossFadeState = CrossFadeState::kNone;
			mfCrossFadeProgress = 0.0f;
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
		
		// Play music from the combined playlist
		if (!mpCurrentMusicStream || mpCurrentMusicStream->mpVoice == nullptr)
		{
			// Check if playlist is not empty before loading music
			if (!mMusicPlaylist.empty())
			{
				LOG("Music streaming: Loading music, index: {}, CRC: {:#018x}", miMusicIndex, mMusicPlaylist[miMusicIndex]);
				if (LoadMusicVoice(mpCurrentMusicStream, mMusicPlaylist[miMusicIndex]))
				{
					// Note: Do NOT increment index here - it will be incremented during cross-fade completion

					if (mpCurrentMusicStream && mpCurrentMusicStream->mpVoice != nullptr)
					{
						CHECK_HRESULT(mpCurrentMusicStream->mpVoice->Start());
						LOG("Music streaming: Started music voice");
					}
				}
			}
		}

		// Check if we need to start cross-fading
		if (mpCurrentMusicStream && mCrossFadeState == CrossFadeState::kNone)
		{
			float fRemaining = mpCurrentMusicStream->GetRemainingTime();
			// LOG("Music streaming: Current track remaining time: {:.2f}s, position: {}/{} bytes", fRemaining, mpCurrentMusicStream->iCurrentPosition, mpCurrentMusicStream->iDataChunkSize);
			
			// Also check if stream has ended (position >= size or last buffer submitted)
			bool bShouldStartCrossFade = (fRemaining <= kfCrossfadeDuration) || 
				(mpCurrentMusicStream->miCurrentPosition >= mpCurrentMusicStream->miDataChunkSize) ||
				(mpCurrentMusicStream->mbLastBufferSubmitted);
				
			if (bShouldStartCrossFade && (!mpNextMusicStream || mpNextMusicStream->mpVoice == nullptr))
			{
				// Load next track
				int64_t iNextIndex = (miMusicIndex + 1) % mMusicPlaylist.size();
				LOG("Music streaming: Starting cross-fade! Loading next track index {} (CRC: {:#018x})", 
					iNextIndex, mMusicPlaylist[iNextIndex]);
				
				if (LoadMusicVoice(mpNextMusicStream, mMusicPlaylist[iNextIndex]))
				{
					if (mpNextMusicStream && mpNextMusicStream->mpVoice)
					{
						mCrossFadeState = CrossFadeState::kStarting;
						mfCrossFadeProgress = 0.0f;
						LOG("Music streaming: Next track loaded successfully, cross-fade state set to Starting");
					}
					else
					{
						LOG("Music streaming: ERROR - Next track loaded but voice is null");
					}
				}
				else
				{
					LOG("Music streaming: ERROR - Failed to load next track for cross-fade");
				}
			}
		}

		UpdateCrossFade(fDeltaTime);
	}

	// Fade out and stop invalid voices
	for (auto it = mVoices.begin(); it != mVoices.end();)
	{
		Voice& rVoice = *it;

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
		if (rVoice.mFlags & kFadingOut)
		{
			ASSERT(rVoice.mfVolume > 0.0f);
			rVoice.mfFadeOutVolume = std::max(0.0f, rVoice.mfFadeOutVolume - (fDeltaTime / rVoice.mfFadeOutTime) * rVoice.mfVolume);
			if (rVoice.mfVolume == 0.0f || rVoice.mfFadeOutVolume == 0.0f)
			{
				bDestroy = true;
			}
		}
		else
		{
			if (rVoice.mfVolume == 0.0f)
			{
				bDestroy = true;
			}
			else
			{
				rVoice.mFlags |= kFadingOut;
				rVoice.mfFadeOutVolume = rVoice.mfVolume;
			}
		}

		if (bDestroy)
		{
			CHECK_HRESULT(rVoice.mpVoice->Stop());
			CHECK_HRESULT(rVoice.mpVoice->FlushSourceBuffers());
			mpAudioEngine->DestroyVoice(rVoice.mpVoice);
			it = mVoices.erase(it);
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

		bool bFound = false;
		for (const Voice& rVoice : mVoices)
		{
			bFound |= rVoice.miFrameId == rSound.iId;
		}
		if (bFound)
		{
			continue;
		}

		Voice voice;
		voice.mFlags = {};
		voice.miId = miNextId++;
		voice.miFrameId = rSound.iId;
		voice.mfVolume = rSoundInfo.fVolume;
		voice.mfPitch = rSoundInfo.fPitch;
		voice.mfFadeOutVolume = rSoundInfo.fVolume;
		voice.mfFadeOutTime = rSoundInfo.fFadeOutTime;
		voice.mpVoice = nullptr;

		if (LoadVoice(voice.mpVoice, rSoundInfo.uiCrc, false, true))
		{
			float fSoundVolume = std::pow(gMasterVolume.Get(), 2.0f) * std::pow(gSoundVolume.Get(), 2.0f);
			CHECK_HRESULT(voice.mpVoice->SetVolume(fSoundVolume * voice.mfVolume));
			CHECK_HRESULT(voice.mpVoice->Start());

			mVoices.push_back(std::move(voice));
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

		Voice* pVoice = nullptr;
		for (Voice& rVoice : mVoices)
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
		pVoice->mvecPosition = rSoundInfo.vecPosition;
		pVoice->mvecVelocity = rSoundInfo.vecVelocity;
	}

	// Calculate 3D volumes
	mVecListenerPosition = rFrame.player.vecPosition;
	XMFLOAT3A f3Position {};
	XMStoreFloat3A(&f3Position, rFrame.player.vecPosition);
	f3Position.z += 5.0f;
	XMFLOAT3A f3Velocity {};
	XMStoreFloat3A(&f3Velocity, rFrame.player.vecVelocity);
	mX3dAudioListener.OrientFront = {0.0f, 0.0f, -1.0f};
	mX3dAudioListener.OrientTop = {0.0f, -1.0f, 0.0f};
	mX3dAudioListener.Position = f3Position;
	mX3dAudioListener.Velocity = f3Velocity;

	for (const Voice& rVoice : mVoices)
	{
		Apply3d(rVoice.mpVoice, rVoice.mvecPosition, rVoice.mvecVelocity, rVoice.mFlags & kFadingOut ? rVoice.mfFadeOutVolume : rVoice.mfVolume, rVoice.mfPitch);
	}

	PROFILE_SET_COUNT(kCpuCounterSounds, mVoices.size());

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
	if (!LoadVoice(pIXAudio2SourceVoice, audioCrc, true, b3d))
	{
		return nullptr;
	}

	float fSoundVolume = std::pow(gMasterVolume.Get(), 2.0f) * std::pow(gSoundVolume.Get(), 2.0f);
	pIXAudio2SourceVoice->SetVolume(fSoundVolume * fVolume);
	pIXAudio2SourceVoice->SetFrequencyRatio(fPitch);
	CHECK_HRESULT(pIXAudio2SourceVoice->Start(0, XAUDIO2_COMMIT_NOW));
	return pIXAudio2SourceVoice;

}

void XM_CALLCONV AudioManager::PlayOneShot(common::crc_t audioCrc, FXMVECTOR vecPosition, float fVolume, float fPitch)
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

void AudioManager::OnBufferEnd()
{
	// Lock mutex to protect music stream data from concurrent access
	std::lock_guard<std::mutex> lock(mMusicStreamMutex);

	LOG("OnBufferEnd: Callback received, cross-fade state={}", mCrossFadeState == CrossFadeState::kNone ? "None" : mCrossFadeState == CrossFadeState::kStarting ? "Starting" : "Active");

	// Handle streaming buffer completion for current music stream
	if (mpCurrentMusicStream && mpCurrentMusicStream->mbStreamActive && !mpCurrentMusicStream->mbLastBufferSubmitted)
	{
		LOG("OnBufferEnd: Processing buffer for current stream");
		mpCurrentMusicStream->ProcessNextBuffer(this);
	}

	// Handle streaming buffer completion for next music stream (during cross-fade)
	if (mpNextMusicStream && mpNextMusicStream->mbStreamActive && !mpNextMusicStream->mbLastBufferSubmitted)
	{
		LOG("OnBufferEnd: Processing buffer for next stream (cross-fade active)");
		mpNextMusicStream->ProcessNextBuffer(this);
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
