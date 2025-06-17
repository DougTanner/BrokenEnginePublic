#include "AudioManager.h"

#include "File/FileManager.h"

#include "Game.h"


namespace engine
{

using enum VoiceFlags;

MusicStream::~MusicStream()
{
	// Ensure voice is properly destroyed when stream is deleted
	// This is critical to prevent XAudio2 resource leaks
	if (pVoice != nullptr && gpAudioManager != nullptr && gpAudioManager->mpAudioEngine != nullptr)
	{
		// Stop the voice first to ensure no callbacks are pending
		pVoice->Stop();
		pVoice->FlushSourceBuffers();
		gpAudioManager->mpAudioEngine->DestroyVoice(pVoice);
		pVoice = nullptr;
	}
}

constexpr float kfCurveDistanceScaler = 10.0f;
constexpr float kfManualFadeStart = 0.0f;
constexpr float kfManualFadeEnd = 150.0f;
constexpr float kfManualFadeVolume = 0.05f;

// DT: GAMELOGIC
// Single combined music playlist containing all tracks
static std::vector<common::crc_t> sAllMusic =
{
	data::kAudioMusicdoodlewavCrc, 
	data::kAudioMusicMandatoryOvertimewavCrc, 
	data::kAudioMusicsong18wavCrc, 
	data::kAudioMusicTyhosibzzzzwavCrc,
	data::kAudioMusicS31UnexpectedTroublewavCrc, 
	data::kAudioMusicS31HighAlertwavCrc, 
	data::kAudioMusicS31OnPatrolwavCrc, 
	data::kAudioMusicS31TheGearsofProgresswavCrc
};

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

bool AudioManager::FillStreamBuffer(MusicStream& rStream, uint8_t* pBuffer, size_t bufferSize, size_t& rBytesRead, bool& rbLastBuffer)
{
	// Fill a streaming buffer with audio data from the chunk, ensuring block alignment
	rbLastBuffer = false;
	rBytesRead = 0;
	
	// Calculate the actual data offset in the chunk (skip WAV header)
	constexpr uint64_t kuiDataOffset = 0x4E;
	
	// Calculate how much data is remaining
	uint64_t uiRemainingData = rStream.uiDataChunkSize - rStream.uiCurrentPosition;
	if (uiRemainingData == 0)
	{
		rbLastBuffer = true;
		LOG("Music streaming: No remaining data to read, position: {}/{}", rStream.uiCurrentPosition, rStream.uiDataChunkSize);
		return false;
	}
	
	// Calculate how much to read, ensuring we don't exceed buffer size or remaining data
	size_t uiBytesToRead = std::min(static_cast<size_t>(uiRemainingData), bufferSize);
	
	// Ensure read size is aligned to ADPCM block boundaries
	if (rStream.uiBlockAlign > 0)
	{
		// Round down to nearest block boundary
		uiBytesToRead = (uiBytesToRead / rStream.uiBlockAlign) * rStream.uiBlockAlign;
		
		// If we would read 0 bytes but have remaining data, read at least one block
		if (uiBytesToRead == 0 && uiRemainingData >= rStream.uiBlockAlign)
		{
			uiBytesToRead = rStream.uiBlockAlign;
		}
	}
	
	// If no aligned data to read, we're at the end
	if (uiBytesToRead == 0)
	{
		rbLastBuffer = true;
		LOG("Music streaming: No aligned data to read, block align: {}, remaining: {}", rStream.uiBlockAlign, uiRemainingData);
		return false;
	}
	
	// Read the data from the chunk at the current position
	bool bSuccess = gpFileManager->ReadChunkData(rStream.chunkLocation.crc, kuiDataOffset + rStream.uiCurrentPosition, pBuffer, uiBytesToRead);
	
	if (!bSuccess)
	{
		LOG("Music streaming: Failed to read chunk data at position {}", rStream.uiCurrentPosition);
		return false;
	}
	
	// Update the current position and return bytes read
	rStream.uiCurrentPosition += uiBytesToRead;
	rBytesRead = uiBytesToRead;
	
	// Check if this is the last buffer
	if (rStream.uiCurrentPosition >= rStream.uiDataChunkSize)
	{
		rbLastBuffer = true;
	}
	
	LOG("Music streaming: Read {} bytes at position {}/{}, last buffer: {}", uiBytesToRead, rStream.uiCurrentPosition, rStream.uiDataChunkSize, rbLastBuffer);
	
	return true;
}

float AudioManager::GetMusicRemainingTime(const MusicStream& rStream) const
{
	// Note: This is called with mutex already locked by caller
	// Calculate remaining bytes in the stream
	uint64_t uiRemainingBytes = rStream.uiDataChunkSize - rStream.uiCurrentPosition;
	if (uiRemainingBytes == 0)
	{
		return 0.0f;
	}

	// Get the chunk to access format information
	if (!gpFileManager->IsChunkReady(rStream.chunkLocation.crc))
	{
		return 0.0f;
	}

	const LazyChunk& rLazyChunk = gpFileManager->GetLazyChunkMap().at(rStream.chunkLocation.crc);
	const ADPCMWAVEFORMAT* pAdpcmwaveformat = reinterpret_cast<const ADPCMWAVEFORMAT*>(&rLazyChunk.data[20]);

	// Simple calculation - only consider complete blocks since FillStreamBuffer enforces alignment
	uint64_t uiRemainingBlocks = uiRemainingBytes / rStream.uiBlockAlign;
	uint64_t uiTotalSamples = uiRemainingBlocks * pAdpcmwaveformat->wSamplesPerBlock;
	
	// Convert samples to time
	float fRemainingTime = static_cast<float>(uiTotalSamples) / static_cast<float>(pAdpcmwaveformat->wfx.nSamplesPerSec);
	
	return fRemainingTime;
}

void AudioManager::UpdateCrossFade(float fDeltaTime)
{
	// Note: This is called with mutex already locked by Update()
	float fMusicVolume = std::pow(gMasterVolume.Get(), 2.0f) * std::pow(gMusicVolume.Get(), 2.0f);
	if (mpCurrentMusicStream && mpCurrentMusicStream->pVoice != nullptr)
	{
		CHECK_HRESULT(mpCurrentMusicStream->pVoice->SetVolume(fMusicVolume));
	}

	switch (mCrossFadeState)
	{
	case CrossFadeState::kStarting:
		// Start the next music voice
		if (mpNextMusicStream && mpNextMusicStream->pVoice != nullptr)
		{
			// Ensure next voice starts at zero volume
			CHECK_HRESULT(mpNextMusicStream->pVoice->SetVolume(0.0f));
			CHECK_HRESULT(mpNextMusicStream->pVoice->Start());
			LOG("Music streaming: Started next music voice for cross-fade");
			mCrossFadeState = CrossFadeState::kActive;
		}
		else
		{
			// Failed to load next voice, reset state
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
			
			// Swap current and next (old stream destructor will clean up voice)
			mpCurrentMusicStream = std::move(mpNextMusicStream);
			mpNextMusicStream.reset();
			
			// Advance music index
			miMusicIndex = (miMusicIndex + 1) % sAllMusic.size();
			
			// Reset cross-fade state
			mCrossFadeState = CrossFadeState::kNone;
			mfCrossFadeProgress = 0.0f;
			
			LOG("Music streaming: Cross-fade complete, now playing track index {}", miMusicIndex);
		}
		else
		{
			// Calculate and apply cross-fade volumes
			// Using cosine interpolation for smooth fade
			float fCurrentMultiplier = std::cos(mfCrossFadeProgress * XM_PIDIV2); // PI/2 for 0 to 90 degrees
			float fNextMultiplier = std::sin(mfCrossFadeProgress * XM_PIDIV2);
			
			if (mpCurrentMusicStream && mpCurrentMusicStream->pVoice != nullptr)
			{
				CHECK_HRESULT(mpCurrentMusicStream->pVoice->SetVolume(fMusicVolume * fCurrentMultiplier));
			}
			
			if (mpNextMusicStream && mpNextMusicStream->pVoice != nullptr)
			{
				CHECK_HRESULT(mpNextMusicStream->pVoice->SetVolume(fMusicVolume * fNextMultiplier));
			}
		}
		break;

	default:
		break;
	}
}

bool AudioManager::LoadMusicVoice(std::unique_ptr<MusicStream>& rpStream, common::crc_t audioCrc)
{
	// Note: This function is called with mMusicStreamMutex already locked
	if (mpAudioEngine == nullptr || !mpAudioEngine->IsAudioDevicePresent()) [[unlikely]]
	{
		return false;
	}

	// DT: TEMP This needs to be removed
	// Check if audio chunk is ready with lazy loading
	if (!gpFileManager->IsChunkReady(audioCrc))
	{
		// Request loading with high priority for music
		gpFileManager->RequestChunkLoad(audioCrc, engine::LoadPriority::kHigh);
		return false;
	}

	const LazyChunk& rLazyChunk = gpFileManager->GetLazyChunkMap().at(audioCrc);
	const ADPCMWAVEFORMAT* pAdpcmwaveformat = reinterpret_cast<const ADPCMWAVEFORMAT*>(&rLazyChunk.data[20]);
	uint32_t uiDataChunkSize = *reinterpret_cast<const uint32_t*>(&rLazyChunk.data[0x4A]);

	LOG("Music streaming: Creating stream for CRC {:#018x}", audioCrc);

	// Create new music stream
	rpStream = std::make_unique<MusicStream>();
	MusicStream& rStream = *rpStream;
	
	// Create voice for streaming
	mpAudioEngine->AllocateVoice(reinterpret_cast<const WAVEFORMATEX*>(pAdpcmwaveformat), SoundEffectInstance_Default, false, &rStream.pVoice);
	CHECK_HRESULT(rStream.pVoice->SetVolume(0.0f));
	
	// Initialize stream with chunk info
	rStream.chunkLocation = rLazyChunk.chunkLocation;
	rStream.uiDataChunkSize = uiDataChunkSize;
	rStream.uiBlockAlign = pAdpcmwaveformat->wfx.nBlockAlign;
	rStream.uiCurrentPosition = 0;
	
	// Allocate 3 streaming buffers
	constexpr size_t kuiBufferSize = 65536;
	rStream.uiBufferSize = kuiBufferSize;
	
	// Round buffer size to block alignment
	if (rStream.uiBlockAlign > 0)
	{
		rStream.uiBufferSize = (rStream.uiBufferSize / rStream.uiBlockAlign) * rStream.uiBlockAlign;
	}
	
	LOG("Music streaming: Creating stream for CRC {:#018x}, data size: {} bytes, block align: {} bytes, buffer size: {} bytes", 
		audioCrc, uiDataChunkSize, rStream.uiBlockAlign, rStream.uiBufferSize);
	
	rStream.buffers.resize(3);
	for (auto& pBuffer : rStream.buffers)
	{
		pBuffer = std::make_unique<uint8_t[]>(rStream.uiBufferSize);
	}
	
	// Fill and submit the first buffer
	bool bLastBuffer = false;
	size_t bytesRead = 0;
	if (FillStreamBuffer(rStream, rStream.buffers[0].get(), rStream.uiBufferSize, bytesRead, bLastBuffer))
	{
		// Submit the first buffer
		XAUDIO2_BUFFER xaudio2Buffer
		{
			.Flags = bLastBuffer ? XAUDIO2_END_OF_STREAM : 0u,
			.AudioBytes = static_cast<UINT32>(bytesRead),
			.pAudioData = rStream.buffers[0].get(),
			.PlayBegin = 0,
			.PlayLength = 0,
			.LoopBegin = 0,
			.LoopLength = 0,
			.LoopCount = 0,
			.pContext = this,
		};
		CHECK_HRESULT(rStream.pVoice->SubmitSourceBuffer(&xaudio2Buffer));
		rStream.iActiveBuffer = 0;
		rStream.bStreamActive = true;
		rStream.bLastBufferSubmitted = bLastBuffer;
		
		LOG("Music streaming: Submitted initial buffer [0] with {} bytes, last buffer: {}", bytesRead, bLastBuffer);
	}
	
	return true;
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
		// Request loading with normal priority for sound effects
		gpFileManager->RequestChunkLoad(audioCrc, engine::LoadPriority::kNormal);
		return false;
	}

