#include "AudioManager.h"

#if defined(BT_CLIENT)

#include "Profile/ProfileManager.h"

namespace engine
{

constexpr AUDIO_ENGINE_FLAGS kAudioEngineFlags = AudioEngine_UseMasteringLimiter;

// Pinned mastering-voice sample rate. Must match DataPacker's audiorepair::kiAudioExportSampleRate
// (packed audio is resampled to this) so source rate == mastering rate and XAudio2 bypasses per-voice
// SRC. Windows shared-mode does any final device-rate conversion once at the mastering output.
constexpr int kiMasteringSampleRate = 48000;

std::wstring AudioManager::GetEndpointId(IMMDevice* pDevice)
{
	LPWSTR pcDeviceId = nullptr;
	CHECK_HRESULT(pDevice->GetId(&pcDeviceId));
	common::ScopedLambda freeDeviceId([=]()
	{
		CoTaskMemFree(pcDeviceId);
	});
	return pcDeviceId != nullptr ? std::wstring(pcDeviceId) : std::wstring();
}

void AudioManager::CreateAudioEngineForEndpoint(const std::wstring& rEndpointId)
{
	mpAudioEngine = std::make_unique<AudioEngine>(kAudioEngineFlags, nullptr, rEndpointId.c_str(), AudioCategory_GameEffects);
}

std::wstring AudioManager::InitializeAudioEndpoint()
{
	Microsoft::WRL::ComPtr<IMMDeviceEnumerator> pMMDeviceEnumerator;
	CHECK_HRESULT(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(pMMDeviceEnumerator.GetAddressOf())));
	LOG(kAudio, kInfo, "  Got MMDeviceEnumerator");

