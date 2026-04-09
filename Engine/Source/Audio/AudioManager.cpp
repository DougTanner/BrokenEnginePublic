#include "AudioManager.h"

#if defined(BT_CLIENT)

#include "Memory/MemoryManager.h"

#include "Game.h"
#include "Profile/ProfileManager.h"

namespace engine
{

constexpr AUDIO_ENGINE_FLAGS kAudioEngineFlags = AudioEngine_UseMasteringLimiter;

AudioManager::AudioManager()
{
	gpAudioManager = this;

	Log(kLogAudio, "\nAudioManager");

	try
	{
		// Find the id of the default audio endpoint
		Microsoft::WRL::ComPtr<IMMDeviceEnumerator> pMMDeviceEnumerator;
		CHECK_HRESULT(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(pMMDeviceEnumerator.GetAddressOf())));
		Log(kLogAudio, "  Got MMDeviceEnumerator");

		Microsoft::WRL::ComPtr<IMMDevice> pDefaultAudioEndpoint;
		HRESULT hresult = pMMDeviceEnumerator->GetDefaultAudioEndpoint(eRender, eMultimedia, &pDefaultAudioEndpoint);
		if (hresult != S_OK)
		{
			return;
		}
		Log(kLogAudio, "  Got DefaultAudioEndpoint");

		LPWSTR pcDefaultDeviceId = nullptr;
		CHECK_HRESULT(pDefaultAudioEndpoint->GetId(&pcDefaultDeviceId));
		if (pcDefaultDeviceId == nullptr)
		{
			Log(kLogAudio, "  GetId returned nullptr");
			return;
		}
		std::wstring defaultAudioEndpointId(pcDefaultDeviceId);
		Log(kLogAudio, "    pcDeviceId: \"{}\"", defaultAudioEndpointId);
		common::ScopedLambda freeDefaultDeviceId([=]()
		{
			CoTaskMemFree(pcDefaultDeviceId);
		});

		Microsoft::WRL::ComPtr<IMMDeviceCollection> pMMDeviceCollection;
		CHECK_HRESULT(pMMDeviceEnumerator->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &pMMDeviceCollection));
		if (pMMDeviceCollection == nullptr)
		{
			Log(kLogAudio, "  EnumAudioEndpoints returned nullptr");
			return;
		}

		Log(kLogAudio, "  Searching for default audio endpoint: {}", defaultAudioEndpointId);
		UINT uiCount = 0;
		CHECK_HRESULT(pMMDeviceCollection->GetCount(&uiCount));
		Log(kLogAudio, "  uiCount: {}", uiCount);
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
			Log(kLogAudio, "    Found: {}", audioEndpointId);
			break;
		}

		if (mpAudioEngine == nullptr || !mpAudioEngine->IsAudioDevicePresent())
		{
			Log(kLogAudio, "  mpAudioEngine == nullptr || !mpAudioEngine->IsAudioDevicePresent()");

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
				Log(kLogAudio, "    Using first in the list: {}", audioEndpointId);
				mpAudioEngine = std::make_unique<AudioEngine>(kAudioEngineFlags, nullptr, audioEndpointId.c_str(), AudioCategory_GameEffects);
			}
		}

		if (mpAudioEngine != nullptr)
		{
			IXAudio2* pIXAudio2 = mpAudioEngine->GetInterface();
			XAUDIO2_DEBUG_CONFIGURATION debugConfiguration
			{
				.TraceMask = XAUDIO2_LOG_ERRORS | XAUDIO2_LOG_WARNINGS,
				.BreakMask = XAUDIO2_LOG_ERRORS,
				.LogThreadID = true,
				.LogFileline = true,
				.LogFunctionName = true,
				.LogTiming = false,
			};
			pIXAudio2->SetDebugConfiguration(&debugConfiguration);

			mpAudioEngine->RegisterNotify(this, false);

			IXAudio2MasteringVoice* pIXAudio2MasteringVoice = mpAudioEngine->GetMasterVoice();
			XAUDIO2_VOICE_DETAILS voiceDetails {};
			pIXAudio2MasteringVoice->GetVoiceDetails(&voiceDetails);
			DWORD uiChannelMask = 0;
			CHECK_HRESULT(pIXAudio2MasteringVoice->GetChannelMask(&uiChannelMask));

			char pcHex[20] {};
			Log(kLogAudio, "    Audio engine: channels {} channel mask {} rate {}", mpAudioEngine->GetOutputChannels(), common::ToHex(std::span(pcHex), mpAudioEngine->GetChannelMask()), mpAudioEngine->GetOutputSampleRate());
			Log(kLogAudio, "    Output format: channels {} channel mask {} format {}", mpAudioEngine->GetOutputFormat().Format.nChannels, common::ToHex(std::span(pcHex), mpAudioEngine->GetOutputFormat().dwChannelMask), mpAudioEngine->GetOutputFormat().Format.wFormatTag);
			Log(kLogAudio, "    MasteringVoice: channels {} channel mask {} sample rate {}", voiceDetails.InputChannels, common::ToHex(std::span(pcHex), uiChannelMask), voiceDetails.InputSampleRate);

			WAVEFORMATEXTENSIBLE waveFormatExtensible = mpAudioEngine->GetOutputFormat();
			miMasteringVoiceChannels = std::min(static_cast<int64_t>(voiceDetails.InputChannels), static_cast<int64_t>(waveFormatExtensible.Format.nChannels));

			mStaticVoices.Init(mpAudioEngine.get(), &miMasteringVoiceChannels);
			mStreamingVoices.Init(mpAudioEngine.get());
		}
	}
	catch ([[maybe_unused]] const std::exception& rException)
	{
		Log(kError, "Failed to create AudioManager: {}", rException.what());
		return;
	}
	catch (...)
	{
		Log(kError, "Failed to create AudioManager");
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

	gpAudioManager = nullptr;
}

