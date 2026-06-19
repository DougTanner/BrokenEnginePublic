#include "AudioManager.h"

#if defined(BT_CLIENT)

#include "Profile/ProfileManager.h"

namespace engine
{

constexpr AUDIO_ENGINE_FLAGS kAudioEngineFlags = AudioEngine_UseMasteringLimiter;

AudioManager::AudioManager()
{
	ASSERT(gpAudioManager == nullptr);

	gpAudioManager = this;

	LOG(kAudio, kInfo, "\nAudioManager");

	try
	{
		// Find the id of the default audio endpoint
		Microsoft::WRL::ComPtr<IMMDeviceEnumerator> pMMDeviceEnumerator;
		CHECK_HRESULT(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(pMMDeviceEnumerator.GetAddressOf())));
		LOG(kAudio, kInfo, "  Got MMDeviceEnumerator");

		// Best-effort: look up the OS default endpoint id, but never abandon construction on failure — the
		// first-active-device fallback below covers a missing/failed default so a machine with active devices still gets audio
		Microsoft::WRL::ComPtr<IMMDevice> pDefaultAudioEndpoint;
		std::wstring defaultAudioEndpointId;
		if (pMMDeviceEnumerator->GetDefaultAudioEndpoint(eRender, eMultimedia, &pDefaultAudioEndpoint) == S_OK)
		{
			LOG(kAudio, kInfo, "  Got DefaultAudioEndpoint");

			LPWSTR pcDefaultDeviceId = nullptr;
			CHECK_HRESULT(pDefaultAudioEndpoint->GetId(&pcDefaultDeviceId));
			common::ScopedLambda freeDefaultDeviceId([=]()
			{
				CoTaskMemFree(pcDefaultDeviceId);
			});
			if (pcDefaultDeviceId != nullptr)
			{
				defaultAudioEndpointId = pcDefaultDeviceId;
				LOG(kAudio, kInfo, "    pcDeviceId: \"{}\"", defaultAudioEndpointId);
			}
			else
			{
				LOG(kAudio, kWarning, "  GetId returned nullptr; falling back to first active device");
			}
		}
		else
		{
			LOG(kAudio, kWarning, "  GetDefaultAudioEndpoint failed; falling back to first active device");
		}

		Microsoft::WRL::ComPtr<IMMDeviceCollection> pMMDeviceCollection;
		CHECK_HRESULT(pMMDeviceEnumerator->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &pMMDeviceCollection));
		if (pMMDeviceCollection == nullptr)
		{
			LOG(kAudio, kWarning, "  EnumAudioEndpoints returned nullptr; constructed without an audio device");
			return;
		}

		UINT uiCount = 0;
		CHECK_HRESULT(pMMDeviceCollection->GetCount(&uiCount));
		LOG(kAudio, kInfo, "  uiCount: {}", uiCount);

		// Match the OS default endpoint when its id is known; otherwise drop straight through to the first-active fallback
		if (!defaultAudioEndpointId.empty())
		{
			LOG(kAudio, kInfo, "  Searching for default audio endpoint: {}", defaultAudioEndpointId);
			for (UINT i = 0; i < uiCount; ++i)
			{
				Microsoft::WRL::ComPtr<IMMDevice> pMMDevice;
				CHECK_HRESULT(pMMDeviceCollection->Item(i, pMMDevice.GetAddressOf()));
				LPWSTR pcDeviceId = nullptr;
				CHECK_HRESULT(pMMDevice->GetId(&pcDeviceId));
				common::ScopedLambda freeDeviceId([=]()
				{
					CoTaskMemFree(pcDeviceId);
				});
				if (pcDeviceId == nullptr)
				{
					LOG(kAudio, kWarning, "  GetId returned nullptr; skipping device");
					continue;
				}
				std::wstring audioEndpointId(pcDeviceId);

				if (audioEndpointId.find(defaultAudioEndpointId) == std::wstring::npos)
				{
					continue;
				}

				mpAudioEngine = std::make_unique<AudioEngine>(kAudioEngineFlags, nullptr, audioEndpointId.c_str(), AudioCategory_GameEffects);
				LOG(kAudio, kInfo, "    Found: {}", audioEndpointId);
				break;
			}
		}

