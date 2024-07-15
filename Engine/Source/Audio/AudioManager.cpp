#include "AudioManager.h"

#include "File/FileManager.h"

#include "Game.h"

using namespace DirectX;

namespace engine
{

using enum VoiceFlags;

constexpr float kfCurveDistanceScaler = 10.0f;
constexpr float kfManualFadeStart = 0.0f;
constexpr float kfManualFadeEnd = 150.0f;
constexpr float kfManualFadeVolume = 0.05f;

// DT: GAMELOGIC
static std::vector<common::crc_t> sMenuMusics = {data::kAudioMusicdoodlewavCrc, data::kAudioMusicMandatoryOvertimewavCrc, data::kAudioMusicsong18wavCrc, data::kAudioMusicTyhosibzzzzwavCrc};
static std::vector<common::crc_t> sGameMusics = {data::kAudioMusicS31UnexpectedTroublewavCrc, data::kAudioMusicS31HighAlertwavCrc, data::kAudioMusicS31OnPatrolwavCrc, data::kAudioMusicS31TheGearsofProgresswavCrc};

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

			mpAudioEngine = std::make_unique<DirectX::AudioEngine>(AudioEngine_Default, nullptr, audioEndpointId.c_str(), AudioCategory_GameEffects);
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
				mpAudioEngine = std::make_unique<DirectX::AudioEngine>(AudioEngine_Default, nullptr, audioEndpointId.c_str(), AudioCategory_GameEffects);
			}
		}

		if (mpAudioEngine != nullptr)
		{
		#if 0 // defined(BT_DEBUG)
			IXAudio2* pIXAudio2 = mpAudioEngine->GetInterface();
			XAUDIO2_DEBUG_CONFIGURATION debugConfiguration {XAUDIO2_LOG_ERRORS | XAUDIO2_LOG_WARNINGS | XAUDIO2_LOG_DETAIL | XAUDIO2_LOG_API_CALLS | XAUDIO2_LOG_FUNC_CALLS, XAUDIO2_LOG_ERRORS | XAUDIO2_LOG_WARNINGS, true, true, true, false};
			pIXAudio2->SetDebugConfiguration(&debugConfiguration);
		#endif

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
	catch ([[maybe_unused]] std::exception& rException)
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
	gpAudioManager = nullptr;
}

void AudioManager::LoadVoice(IXAudio2SourceVoice*& rpVoice, common::crc_t audioCrc, bool bOneShot, bool bMusic, bool b3d)
{
	if (mpAudioEngine == nullptr || !mpAudioEngine->IsAudioDevicePresent()) [[unlikely]]
	{
		return;
	}

	const Chunk& rChunk = gpFileManager->GetDataChunkMap()[audioCrc];

	ADPCMWAVEFORMAT* pAdpcmwaveformat = reinterpret_cast<ADPCMWAVEFORMAT*>(&rChunk.pData[20]);
	if (b3d)
	{
		// 3d sounds should have only one channel, re-export the sound as mono
		ASSERT(pAdpcmwaveformat->wfx.nChannels == 1);
	}
	uint32_t uiDataChunkSize = *reinterpret_cast<uint32_t*>(&rChunk.pData[0x4A]);
	const BYTE* pData = reinterpret_cast<const BYTE*>(&rChunk.pData[0x4E]);

	if (rpVoice == nullptr)
	{
		mpAudioEngine->AllocateVoice(reinterpret_cast<WAVEFORMATEX*>(pAdpcmwaveformat), SoundEffectInstance_Default, bOneShot, &rpVoice);
		CHECK_HRESULT(rpVoice->SetVolume(0.0f));
	}

	XAUDIO2_BUFFER xaudio2Buffer
	{
		.Flags = bMusic ? 0u : XAUDIO2_END_OF_STREAM,
		.AudioBytes = uiDataChunkSize,
		.pAudioData = pData,
		.PlayBegin = 0,
		.PlayLength = 0,
		.LoopBegin = 0,
		.LoopLength = 0,
		.LoopCount = bOneShot || bMusic ? 0u : XAUDIO2_LOOP_INFINITE,
		.pContext = bMusic  ? this : nullptr,
	};
	// Error 0x88960001 here can mean mono/stereo .wav on same voice
	CHECK_HRESULT(rpVoice->SubmitSourceBuffer(&xaudio2Buffer));
}

