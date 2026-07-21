#include "AudioManager.h"

#if defined(BT_CLIENT)

#include "StaticVoices.h"
#include "StreamingVoices.h"

#include "Profile/ProfileManager.h"

namespace engine
{

constexpr AUDIO_ENGINE_FLAGS kAudioEngineFlags = AudioEngine_UseMasteringLimiter;

// Pinned mastering-voice sample rate. Must match DataPacker's audiorepair::kiAudioExportSampleRate
// (packed audio is resampled to this) so source rate == mastering rate and XAudio2 bypasses per-voice
// SRC. Windows shared-mode does any final device-rate conversion once at the mastering output.
constexpr int kiMasteringSampleRate = 48000;

// Silent-start recovery probe interval, in Update frames. A deviceless machine holds the silent engine
// forever, so an unthrottled probe would warn + full-reset every frame; ~2 s at 60 fps latency to pick up
// a newly attached device is imperceptible against that cost.
constexpr int64_t kiSilentRecoveryRetryFrames = 120;

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

	// Enumeration-dependent selection. A null collection is not fatal: fall through to the OS-default
	// construction below so the stable-engine contract still holds (silent engine instead of null).
	std::wstring selectedDeviceId;
	if (pMMDeviceCollection != nullptr)
	{
		UINT uiCount = 0;
		CHECK_HRESULT(pMMDeviceCollection->GetCount(&uiCount));
		LOG(kAudio, kInfo, "  uiCount: {}", uiCount);

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
	}
	else
	{
		LOG(kAudio, kInfo, "  EnumAudioEndpoints returned an empty collection; constructing silent-capable engine against the OS default");
	}