void AudioManager::SetNextMusicTrackCallback(std::function<common::crc_t()> callback)
{
	mStreamingVoices.SetNextTrackCallback(callback);
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
	Log(kLogAudio, "Suspend: destroying {} static voices, {} streams", mStaticVoices.GetVoiceCount(), mStreamingVoices.GetStreamCount());
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
	Log(kLogAudio, "Resume");
}

void AudioManager::PlayMusic(common::crc_t uiAudioCrc)
{
	mStreamingVoices.Play(uiAudioCrc);
}

IXAudio2SourceVoice* AudioManager::PlayOneShot(const game::Frame& rFrame, common::crc_t uiAudioCrc, bool b3d, float fVolume, float fPitch, float fPitchRange)
{
	return mStaticVoices.PlayOneShot(rFrame, uiAudioCrc, b3d, fVolume, fPitch, fPitchRange);
}

void XM_CALLCONV AudioManager::PlayOneShot3d(const game::Frame& rFrame, common::crc_t uiAudioCrc, FXMVECTOR vecPosition, float fVolume, float fPitch, float fPitchRange)
{
	mStaticVoices.PlayOneShot3d(rFrame, uiAudioCrc, vecPosition, fVolume, fPitch, fPitchRange);
}

void AudioManager::Set3dSettings(float fCurveDistanceScaler, float fManualFadeStart, float fManualFadeEnd, float fManualFadeVolume)
{
	mStaticVoices.Set3dSettings(fCurveDistanceScaler, fManualFadeStart, fManualFadeEnd, fManualFadeVolume);
}

void AudioManager::Update(const game::Frame* pFrame)
{
	ScopedCpuProfile scopedCpuProfile(kCpuTimerAudio);

	// Heap: Voice map emplace/erase, make_unique<StreamingVoice> for track transitions, and
	// XAudio2 internal allocations (AllocateVoice, Update). Not controllable or pre-allocatable.
	ScopedSuppressAllocationTracking suppressAllocationTracking;

	if (mbSuspended.load(std::memory_order_acquire))
	{
		return;
	}

	if (mbClearVoicesRequested.exchange(false, std::memory_order_acquire))
	{
		mStaticVoices.Clear(true);
	}

	if (mpAudioEngine != nullptr && !mpAudioEngine->IsAudioDevicePresent()) [[unlikely]]
	{
		Log(kLogAudio, kWarning, "Music streaming: Audio device not present, resetting audio engine");

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
		mStaticVoices.UpdateLifecycle(*pFrame, fDeltaTime);
		mStaticVoices.UpdateListenerPosition(*pFrame);
	}

	mStaticVoices.UpdateVolumes();

	gpProfileManager->SetCount(kCpuCounterSounds, mStaticVoices.GetVoiceCount());
	gpProfileManager->SetCount(kCpuCounterStreams, mStreamingVoices.GetStreamCount());

	mpAudioEngine->Update();
}

void AudioManager::OnCriticalError()
{
	Log(kError, "AudioManager::OnCriticalError()");

	// Destroy voices immediately — XAudio2 callback thread is no longer running
	mStaticVoices.Clear(false);
	mStreamingVoices.Clear(false);
}

void AudioManager::OnReset()
{
	Log(kLogAudio, "AudioManager::OnReset()");
	mbClearVoicesRequested.store(true, std::memory_order_release);
	mStreamingVoices.Clear(true);
}

void AudioManager::OnDestroyEngine() noexcept
{
	Log(kLogAudio, "AudioManager::OnDestroyEngine()");
	mbClearVoicesRequested.store(true, std::memory_order_release);
	mStreamingVoices.Clear(true);
}

void AudioManager::OnTrim()
{
	Log(kLogAudio, "AudioManager::OnTrim()");
}

void AudioManager::OnDestroyParent() noexcept
{
	Log(kLogAudio, "AudioManager::OnDestroyParent()");
}

} // namespace engine

#endif // defined(BT_CLIENT)