	const LazyChunk& rLazyChunk = gpFileManager->GetLazyChunkMap().at(audioCrc);
	const ADPCMWAVEFORMAT* pAdpcmwaveformat = reinterpret_cast<const ADPCMWAVEFORMAT*>(&rLazyChunk.data[20]);
	
	if (b3d)
	{
		// 3d sounds should have only one channel, re-export the sound as mono
		ASSERT(pAdpcmwaveformat->wfx.nChannels == 1);
	}
	
	uint32_t uiDataChunkSize = *reinterpret_cast<const uint32_t*>(&rLazyChunk.data[0x4A]);
	const BYTE* pData = reinterpret_cast<const BYTE*>(&rLazyChunk.data[0x4E]);

	// Create voice if needed
	if (rpVoice == nullptr)
	{
		mpAudioEngine->AllocateVoice(reinterpret_cast<const WAVEFORMATEX*>(pAdpcmwaveformat), SoundEffectInstance_Default, bOneShot, &rpVoice);
		CHECK_HRESULT(rpVoice->SetVolume(0.0f));
	}

	// Submit the entire sound buffer
	XAUDIO2_BUFFER xaudio2Buffer
	{
		.Flags = XAUDIO2_END_OF_STREAM,
		.AudioBytes = uiDataChunkSize,
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

void XM_CALLCONV AudioManager::Apply3d(IXAudio2SourceVoice* pIXAudio2SourceVoice, FXMVECTOR vecPosition, FXMVECTOR vecVelocity, float fVolume, float fPitch)
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

	CHECK_HRESULT(pIXAudio2SourceVoice->SetOutputMatrix(mpAudioEngine->GetMasterVoice(), 1, static_cast<UINT32>(iMasteringVoiceChannels), x3dAudioDspSettings.pMatrixCoefficients));
	CHECK_HRESULT(pIXAudio2SourceVoice->SetFrequencyRatio(x3dAudioDspSettings.DopplerFactor));

	/* Fails because AudioEngine doesn't set XAUDIO2_VOICE_USEFILTER
	XAUDIO2_FILTER_PARAMETERS filterParameters = {LowPassFilter, 2.0f * sinf(X3DAUDIO_PI / 6.0f * x3dAudioDspSettings.LPFDirectCoefficient), 1.0f};
	CHECK_HRESULT(pIXAudio2SourceVoice->SetFilterParameters(&filterParameters));
	*/

	// Apply custom volume
	float fSoundVolume = std::pow(gMasterVolume.Get(), 2.0f) * std::pow(gSoundVolume.Get(), 2.0f);
	float fDistance = common::Distance(vecPosition, mVecListenerPosition);
	if (fDistance < kfManualFadeStart)
	{
		CHECK_HRESULT(pIXAudio2SourceVoice->SetVolume(fSoundVolume * fVolume));
	}
	else if (fDistance < kfManualFadeEnd)
	{
		float fPercent = std::clamp((fDistance - kfManualFadeStart) / (kfManualFadeEnd - kfManualFadeStart), 0.0f, 1.0f);
		CHECK_HRESULT(pIXAudio2SourceVoice->SetVolume(fSoundVolume *  ((1.0f - fPercent) * fVolume + fPercent * kfManualFadeVolume)));
	}
	else
	{
		CHECK_HRESULT(pIXAudio2SourceVoice->SetVolume(fSoundVolume * kfManualFadeVolume));
	}

	CHECK_HRESULT(pIXAudio2SourceVoice->SetFrequencyRatio(fPitch));
}

void AudioManager::Update(const game::Frame& rFrame)
{
	if (mpAudioEngine != nullptr && !mpAudioEngine->IsAudioDevicePresent())
	{
		LOG("Music streaming: Audio device not present, resetting audio engine");
		mpAudioEngine->Reset();
		{
			std::lock_guard<std::mutex> lock(mMusicStreamMutex);
			// Note: Stream destructors will handle voice cleanup
			// But we need to null the voice pointers first since AudioEngine was reset
			if (mpCurrentMusicStream)
			{
				mpCurrentMusicStream->pVoice = nullptr;
			}
			if (mpNextMusicStream)
			{
				mpNextMusicStream->pVoice = nullptr;
			}
			mpCurrentMusicStream.reset();
			mpNextMusicStream.reset();
			mCrossFadeState = CrossFadeState::kNone;
			mfCrossFadeProgress = 0.0f;
		}
		mVoices.clear();
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
		if (!mpCurrentMusicStream || mpCurrentMusicStream->pVoice == nullptr)
		{
			LOG("Music streaming: Loading music, index: {}, CRC: {:#018x}", miMusicIndex, sAllMusic[miMusicIndex]);
			LoadMusicVoice(mpCurrentMusicStream, sAllMusic[miMusicIndex]);
			miMusicIndex = miMusicIndex == static_cast<int64_t>(sAllMusic.size()) - 1 ? 0 : miMusicIndex + 1;

			if (mpCurrentMusicStream && mpCurrentMusicStream->pVoice != nullptr)
			{
				CHECK_HRESULT(mpCurrentMusicStream->pVoice->Start());
				LOG("Music streaming: Started music voice");
			}
		}

		// Check if we need to start cross-fading
		if (mpCurrentMusicStream && mCrossFadeState == CrossFadeState::kNone)
		{
			float fRemaining = GetMusicRemainingTime(*mpCurrentMusicStream);
			if (fRemaining <= 4.0f && fRemaining > 0.0f && (!mpNextMusicStream || mpNextMusicStream->pVoice == nullptr))
			{
				// Load next track
				int64_t iNextIndex = (miMusicIndex + 1) % sAllMusic.size();
				LoadMusicVoice(mpNextMusicStream, sAllMusic[iNextIndex]);
				if (mpNextMusicStream && mpNextMusicStream->pVoice)
				{
					mCrossFadeState = CrossFadeState::kStarting;
					mfCrossFadeProgress = 0.0f;
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

			bValid |= rSound.iId == rVoice.iFrameId;
		}
		if (bValid)
		{
			++it;
			continue;
		}

		bool bDestroy = false;
		if (rVoice.flags & kFadingOut)
		{
			ASSERT(rVoice.fVolume > 0.0f);
			rVoice.fFadeOutVolume = std::max(0.0f, rVoice.fFadeOutVolume - (fDeltaTime / rVoice.fFadeOutTime) * rVoice.fVolume);
			if (rVoice.fVolume == 0.0f || rVoice.fFadeOutVolume == 0.0f)
			{
				bDestroy = true;
			}
		}
		else
		{
			if (rVoice.fVolume == 0.0f)
			{
				bDestroy = true;
			}
			else
			{
				rVoice.flags |= kFadingOut;
				rVoice.fFadeOutVolume = rVoice.fVolume;
			}
		}
		if (bDestroy)
		{
			CHECK_HRESULT(rVoice.pIXAudio2SourceVoice->Stop());
			mpAudioEngine->DestroyVoice(rVoice.pIXAudio2SourceVoice);
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
			bFound |= rVoice.iFrameId == rSound.iId;
		}
		if (bFound)
		{
			continue;
		}

		Voice voice
		{
			.flags = {},
			.iId = miNextId++,
			.iFrameId = rSound.iId,
			.fVolume = rSoundInfo.fVolume,
			.fPitch = rSoundInfo.fPitch,
			.fFadeOutVolume = rSoundInfo.fVolume,
			.fFadeOutTime = rSoundInfo.fFadeOutTime,
			.pIXAudio2SourceVoice = nullptr,
		};

		if (LoadVoice(voice.pIXAudio2SourceVoice, rSoundInfo.uiCrc, false, true))
		{
			float fSoundVolume = std::pow(gMasterVolume.Get(), 2.0f) * std::pow(gSoundVolume.Get(), 2.0f);
			CHECK_HRESULT(voice.pIXAudio2SourceVoice->SetVolume(fSoundVolume * voice.fVolume));
			CHECK_HRESULT(voice.pIXAudio2SourceVoice->Start());

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
			if (rVoice.iFrameId == rSound.iId)
			{
				pVoice = &rVoice;
			}
		}
		if (pVoice == nullptr)
		{
			continue;
		}

		pVoice->fVolume = rSoundInfo.fVolume;
		pVoice->fPitch = rSoundInfo.fPitch;
		pVoice->vecPosition = rSoundInfo.vecPosition;
		pVoice->vecVelocity = rSoundInfo.vecVelocity;
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
		Apply3d(rVoice.pIXAudio2SourceVoice, rVoice.vecPosition, rVoice.vecVelocity, rVoice.flags & kFadingOut ? rVoice.fFadeOutVolume : rVoice.fVolume, rVoice.fPitch);
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
	
	// Handle streaming buffer completion for current music stream
	if (mpCurrentMusicStream && mpCurrentMusicStream->bStreamActive && !mpCurrentMusicStream->bLastBufferSubmitted)
	{
		MusicStream& rStream = *mpCurrentMusicStream;
		
		// Find the next buffer to fill
		int iNextBuffer = (rStream.iActiveBuffer + 1) % static_cast<int>(rStream.buffers.size());
		
		// Fill the next buffer with audio data
		bool bLastBuffer = false;
		size_t bytesRead = 0;
		if (FillStreamBuffer(rStream, rStream.buffers[iNextBuffer].get(), rStream.uiBufferSize, bytesRead, bLastBuffer))
		{
			// Submit the filled buffer
			XAUDIO2_BUFFER xaudio2Buffer
			{
				.Flags = bLastBuffer ? XAUDIO2_END_OF_STREAM : 0u,
				.AudioBytes = static_cast<UINT32>(bytesRead),
				.pAudioData = rStream.buffers[iNextBuffer].get(),
				.PlayBegin = 0,
				.PlayLength = 0,
				.LoopBegin = 0,
				.LoopLength = 0,
				.LoopCount = 0,
				.pContext = this,
			};
			CHECK_HRESULT(rStream.pVoice->SubmitSourceBuffer(&xaudio2Buffer));
			rStream.iActiveBuffer = iNextBuffer;
			rStream.bLastBufferSubmitted = bLastBuffer;
		}
		else if (bLastBuffer)
		{
			// Mark stream as complete
			rStream.bStreamActive = false;
			rStream.bLastBufferSubmitted = true;
		}
	}
	
	// Handle streaming buffer completion for next music stream (during cross-fade)
	if (mpNextMusicStream && mpNextMusicStream->bStreamActive && !mpNextMusicStream->bLastBufferSubmitted)
	{
		MusicStream& rStream = *mpNextMusicStream;
		
		// Find the next buffer to fill
		int iNextBuffer = (rStream.iActiveBuffer + 1) % static_cast<int>(rStream.buffers.size());
		
		// Fill the next buffer with audio data
		bool bLastBuffer = false;
		size_t bytesRead = 0;
		if (FillStreamBuffer(rStream, rStream.buffers[iNextBuffer].get(), rStream.uiBufferSize, bytesRead, bLastBuffer))
		{
			// Submit the filled buffer
			XAUDIO2_BUFFER xaudio2Buffer
			{
				.Flags = bLastBuffer ? XAUDIO2_END_OF_STREAM : 0u,
				.AudioBytes = static_cast<UINT32>(bytesRead),
				.pAudioData = rStream.buffers[iNextBuffer].get(),
				.PlayBegin = 0,
				.PlayLength = 0,
				.LoopBegin = 0,
				.LoopLength = 0,
				.LoopCount = 0,
				.pContext = this,
			};
			CHECK_HRESULT(rStream.pVoice->SubmitSourceBuffer(&xaudio2Buffer));
			rStream.iActiveBuffer = iNextBuffer;
			rStream.bLastBufferSubmitted = bLastBuffer;
		}
		else if (bLastBuffer)
		{
			// Mark stream as complete
			rStream.bStreamActive = false;
			rStream.bLastBufferSubmitted = true;
		}
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