	Microsoft::WRL::ComPtr<IMMDevice> pDefaultAudioEndpoint;
	std::wstring defaultAudioEndpointId;
	if (pMMDeviceEnumerator->GetDefaultAudioEndpoint(eRender, eMultimedia, &pDefaultAudioEndpoint) == S_OK)
	{
		LOG(kAudio, kInfo, "  Got DefaultAudioEndpoint");
		defaultAudioEndpointId = GetEndpointId(pDefaultAudioEndpoint.Get());
		if (!defaultAudioEndpointId.empty())
		{
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
		return {};
	}

	UINT uiCount = 0;
	CHECK_HRESULT(pMMDeviceCollection->GetCount(&uiCount));
	LOG(kAudio, kInfo, "  uiCount: {}", uiCount);

	std::wstring selectedDeviceId;
	if (!defaultAudioEndpointId.empty())
	{
		LOG(kAudio, kInfo, "  Searching for default audio endpoint: {}", defaultAudioEndpointId);
		for (UINT i = 0; i < uiCount; ++i)
		{
			Microsoft::WRL::ComPtr<IMMDevice> pMMDevice;
			CHECK_HRESULT(pMMDeviceCollection->Item(i, pMMDevice.GetAddressOf()));
			std::wstring audioEndpointId = GetEndpointId(pMMDevice.Get());
			if (audioEndpointId.empty())
			{
				LOG(kAudio, kWarning, "  GetId returned nullptr; skipping device");
				continue;
			}
			if (audioEndpointId.find(defaultAudioEndpointId) == std::wstring::npos)
			{
				continue;
			}

			CreateAudioEngineForEndpoint(audioEndpointId);
			selectedDeviceId = audioEndpointId;
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
			std::wstring audioEndpointId = GetEndpointId(pMMDevice.Get());
			if (!audioEndpointId.empty())
			{
				LOG(kAudio, kInfo, "    Using first in the list: {}", audioEndpointId);
				CreateAudioEngineForEndpoint(audioEndpointId);
				selectedDeviceId = audioEndpointId;
			}
			else
			{
				LOG(kAudio, kWarning, "  GetId returned nullptr; no first-active device available");
			}
		}
	}
	return selectedDeviceId;
}

void AudioManager::CacheMasteringVoiceChannels()
{
	IXAudio2MasteringVoice* pMasterVoice = mpAudioEngine->GetMasterVoice();
	if (pMasterVoice == nullptr)
	{
		return;
	}
	XAUDIO2_VOICE_DETAILS voiceDetails {};
	pMasterVoice->GetVoiceDetails(&voiceDetails);
	WAVEFORMATEXTENSIBLE waveFormat = mpAudioEngine->GetOutputFormat();
	miMasteringVoiceChannels = std::min(static_cast<int64_t>(voiceDetails.InputChannels), static_cast<int64_t>(waveFormat.Format.nChannels));
}

void AudioManager::InitializeAudioSubsystems(const std::wstring& rSelectedDeviceId)
{
	mPinnedOutputFormat.wFormatTag = WAVE_FORMAT_PCM;
	mPinnedOutputFormat.nChannels = static_cast<WORD>(mpAudioEngine->GetOutputChannels());
	mPinnedOutputFormat.nSamplesPerSec = static_cast<DWORD>(kiMasteringSampleRate);
	mPinnedOutputFormat.wBitsPerSample = 16;
	mPinnedOutputFormat.nBlockAlign = static_cast<WORD>(mPinnedOutputFormat.nChannels * (mPinnedOutputFormat.wBitsPerSample / 8));
	mPinnedOutputFormat.nAvgBytesPerSec = mPinnedOutputFormat.nSamplesPerSec * mPinnedOutputFormat.nBlockAlign;
	mPinnedOutputFormat.cbSize = 0;
	if (mpAudioEngine->GetOutputSampleRate() != kiMasteringSampleRate)
	{
		LOG(kAudio, kInfo, "  Pinning mastering voice to {} Hz (device native {} Hz)", kiMasteringSampleRate, mpAudioEngine->GetOutputSampleRate());
		if (!mpAudioEngine->Reset(&mPinnedOutputFormat, rSelectedDeviceId.empty() ? nullptr : rSelectedDeviceId.c_str()))
		{
			LOG(kAudio, kWarning, "  Mastering-rate pin failed; continuing at device rate");
		}
	}

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

	IXAudio2MasteringVoice* pMasteringVoice = mpAudioEngine->GetMasterVoice();
	if (pMasteringVoice != nullptr)
	{
		XAUDIO2_VOICE_DETAILS voiceDetails {};
		pMasteringVoice->GetVoiceDetails(&voiceDetails);
		DWORD uiChannelMask = 0;
		CHECK_HRESULT(pMasteringVoice->GetChannelMask(&uiChannelMask));

		char pcHex[20] {};
		LOG(kAudio, kInfo, "    Audio engine: channels {} channel mask {} rate {}", mpAudioEngine->GetOutputChannels(), common::ToHex(std::span(pcHex), mpAudioEngine->GetChannelMask()), mpAudioEngine->GetOutputSampleRate());
		LOG(kAudio, kInfo, "    Output format: channels {} channel mask {} format {}", mpAudioEngine->GetOutputFormat().Format.nChannels, common::ToHex(std::span(pcHex), mpAudioEngine->GetOutputFormat().dwChannelMask), mpAudioEngine->GetOutputFormat().Format.wFormatTag);
		LOG(kAudio, kInfo, "    MasteringVoice: channels {} channel mask {} sample rate {}", voiceDetails.InputChannels, common::ToHex(std::span(pcHex), uiChannelMask), voiceDetails.InputSampleRate);
	}

	CacheMasteringVoiceChannels();
	mStaticVoices.Init(mpAudioEngine.get(), &miMasteringVoiceChannels);
	mStreamingVoices.Init(mpAudioEngine.get());
}

AudioManager::AudioManager()
{
	ASSERT(gpAudioManager == nullptr);

	gpAudioManager = this;

	LOG(kAudio, kInfo, "\nAudioManager");

	try
	{
		// Find the id of the default audio endpoint
		std::wstring selectedDeviceId = InitializeAudioEndpoint();

		if (mpAudioEngine != nullptr)
		{
			InitializeAudioSubsystems(selectedDeviceId);
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

	ClearVoices(false);

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

void AudioManager::ClearVoices(bool bNullVoicesBeforeDestroy)
{
	mStaticVoices.Clear(bNullVoicesBeforeDestroy);
	mStreamingVoices.Clear(bNullVoicesBeforeDestroy);
}

void AudioManager::Suspend()
{
	if (mpAudioEngine == nullptr)
	{
		return;
	}

	mbSuspended.store(true, std::memory_order_release);

	mpAudioEngine->Suspend();

	// Processing thread is now stopped — DestroyVoice returns instantly
	LOG(kAudio, kInfo, "Suspend: destroying {} static voices, {} streams", mStaticVoices.GetVoiceCount(), mStreamingVoices.GetStreamCount());
	ClearVoices(false);
}

void AudioManager::Resume()
{
	if (mpAudioEngine == nullptr)
	{
		return;
	}

	mbSuspended.store(false, std::memory_order_release);
	mpAudioEngine->Resume();
	LOG(kAudio, kInfo, "Resume");
}

void AudioManager::PlayMusic(common::crc_t uiAudioCrc)
{
	if (mbSuspended.load(std::memory_order_acquire))
	{
		return;
	}
	mStreamingVoices.Play(uiAudioCrc);
}

void AudioManager::PlayOneShot(const game::Frame& rFrame, common::crc_t uiAudioCrc, bool b3d, float fVolume, float fPitch, float fPitchRange)
{
	if (mbSuspended.load(std::memory_order_acquire))
	{
		return;
	}
	mStaticVoices.PlayOneShot(rFrame, uiAudioCrc, b3d, fVolume, fPitch, fPitchRange);
}

void XM_CALLCONV AudioManager::PlayOneShot3d(const game::Frame& rFrame, common::crc_t uiAudioCrc, FXMVECTOR vecPosition, float fVolume, float fPitch, float fPitchRange)
{
	if (mbSuspended.load(std::memory_order_acquire))
	{
		return;
	}
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
		ClearVoices(true);
	}

	if (mpAudioEngine != nullptr && !mpAudioEngine->IsAudioDevicePresent()) [[unlikely]]
	{
		LOG(kAudio, kWarning, "Music streaming: Audio device not present, resetting audio engine");

		// Re-apply the pinned 48 kHz format (native channels captured at startup) so the mastering voice keeps the
		// SRC bypass; Reset(nullptr) would otherwise recreate it at the new default device's rate. DirectXTK reports
		// failure two ways: Reset returns false for a missing/busy device, but THROWS for any other failure — including
		// CreateMasteringVoice failing when the pinned channel count exceeds the new device's (e.g. a 7.1 -> stereo
		// hot-swap). Guard both at this trust boundary (the ctor guards Reset the same way) and fall back to the
		// device-default format, which requests the device's own channels/rate and so cannot mismatch (only the SRC
		// bypass is lost until the next launch re-pins).
		bool bMasteringReset = false;
		try
		{
			bMasteringReset = mpAudioEngine->Reset(&mPinnedOutputFormat, nullptr);
		}
		catch (const std::exception&)
		{
			LOG(kAudio, kWarning, "Pinned mastering format Reset threw (device incompatible with the pinned channel count); falling back to device-default format");
		}

		if (!bMasteringReset)
		{
			try
			{
				mpAudioEngine->Reset(nullptr, nullptr);
			}
			catch (const std::exception&)
			{
				// Device fully unusable; the IsAudioDevicePresent() retry loop below re-enters the reset path next frame.
				LOG(kAudio, kWarning, "Device-default format Reset also failed; audio stays off until the device recovers");
			}
		}

		// A successful DirectXTK Reset synchronously calls OnReset. This explicit clear owns that
		// reset; consume its deferred request so it cannot clear a newly restarted track next frame.
		// A genuinely later callback store after this exchange remains armed for the next Update.
		mbClearVoicesRequested.exchange(false, std::memory_order_acquire);

		CacheMasteringVoiceChannels();

		// After Reset() is called, all XAudio2SourceVoices are destroyed internally to AudioEngine and their pointers must be set to nullptr
		ClearVoices(true);
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
		mStaticVoices.UpdateListenerPosition();
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
	ClearVoices(false);
}

void AudioManager::OnReset()
{
	LOG(kAudio, kDebug, "AudioManager::OnReset()");
	mbClearVoicesRequested.store(true, std::memory_order_release);
}

void AudioManager::OnDestroyEngine() noexcept
{
	LOG(kAudio, kDebug, "AudioManager::OnDestroyEngine()");
	mbClearVoicesRequested.store(true, std::memory_order_release);
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