void XM_CALLCONV AudioManager::Apply3d(IXAudio2SourceVoice* pIXAudio2SourceVoice, DirectX::FXMVECTOR vecPosition, DirectX::FXMVECTOR vecVelocity, float fVolume, float fPitch)
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
		LOG("mpAudioEngine->Reset()");
		mpAudioEngine->Reset();
		mpMenuMusicVoice = nullptr;
		mpGameMusicVoice = nullptr;
		mVoices.clear();
	}

	if (mpAudioEngine == nullptr || !mpAudioEngine->IsAudioDevicePresent()) [[unlikely]]
	{
		return;
	}

	ASSERT(rFrame.eFrameType == FrameType::kFull);
		
	// Music
	float fMusicVolume = std::pow(gMasterVolume.Get(), 2.0f) * std::pow(gMusicVolume.Get(), 2.0f);

	std::chrono::nanoseconds deltaNs = mRealTime.GetDeltaNs(true);
	float fDeltaTime = common::NanosecondsToFloatSeconds<float>(deltaNs);
	if (game::gpGame->mbMainMenuMusic)
	{
		mfMenuMusicVolume = std::clamp(mfMenuMusicVolume + fDeltaTime, 0.0f, 1.0f);
		mfGameMusicVolume = std::clamp(mfGameMusicVolume - fDeltaTime, 0.0f, 1.0f);
	}
	else
	{
		mfMenuMusicVolume = std::clamp(mfMenuMusicVolume - fDeltaTime, 0.0f, 1.0f);
		mfGameMusicVolume = std::clamp(mfGameMusicVolume + fDeltaTime, 0.0f, 1.0f);
	}

	if (mfMenuMusicVolume > 0.0f)
	{
		if (mpMenuMusicVoice == nullptr)
		{
			LoadVoice(mpMenuMusicVoice, sMenuMusics[miMenuMusicIndex], false, true, false);
			miMenuMusicIndex = miMenuMusicIndex == static_cast<int64_t>(sMenuMusics.size()) - 1 ? 0 : miMenuMusicIndex + 1;
			LoadVoice(mpMenuMusicVoice, sMenuMusics[miMenuMusicIndex], false, true, false);
			miMenuMusicIndex = miMenuMusicIndex == static_cast<int64_t>(sMenuMusics.size()) - 1 ? 0 : miMenuMusicIndex + 1;

			CHECK_HRESULT(mpMenuMusicVoice->Start());
		}

		CHECK_HRESULT(mpMenuMusicVoice->SetVolume(fMusicVolume * mfMenuMusicVolume));
	}
	else if (mfMenuMusicVolume == 0.0f && mpMenuMusicVoice != nullptr)
	{
		CHECK_HRESULT(mpMenuMusicVoice->Stop());
		mpAudioEngine->DestroyVoice(mpMenuMusicVoice);
		mpMenuMusicVoice = nullptr;
		miMenuMusicIndex = miGameMusicIndex == 0 ? static_cast<int64_t>(sGameMusics.size()) - 1 : miGameMusicIndex - 1;
	}

	if (mfGameMusicVolume > 0.0f)
	{
		if (mpGameMusicVoice == nullptr)
		{
			LoadVoice(mpGameMusicVoice, sGameMusics[miGameMusicIndex], false, true, false);
			miGameMusicIndex = miGameMusicIndex == static_cast<int64_t>(sGameMusics.size()) - 1 ? 0 : miGameMusicIndex + 1;
			LoadVoice(mpGameMusicVoice, sGameMusics[miGameMusicIndex], false, true, false);
			miGameMusicIndex = miGameMusicIndex == static_cast<int64_t>(sGameMusics.size()) - 1 ? 0 : miGameMusicIndex + 1;

			CHECK_HRESULT(mpGameMusicVoice->Start());
		}

		CHECK_HRESULT(mpGameMusicVoice->SetVolume(fMusicVolume * mfGameMusicVolume));
	}
	else if (mfGameMusicVolume == 0.0f && mpGameMusicVoice != nullptr)
	{
		CHECK_HRESULT(mpGameMusicVoice->Stop());
		mpAudioEngine->DestroyVoice(mpGameMusicVoice);
		mpGameMusicVoice = nullptr;
		miGameMusicIndex = miGameMusicIndex == 0 ? static_cast<int64_t>(sGameMusics.size()) - 1 : miGameMusicIndex - 1;
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

		LoadVoice(voice.pIXAudio2SourceVoice, rSoundInfo.uiCrc, false, false, true);
		float fSoundVolume = std::pow(gMasterVolume.Get(), 2.0f) * std::pow(gSoundVolume.Get(), 2.0f);
		CHECK_HRESULT(voice.pIXAudio2SourceVoice->SetVolume(fSoundVolume * voice.fVolume));
		CHECK_HRESULT(voice.pIXAudio2SourceVoice->Start());

		mVoices.push_back(std::move(voice));
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
	LoadVoice(pIXAudio2SourceVoice, audioCrc, true, false, b3d);
	float fSoundVolume = std::pow(gMasterVolume.Get(), 2.0f) * std::pow(gSoundVolume.Get(), 2.0f);
	pIXAudio2SourceVoice->SetVolume(fSoundVolume * fVolume);
	pIXAudio2SourceVoice->SetFrequencyRatio(fPitch);
	CHECK_HRESULT(pIXAudio2SourceVoice->Start(0, XAUDIO2_COMMIT_NOW));
	return pIXAudio2SourceVoice;
}

void XM_CALLCONV AudioManager::PlayOneShot(common::crc_t audioCrc, DirectX::FXMVECTOR vecPosition, float fVolume, float fPitch)
{
#if defined(BT_DEBUG)
	ASSERT(gCurrentFrameTypeProcessing == FrameType::kFull);
#endif

	if (mpAudioEngine == nullptr || !mpAudioEngine->IsAudioDevicePresent()) [[unlikely]]
	{
		return;
	}

	IXAudio2SourceVoice* pIXAudio2SourceVoice = PlayOneShot(audioCrc, true, fVolume, fPitch);
	Apply3d(pIXAudio2SourceVoice, vecPosition, XMVectorZero(), fVolume, fPitch);
}

void __cdecl AudioManager::OnBufferEnd()
{
	if (game::gpGame->mbMainMenuMusic)
	{
		LoadVoice(mpMenuMusicVoice, sMenuMusics[miMenuMusicIndex], false, true, false);
		miMenuMusicIndex = miMenuMusicIndex == static_cast<int64_t>(sMenuMusics.size()) - 1 ? 0 : miMenuMusicIndex + 1;
	}
	else
	{
		LoadVoice(mpGameMusicVoice, sGameMusics[miGameMusicIndex], false, true, false);
		miGameMusicIndex = miGameMusicIndex == static_cast<int64_t>(sGameMusics.size()) - 1 ? 0 : miGameMusicIndex + 1;
	}
}

} // namespace engine
