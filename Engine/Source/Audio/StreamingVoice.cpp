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
	LOG(kAudio, kDebug, "Music streaming: Initializing stream for CRC {:#018x}, data size: {} bytes, buffer size: {} bytes", mpLazyChunk->location.crc, mpLazyChunk->header.iSize, kiBufferSize);

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

void StreamingVoice::FillSlot(int64_t iSlot)
{
	mSlotBytesRead[iSlot] = 0;
	mbSlotLastBuffer[iSlot] = false;

	int64_t iRemainingData = mpLazyChunk->header.iSize - miCurrentPosition;
	if (iRemainingData == 0)
	{
		mbSlotLastBuffer[iSlot] = true;
		mbFillFailed.store(true, std::memory_order_release);
		LOG(kAudio, kDebug, "Music streaming: No remaining data to read, position: {}/{}", miCurrentPosition, mpLazyChunk->header.iSize);
		return;
	}

	int64_t iBytesToRead = std::min(iRemainingData, kiBufferSize);

	bool bSuccess = gpFileManager->ReadChunkData(mpLazyChunk->location.crc, miCurrentPosition, std::span<std::byte>(reinterpret_cast<std::byte*>(mBuffers[iSlot]), iBytesToRead));
	if (!bSuccess)
	{
		mbFillFailed.store(true, std::memory_order_release);
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
