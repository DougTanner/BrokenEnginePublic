#include "StreamingVoice.h"

#if defined(BT_CLIENT)

#include "Ui/SoundSettingsWrappersBase.h"

namespace engine
{

using enum StreamingVoiceFlags;

StreamingVoice::StreamingVoice(IXAudio2SourceVoice* pVoice, const LazyChunk* pLazyChunk)
: mpLazyChunk(pLazyChunk)
, mpVoice(pVoice)
{
	LOG(kAudio, kDebug, "Music streaming: Initializing stream for CRC {:#018x}, data size: {} bytes, buffer size: {} bytes", mpLazyChunk->location.crc, mpLazyChunk->header.iSize, kiBufferSize);

	CHECK_HRESULT(mpVoice->SetVolume(0.0f));

	// Pre-queue all buffers: main-thread refill latency exceeds single-buffer playback time, so a queue depth of 1 underruns.
	for (int64_t i = 0; i < kiBufferCount; ++i)
	{
		bool bLastBuffer = false;
		int64_t iBytesRead = 0;
		if (FillBuffer(mBuffers[i], iBytesRead, bLastBuffer))
		{
			XAUDIO2_BUFFER xaudio2Buffer
			{
				.Flags = bLastBuffer ? XAUDIO2_END_OF_STREAM : 0u,
				.AudioBytes = static_cast<UINT32>(iBytesRead),
				.pAudioData = mBuffers[i],
				.PlayBegin = 0,
				.PlayLength = 0,
				.LoopBegin = 0,
				.LoopLength = 0,
				.LoopCount = 0,
				.pContext = this,
			};
			CHECK_HRESULT(mpVoice->SubmitSourceBuffer(&xaudio2Buffer));
			miActiveBuffer = i;
			mFlags.Set(kLastBufferSubmitted, bLastBuffer);

			LOG(kAudio, kDebug, "Music streaming: Submitted initial buffer [{}] with {} bytes, last buffer: {}", i, iBytesRead, bLastBuffer);

			if (bLastBuffer)
			{
				break;
			}
		}
		else
		{
			mFlags.Set(kLastBufferSubmitted);
			break;
		}
	}

	CHECK_HRESULT(mpVoice->Start());
}

StreamingVoice::~StreamingVoice()
{
	DestroyXAudio2SourceVoice(mpVoice);
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

bool StreamingVoice::FillBuffer(uint8_t (&rBuffer)[kiBufferSize], int64_t& riBytesRead, bool& rbLastBuffer)
{
	rbLastBuffer = false;
	riBytesRead = 0;

	// Calculate data remaining
	int64_t iRemainingData = mpLazyChunk->header.iSize - miCurrentPosition;
	if (iRemainingData == 0)
	{
		rbLastBuffer = true;
		LOG(kAudio, kDebug, "Music streaming: No remaining data to read, position: {}/{}", miCurrentPosition, mpLazyChunk->header.iSize);
		return false;
	}

	// Calculate how much to read, ensuring we don't exceed buffer size or remaining data
	int64_t iBytesToRead = std::min(iRemainingData, kiBufferSize);

	// Read the data from the chunk at the current position
	bool bSuccess = gpFileManager->ReadChunkData(mpLazyChunk->location.crc, miCurrentPosition, std::span<std::byte>(reinterpret_cast<std::byte*>(rBuffer), iBytesToRead));

	if (!bSuccess)
	{
		LOG(kAudio, kWarning, "Music streaming: Failed to read chunk data at position {}", miCurrentPosition);
		return false;
	}

	// Update the current position and return bytes read
	miCurrentPosition += iBytesToRead;
	riBytesRead = iBytesToRead;

	// Check if this is the last buffer
	if (miCurrentPosition >= mpLazyChunk->header.iSize)
	{
		rbLastBuffer = true;
		LOG(kAudio, kDebug, "Music streaming last buffer: Read {} bytes at position {}/{}", iBytesToRead, miCurrentPosition, mpLazyChunk->header.iSize);
	}

	return true;
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