	// Always yield one stable engine. When no endpoint produced one (no active device, a found device
	// with a null id, or a null endpoint collection), construct against the OS default (deviceId == nullptr): with the mastering limiter
	// flag and no AudioEngine_ThrowOnNoAudioHW, DirectXTK keeps a silent-mode shell instead of throwing.
	// The voice subsystems receive this pointer once and it is never recreated — only Reset recovers it.
	if (mpAudioEngine == nullptr)
	{
		LOG(kAudio, kWarning, "  No usable audio endpoint; constructing silent-capable engine against the OS default");
		mpAudioEngine = std::make_unique<AudioEngine>(kAudioEngineFlags, nullptr, nullptr, AudioCategory_GameEffects);
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

WAVEFORMATEX AudioManager::MakePinnedOutputFormat(WORD uiChannels) const
{
	WAVEFORMATEX format {};
	format.wFormatTag = WAVE_FORMAT_PCM;
	format.nChannels = uiChannels;
	format.nSamplesPerSec = static_cast<DWORD>(kiMasteringSampleRate);
	format.wBitsPerSample = 16;
	format.nBlockAlign = static_cast<WORD>(format.nChannels * (format.wBitsPerSample / 8));
	format.nAvgBytesPerSec = format.nSamplesPerSec * format.nBlockAlign;
	format.cbSize = 0;
	return format;
}

void AudioManager::ConfigureLiveGraph(const std::wstring& rSelectedDeviceId)
{
	// A silent-mode engine has no live output graph (GetOutputChannels() == 0). Defer all pinned-format
	// setup — mPinnedOutputFormat stays empty and kPinnedFormatValid false — so Update recovery discovers
	// native channels from the OS default when a device later appears.
	if (mpAudioEngine->GetOutputChannels() == 0)
	{
		LOG(kAudio, kInfo, "  No live audio graph at startup; running silent until a device appears");
		return;
	}

	mPinnedOutputFormat = MakePinnedOutputFormat(static_cast<WORD>(mpAudioEngine->GetOutputChannels()));
	if (mpAudioEngine->GetOutputSampleRate() != kiMasteringSampleRate)
	{
		LOG(kAudio, kInfo, "  Pinning mastering voice to {} Hz (device native {} Hz)", kiMasteringSampleRate, mpAudioEngine->GetOutputSampleRate());
		// Reset goes silent before migrating (AudioEngine.cpp SetSilentMode), so a failed pin leaves the
		// graph silent — fall back to the device-default format to restore a usable graph. DirectXTK reports
		// failure two ways: false for a missing/busy device, a throw for other failures (e.g. the pinned
		// channel count exceeding the device's); guard both at this trust boundary.
		bool bPinned = false;
		try
		{
			bPinned = mpAudioEngine->Reset(&mPinnedOutputFormat, rSelectedDeviceId.empty() ? nullptr : rSelectedDeviceId.c_str());
		}
		catch (const std::exception&)
		{
			LOG(kAudio, kWarning, "  Pinned mastering format Reset threw; falling back to device-default format");
		}
		if (!bPinned)
		{
			LOG(kAudio, kWarning, "  Mastering-rate pin failed; falling back to device-default format");
			try
			{
				mpAudioEngine->Reset(nullptr, rSelectedDeviceId.empty() ? nullptr : rSelectedDeviceId.c_str());
			}
			catch (const std::exception&)
			{
				LOG(kAudio, kWarning, "  Device-default format Reset also failed; audio stays silent until the device recovers");
			}
		}
	}

	// The pin/fallback Resets may have left the graph silent (both failed). Only touch the live interface —
	// debug config, mastering diagnostics, channel cache, pinned-format validity — when one exists.
	if (!mpAudioEngine->IsAudioDevicePresent())
	{
		LOG(kAudio, kWarning, "  Audio graph went silent during mastering-rate pin; no live format available");
		return;
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
	mFlags.Set(AudioManagerFlags::kPinnedFormatValid);
}

void AudioManager::InitializeAudioSubsystems(const std::wstring& rSelectedDeviceId)
{
	// Live-graph configuration (skips itself in silent mode); runs before ownership attach so its pin Reset
	// is not observed as a notification.
	ConfigureLiveGraph(rSelectedDeviceId);

	// One-time ownership attach — exactly once against the stable engine, even when silent. RegisterNotify
	// deliberately follows ConfigureLiveGraph so the initial pin attempt cannot deliver an OnReset here.
	mpAudioEngine->RegisterNotify(this, false);
	mpStaticVoices->Init(mpAudioEngine.get(), &miMasteringVoiceChannels);
	mpStreamingVoices->Init(mpAudioEngine.get());
}

AudioManager::AudioManager()
: mpStaticVoices(std::make_unique<StaticVoices>())
, mpStreamingVoices(std::make_unique<StreamingVoices>())
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
	mpStreamingVoices->SetNextTrackCallback(std::move(callback));
}

void AudioManager::SkipNextStaticVoiceInvalidation()
{
	mpStaticVoices->SkipNextInvalidation();
}

void AudioManager::ClearVoices(bool bNullVoicesBeforeDestroy)
{
	mpStaticVoices->Clear(bNullVoicesBeforeDestroy);
	mpStreamingVoices->Clear(bNullVoicesBeforeDestroy);
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
	LOG(kAudio, kInfo, "Suspend: destroying {} static voices, {} streams", mpStaticVoices->GetVoiceCount(), mpStreamingVoices->GetStreamCount());
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
	mpStreamingVoices->Play(uiAudioCrc);
}

void AudioManager::PlayOneShot(const game::Frame& rFrame, common::crc_t uiAudioCrc, bool b3d, float fVolume, float fPitch, float fPitchRange)
{
	if (mbSuspended.load(std::memory_order_acquire))
	{
		return;
	}
	mpStaticVoices->PlayOneShot(rFrame, uiAudioCrc, b3d, fVolume, fPitch, fPitchRange);
}

void XM_CALLCONV AudioManager::PlayOneShot3d(const game::Frame& rFrame, common::crc_t uiAudioCrc, FXMVECTOR vecPosition, float fVolume, float fPitch, float fPitchRange)
{
	if (mbSuspended.load(std::memory_order_acquire))
	{
		return;
	}
	mpStaticVoices->PlayOneShot3d(rFrame, uiAudioCrc, vecPosition, fVolume, fPitch, fPitchRange);
}

void AudioManager::FinishDeviceReset()
{
	// A successful DirectXTK Reset synchronously calls OnReset. This explicit clear owns that reset; consume
	// its deferred request so it cannot clear a newly restarted track next frame. A genuinely later callback
	// store after this exchange remains armed for the next Update. Harmless when Reset failed (nothing set it).
	mbClearVoicesRequested.exchange(false, std::memory_order_acquire);

	CacheMasteringVoiceChannels();

	// After Reset() is called, all XAudio2SourceVoices are destroyed internally to AudioEngine and their pointers must be set to nullptr
	ClearVoices(true);
}

void AudioManager::AttemptSilentEngineRecovery()
{
	// No device was present when the engine was constructed (or a startup pin left it silent), so native
	// channels were never discovered. Reset to the OS default to bring up a live graph and learn its channel
	// count, then derive + pin the 48 kHz format. Reset guards match the trust boundary elsewhere.
	if (!(mFlags & AudioManagerFlags::kSilentRecoveryLogged))
	{
		LOG(kAudio, kWarning, "Audio device absent; probing for a device to recover");
		mFlags.Set(AudioManagerFlags::kSilentRecoveryLogged);
	}

	bool bDefaultReset = false;
	try
	{
		bDefaultReset = mpAudioEngine->Reset(nullptr, nullptr);
	}
	catch (const std::exception&)
	{
		// Still silent; the throttled retry re-enters next interval.
	}

	if (!bDefaultReset || !mpAudioEngine->IsAudioDevicePresent())
	{
		return;
	}

	// A live graph exists now. Derive the pinned format from the discovered native channels and pin to
	// 48 kHz — unless the device is already native 48 kHz (mirror startup's skip to avoid a needless graph
	// teardown/rebuild). A failed pin goes silent, so restore the device-default graph on false/throw.
	mPinnedOutputFormat = MakePinnedOutputFormat(static_cast<WORD>(mpAudioEngine->GetOutputChannels()));
	if (mpAudioEngine->GetOutputSampleRate() != kiMasteringSampleRate)
	{
		// The default Reset above brought the graph live, so these pin/fallback Resets run against a live
		// xaudio2 — DirectXTK's Reset synchronously delivers the migration OnCriticalError (SetSilentMode).
		// Suppress it across this window (main-thread synchronous, so only the expected notification arrives)
		// so recovery is not logged as a false critical error with a redundant voice clear.
		mFlags.Set(AudioManagerFlags::kExpectedResetInProgress);
		bool bPinned = false;
		try
		{
			bPinned = mpAudioEngine->Reset(&mPinnedOutputFormat, nullptr);
		}
		catch (const std::exception&)
		{
			LOG(kAudio, kWarning, "Pinned mastering format Reset threw during silent recovery; restoring device-default graph");
		}
		if (!bPinned)
		{
			try
			{
				mpAudioEngine->Reset(nullptr, nullptr);
			}
			catch (const std::exception&)
			{
				LOG(kAudio, kWarning, "Device-default Reset failed after pin failure during silent recovery; staying silent");
			}
		}
		mFlags.Clear(AudioManagerFlags::kExpectedResetInProgress);
	}

	// Only claim recovery — pinned-format validity, re-armed warning, voice reset — when the graph is live.
	if (mpAudioEngine->IsAudioDevicePresent())
	{
		mFlags.Set(AudioManagerFlags::kPinnedFormatValid);
		mFlags.Clear(AudioManagerFlags::kSilentRecoveryLogged);
		LOG(kAudio, kInfo, "Audio device recovered; live graph established");
		FinishDeviceReset();
	}
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
		if (mFlags & AudioManagerFlags::kPinnedFormatValid)
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

			FinishDeviceReset();
		}
		else
		{
			// Silent-start engine: mPinnedOutputFormat was never validated against a live graph (kPinnedFormatValid
			// false — a silent start, or a startup pin whose Resets both failed), so the pinned-first path above
			// cannot run. Throttle the probe so a permanently-deviceless machine does not
			// warn + full-reset every frame (a stable silent engine makes this branch true every frame).
			if (++miSilentRecoveryFrameCounter >= kiSilentRecoveryRetryFrames)
			{
				miSilentRecoveryFrameCounter = 0;
				AttemptSilentEngineRecovery();
			}
		}
	}

