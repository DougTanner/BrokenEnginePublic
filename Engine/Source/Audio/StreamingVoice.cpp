#include "StreamingVoice.h"

#if defined(BT_CLIENT)

#include "AudioUtility.h"
#include "File/FileManager.h"
#include "Ui/SoundSettingsWrappersBase.h"

namespace engine
{

using enum StreamingVoiceFlags;

StreamingVoice::StreamingVoice(AudioEngine* pAudioEngine, IXAudio2SourceVoice* pVoice, const LazyChunk* pLazyChunk)
: mpLazyChunk(pLazyChunk)
, mpVoice(pVoice)
, mpAudioEngine(pAudioEngine)
{
	LOG(kAudio, kDebug, "Music streaming: Initializing stream for CRC {}, data size: {} bytes, buffer size: {} bytes", mpLazyChunk->location.crc, mpLazyChunk->header.iSize, kiBufferSize);

	CHECK_HRESULT(mpVoice->SetVolume(0.0f));
}

StreamingVoice::~StreamingVoice()
{
	DestroyXAudio2SourceVoice(mpAudioEngine, mpVoice);
}

float StreamingVoice::GetRemainingTime() const
{
	int64_t iRemainingBytes = mpLazyChunk->header.iSize - miCurrentPosition;
	if (iRemainingBytes <= 0)
	{
		return 0.0f;
	}

	return static_cast<float>(iRemainingBytes) / static_cast<float>(mpLazyChunk->header.audioHeader.waveFormat.nAvgBytesPerSec);
}

bool StreamingVoice::ShouldTransition() const
{
	return GetRemainingTime() <= kfCrossfadeDuration || miCurrentPosition >= mpLazyChunk->header.iSize || (mFlags & kLastBufferSubmitted);
}

void StreamingVoice::FillSlot(int64_t iSlot)
{
	mSlotBytesRead[iSlot] = 0;
	mbSlotLastBuffer[iSlot] = false;

	int64_t iRemainingData = mpLazyChunk->header.iSize - miCurrentPosition;
	if (iRemainingData == 0)
	{
		mbSlotLastBuffer[iSlot] = true;
		mbFillDone.store(true, std::memory_order_release);
		LOG(kAudio, kDebug, "Music streaming: No remaining data to read, position: {}/{}", miCurrentPosition, mpLazyChunk->header.iSize);
		return;
	}

	int64_t iBytesToRead = std::min(iRemainingData, kiBufferSize);

	bool bSuccess = gpFileManager->ReadChunkData(mpLazyChunk->location.crc, miCurrentPosition, std::span<std::byte>(reinterpret_cast<std::byte*>(mBuffers[iSlot]), iBytesToRead));
	if (!bSuccess)
	{
		mbFillDone.store(true, std::memory_order_release);
		LOG(kAudio, kWarning, "Music streaming: Failed to read chunk data at position {}", miCurrentPosition);
		return;
	}

	miCurrentPosition += iBytesToRead;
	mSlotBytesRead[iSlot] = iBytesToRead;

	if (miCurrentPosition >= mpLazyChunk->header.iSize)
	{
		mbSlotLastBuffer[iSlot] = true;
		LOG(kAudio, kDebug, "Music streaming last buffer: Read {} bytes at position {}/{}", iBytesToRead, miCurrentPosition, mpLazyChunk->header.iSize);
	}
}

void StreamingVoice::FillReadyBuffers()
{
	if (mbFillDone.load(std::memory_order_acquire))
	{
		return;
	}
	for (int64_t iSlot = 0; iSlot < kiBufferCount; ++iSlot)
	{
		if (mSlotStates[iSlot].load(std::memory_order_acquire) != static_cast<uint8_t>(SlotState::kEmpty))
		{
			continue;
		}
		mSlotStates[iSlot].store(static_cast<uint8_t>(SlotState::kFilling), std::memory_order_relaxed);
		FillSlot(iSlot);
		mSlotStates[iSlot].store(static_cast<uint8_t>(SlotState::kReady), std::memory_order_release);
		if (mbFillDone.load(std::memory_order_acquire))
		{
			return;
		}
	}
}

void StreamingVoice::DrainConsumedAndSubmitReady()
{
	int64_t iConsumed = miBuffersConsumed.exchange(0, std::memory_order_acquire);
	for (int64_t i = 0; i < iConsumed; ++i)
	{
		mSlotStates[miNextConsume].store(static_cast<uint8_t>(SlotState::kEmpty), std::memory_order_release);
		miNextConsume = (miNextConsume + 1) % kiBufferCount;
	}

	while (!(mFlags & kLastBufferSubmitted))
	{
		uint8_t uiState = mSlotStates[miNextSubmit].load(std::memory_order_acquire);
		if (uiState != static_cast<uint8_t>(SlotState::kReady))
		{
			break;
		}

		int64_t iBytesRead = mSlotBytesRead[miNextSubmit];
		bool bLastBuffer = mbSlotLastBuffer[miNextSubmit];

		if (iBytesRead == 0)
		{
			// Worker hit EOF or read failure with nothing to submit; mark done and recycle the slot.
			mFlags.Set(kLastBufferSubmitted);
			mSlotStates[miNextSubmit].store(static_cast<uint8_t>(SlotState::kEmpty), std::memory_order_release);
			miNextSubmit = (miNextSubmit + 1) % kiBufferCount;
			break;
		}

		XAUDIO2_BUFFER xaudio2Buffer
		{
			.Flags = bLastBuffer ? XAUDIO2_END_OF_STREAM : 0u,
			.AudioBytes = static_cast<UINT32>(iBytesRead),
			.pAudioData = mBuffers[miNextSubmit],
			.PlayBegin = 0,
			.PlayLength = 0,
			.LoopBegin = 0,
			.LoopLength = 0,
			.LoopCount = 0,
			.pContext = this,
		};
		HRESULT hResult = mpVoice->SubmitSourceBuffer(&xaudio2Buffer);
		if (FAILED(hResult))
		{
			char pcHex[20] {};
			LOG(kAudio, kWarning, "SubmitSourceBuffer failed, HRESULT: {}", common::ToHex(std::span(pcHex), static_cast<uint32_t>(hResult)));
			mFlags.Set(kLastBufferSubmitted);
			break;
		}

		mSlotStates[miNextSubmit].store(static_cast<uint8_t>(SlotState::kSubmitted), std::memory_order_release);
		miNextSubmit = (miNextSubmit + 1) % kiBufferCount;

		if (!mbStarted)
		{
			CHECK_HRESULT(mpVoice->Start());
			mbStarted = true;
		}

		if (bLastBuffer)
		{
			mFlags.Set(kLastBufferSubmitted);
			break;
		}
	}
}

void StreamingVoice::BeginFadeOut()
{
	mFlags.Clear(kFadingIn);
	mFlags.Set(kFadingOut);
}

void StreamingVoice::DetachXAudio2Voice()
{
	mpVoice = nullptr;
}

bool StreamingVoice::UpdateVolume(float fDeltaTime)
{
	if (mFlags & kFadingIn)
	{
		mfCurrentVolume += fDeltaTime / kfCrossfadeDuration;
		if (mfCurrentVolume >= 1.0f)
		{
			mFlags.Clear(kFadingIn);
		}
	}
	else if (mFlags & kFadingOut)
	{
		mfCurrentVolume -= fDeltaTime / kfCrossfadeDuration;
		if (mfCurrentVolume <= 0.0f)
		{
			mFlags.Clear(kFadingOut);
		}
	}
	mfCurrentVolume = std::clamp(mfCurrentVolume, 0.0f, 1.0f);

	CHECK_HRESULT(mpVoice->SetVolume(VolumeToPower(gMasterVolume.Get(), gMusicVolume.Get(), mfCurrentVolume)));

	return mfCurrentVolume <= 0.0f;
}

void StreamingVoice::OnBufferEnd()
{
	miBuffersConsumed.fetch_add(1, std::memory_order_release);
}

} // namespace engine

#endif // BT_CLIENT