		if (mpAudioEngine == nullptr || !mpAudioEngine->IsAudioDevicePresent())
		{
			LOG(kAudio, kDebug, "  mpAudioEngine == nullptr || !mpAudioEngine->IsAudioDevicePresent()");

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
				if (pcDeviceId != nullptr)
				{
					std::wstring audioEndpointId(pcDeviceId);
					LOG(kAudio, kInfo, "    Using first in the list: {}", audioEndpointId);
					mpAudioEngine = std::make_unique<AudioEngine>(kAudioEngineFlags, nullptr, audioEndpointId.c_str(), AudioCategory_GameEffects);
				}
				else
				{
					LOG(kAudio, kWarning, "  GetId returned nullptr; no first-active device available");
				}
			}
		}

		if (mpAudioEngine != nullptr)
		{
			IXAudio2* pIXAudio2 = mpAudioEngine->GetInterface();
			XAUDIO2_DEBUG_CONFIGURATION debugConfiguration
			{
				.TraceMask = XAUDIO2_LOG_ERRORS | XAUDIO2_LOG_WARNINGS,
				.BreakMask = XAUDIO2_LOG_ERRORS,
				.LogThreadID = TRUE,
				.LogFileline = TRUE,
				.LogFunctionName = TRUE,
				.LogTiming = FALSE,
			};
			pIXAudio2->SetDebugConfiguration(&debugConfiguration);

			mpAudioEngine->RegisterNotify(this, false);

			IXAudio2MasteringVoice* pIXAudio2MasteringVoice = mpAudioEngine->GetMasterVoice();
			XAUDIO2_VOICE_DETAILS voiceDetails {};
			pIXAudio2MasteringVoice->GetVoiceDetails(&voiceDetails);
			DWORD uiChannelMask = 0;
			CHECK_HRESULT(pIXAudio2MasteringVoice->GetChannelMask(&uiChannelMask));

			char pcHex[20] {};
			LOG(kAudio, kInfo, "    Audio engine: channels {} channel mask {} rate {}", mpAudioEngine->GetOutputChannels(), common::ToHex(std::span(pcHex), mpAudioEngine->GetChannelMask()), mpAudioEngine->GetOutputSampleRate());
			LOG(kAudio, kInfo, "    Output format: channels {} channel mask {} format {}", mpAudioEngine->GetOutputFormat().Format.nChannels, common::ToHex(std::span(pcHex), mpAudioEngine->GetOutputFormat().dwChannelMask), mpAudioEngine->GetOutputFormat().Format.wFormatTag);
			LOG(kAudio, kInfo, "    MasteringVoice: channels {} channel mask {} sample rate {}", voiceDetails.InputChannels, common::ToHex(std::span(pcHex), uiChannelMask), voiceDetails.InputSampleRate);

			WAVEFORMATEXTENSIBLE waveFormatExtensible = mpAudioEngine->GetOutputFormat();
			miMasteringVoiceChannels = std::min(static_cast<int64_t>(voiceDetails.InputChannels), static_cast<int64_t>(waveFormatExtensible.Format.nChannels));

			mStaticVoices.Init(mpAudioEngine.get(), &miMasteringVoiceChannels);
			mStreamingVoices.Init(mpAudioEngine.get());
		}
	}
	catch ([[maybe_unused]] const std::exception& rException)
	{
		LOG(kDefault, kError, "Failed to create AudioManager: {}", rException.what());
		return;
	}
	catch (...)
	{
		LOG(kDefault, kError, "Failed to create AudioManager");
		return;
	}
}

AudioManager::~AudioManager()
{
	if (mpAudioEngine != nullptr)
	{
		mpAudioEngine->UnregisterNotify(this, false, false);
	}

	mStaticVoices.Clear(false);
	mStreamingVoices.Clear(false);

	if (mpAudioEngine != nullptr)
	{
		mpAudioEngine->Update();
	}

	if (gpAudioManager == this)
	{
		gpAudioManager = nullptr;
	}
}

void AudioManager::SetNextMusicTrackCallback(std::function<common::crc_t()> callback)
{
	mStreamingVoices.SetNextTrackCallback(std::move(callback));
}

void AudioManager::ClearVoices()
{
	mStaticVoices.Clear(true);
	mStreamingVoices.Clear(true);
}

void AudioManager::Suspend()
{
	if (mpAudioEngine == nullptr)
	{
		return;
	}

	mbSuspended.store(true, std::memory_order_release);
	mStaticVoices.SetSuspended(true);

	mpAudioEngine->Suspend();

	// Processing thread is now stopped — DestroyVoice returns instantly
	LOG(kAudio, kInfo, "Suspend: destroying {} static voices, {} streams", mStaticVoices.GetVoiceCount(), mStreamingVoices.GetStreamCount());
	mStaticVoices.Clear(false);
	mStreamingVoices.Clear(false);
}

void AudioManager::Resume()
{
	if (mpAudioEngine == nullptr)
	{
		return;
	}

	mbSuspended.store(false, std::memory_order_release);
	mStaticVoices.SetSuspended(false);
	mpAudioEngine->Resume();
	LOG(kAudio, kInfo, "Resume");
}

void AudioManager::PlayMusic(common::crc_t uiAudioCrc)
{
	mStreamingVoices.Play(uiAudioCrc);
}