	if (mpAudioEngine == nullptr || !mpAudioEngine->IsAudioDevicePresent()) [[unlikely]]
	{
		return;
	}

	float fDeltaTime = common::NanosecondsToFloatSeconds<float>(mRealTime.GetDeltaNs(true));

	mpStreamingVoices->CheckTrackTransition();
	mpStreamingVoices->Update(fDeltaTime);

	if (pFrame != nullptr)
	{
		// Listener position must update first — UpdateLifecycle's priority/cull pass
		// reads mVecListenerPosition and mfEffectiveFadeEnd computed here.
		mpStaticVoices->UpdateListenerPosition();
		mpStaticVoices->UpdateLifecycle(*pFrame, fDeltaTime);
	}

	mpStaticVoices->UpdateVolumes();

	gpProfileManager->SetCount(kCpuCounterSounds, mpStaticVoices->GetVoiceCount());
	gpProfileManager->SetCount(kCpuCounterStreams, mpStreamingVoices->GetStreamCount());

	mpAudioEngine->Update();
}

void AudioManager::OnCriticalError()
{
	// Silent-recovery's pin/fallback Reset synchronously fires this migration notification (SetSilentMode) on
	// the main thread while the graph is intentionally being torn down and rebuilt. It is expected there, not a
	// real critical error, so skip the false error log and the redundant voice clear (recovery clears voices via
	// FinishDeviceReset on success).
	if (mFlags & AudioManagerFlags::kExpectedResetInProgress)
	{
		return;
	}

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