void AudioManager::PlayOneShot(const game::Frame& rFrame, common::crc_t uiAudioCrc, bool b3d, float fVolume, float fPitch, float fPitchRange)
{
	mStaticVoices.PlayOneShot(rFrame, uiAudioCrc, b3d, fVolume, fPitch, fPitchRange);
}

void XM_CALLCONV AudioManager::PlayOneShot3d(const game::Frame& rFrame, common::crc_t uiAudioCrc, FXMVECTOR vecPosition, float fVolume, float fPitch, float fPitchRange)
{
	mStaticVoices.PlayOneShot3d(rFrame, uiAudioCrc, vecPosition, fVolume, fPitch, fPitchRange);
}

void AudioManager::Update(const game::Frame* pFrame)
{
	ScopedCpuProfile scopedCpuProfile(kCpuTimerAudio);

	// Heap: Voice map emplace/erase, make_unique<StreamingVoice> for track transitions, and
	// XAudio2 internal allocations (AllocateVoice, Update). Not controllable or pre-allocatable.
	ScopedSuppressAllocationTracking suppress;

	if (mbSuspended.load(std::memory_order_acquire))
	{
		return;
	}

	if (mbClearVoicesRequested.exchange(false, std::memory_order_acquire))
	{
		mStaticVoices.Clear(true);
	}

	if (mbClearStreamingVoicesRequested.exchange(false, std::memory_order_acquire))
	{
		mStreamingVoices.Clear(true);
	}

	if (mpAudioEngine != nullptr && !mpAudioEngine->IsAudioDevicePresent()) [[unlikely]]
	{
		LOG(kAudio, kWarning, "Music streaming: Audio device not present, resetting audio engine");

		mpAudioEngine->Reset();

		// Re-cache mastering voice channels after device reset
		{
			IXAudio2MasteringVoice* pMasterVoice = mpAudioEngine->GetMasterVoice();
			if (pMasterVoice != nullptr)
			{
				XAUDIO2_VOICE_DETAILS voiceDetails {};
				pMasterVoice->GetVoiceDetails(&voiceDetails);
				WAVEFORMATEXTENSIBLE waveFormat = mpAudioEngine->GetOutputFormat();
				miMasteringVoiceChannels = std::min(static_cast<int64_t>(voiceDetails.InputChannels), static_cast<int64_t>(waveFormat.Format.nChannels));
			}
		}

		// After Reset() is called, all XAudio2SourceVoices are destroyed internally to AudioEngine and their pointers must be set to nullptr
		ClearVoices();
	}

	if (mpAudioEngine == nullptr || !mpAudioEngine->IsAudioDevicePresent()) [[unlikely]]
	{
		return;
	}

	float fDeltaTime = common::NanosecondsToFloatSeconds<float>(mRealTime.GetDeltaNs(true));

	mStreamingVoices.CheckTrackTransition();
	mStreamingVoices.Update(fDeltaTime);

	if (pFrame != nullptr)
	{
		// Listener position must update first — UpdateLifecycle's priority/cull pass
		// reads mVecListenerPosition and mfEffectiveFadeEnd computed here.
		mStaticVoices.UpdateListenerPosition(*pFrame);
		mStaticVoices.UpdateLifecycle(*pFrame, fDeltaTime);
	}

	mStaticVoices.UpdateVolumes();

	gpProfileManager->SetCount(kCpuCounterSounds, mStaticVoices.GetVoiceCount());
	gpProfileManager->SetCount(kCpuCounterStreams, mStreamingVoices.GetStreamCount());

	mpAudioEngine->Update();
}

void AudioManager::OnCriticalError()
{
	LOG(kDefault, kError, "AudioManager::OnCriticalError()");

	// Destroy voices immediately — XAudio2 callback thread is no longer running
	mStaticVoices.Clear(false);
	mStreamingVoices.Clear(false);
}

void AudioManager::OnReset()
{
	LOG(kAudio, kDebug, "AudioManager::OnReset()");
	mbClearVoicesRequested.store(true, std::memory_order_release);
	mbClearStreamingVoicesRequested.store(true, std::memory_order_release);
}

void AudioManager::OnDestroyEngine() noexcept
{
	LOG(kAudio, kDebug, "AudioManager::OnDestroyEngine()");
	mbClearVoicesRequested.store(true, std::memory_order_release);
	mbClearStreamingVoicesRequested.store(true, std::memory_order_release);
}

void AudioManager::OnTrim()
{
	LOG(kAudio, kDebug, "AudioManager::OnTrim()");
}

void AudioManager::OnDestroyParent() noexcept
{
	LOG(kAudio, kDebug, "AudioManager::OnDestroyParent()");
}

} // namespace engine

#endif // defined(BT_CLIENT)
