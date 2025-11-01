#include "StreamingVoice.h"

#include "AudioManager.h"
#include "File/FileManager.h"
#include "Ui/Wrapper.h"

namespace engine
{

using enum StreamingVoiceFlags;

StreamingVoice::StreamingVoice(IXAudio2SourceVoice* pVoice, const LazyChunk& rLazyChunk)
: mrLazyChunk(rLazyChunk)
, mpVoice(pVoice)
{
#if 1 // DT: TEMP
	int64_t iBytesFor8Seconds = 8 * mrLazyChunk.header.audioHeader.waveFormat.nAvgBytesPerSec;
	iBytesFor8Seconds = common::RoundDown<int64_t>(iBytesFor8Seconds, mrLazyChunk.header.audioHeader.waveFormat.nBlockAlign);
	if (iBytesFor8Seconds >= mrLazyChunk.header.iSize)
	{
		miCurrentPosition = 0;
		LOG_STREAMING_VOICES("Music streaming: Track shorter than 8 seconds, starting from beginning");
	}
	else
	{
		miCurrentPosition = mrLazyChunk.header.iSize - iBytesFor8Seconds;
		miCurrentPosition = common::RoundDown<int64_t>(miCurrentPosition, mrLazyChunk.header.audioHeader.waveFormat.nBlockAlign);
		LOG_STREAMING_VOICES("Music streaming: Starting playback at position {} (8 seconds from end of {} total bytes)", miCurrentPosition, mrLazyChunk.header.iSize);
	}
#endif

	// Calculate buffer size rounded up to block alignment
	int64_t iBufferSize = kiBufferSize;
	if (mrLazyChunk.header.audioHeader.waveFormat.nBlockAlign > 0)
	{
		iBufferSize = common::RoundUp<int64_t>(kiBufferSize, mrLazyChunk.header.audioHeader.waveFormat.nBlockAlign);
	}

	LOG_STREAMING_VOICES("Music streaming: Initializing stream for CRC {:#018x}, data size: {} bytes, block align: {} bytes, buffer size: {} bytes", mrLazyChunk.location.crc, mrLazyChunk.header.iSize, mrLazyChunk.header.audioHeader.waveFormat.nBlockAlign, iBufferSize);

	// Allocate streaming buffers
	mBuffers.resize(kiBufferCount);
	for (auto& buffer : mBuffers)
	{
		buffer.resize(iBufferSize);
	}

	CHECK_HRESULT(mpVoice->SetVolume(0.0f));

	// Fill and submit the first buffer
	bool bLastBuffer = false;
	int64_t iBytesRead = 0;
	if (FillBuffer(mBuffers[miActiveBuffer], iBytesRead, bLastBuffer))
	{
		// Submit the first buffer
		XAUDIO2_BUFFER xaudio2Buffer
		{
			.Flags = bLastBuffer ? XAUDIO2_END_OF_STREAM : 0u,
			.AudioBytes = static_cast<UINT32>(iBytesRead),
			.pAudioData = mBuffers[miActiveBuffer].data(),
			.PlayBegin = 0,
			.PlayLength = 0,
			.LoopBegin = 0,
			.LoopLength = 0,
			.LoopCount = 0,
			.pContext = this,
		};
		CHECK_HRESULT(mpVoice->SubmitSourceBuffer(&xaudio2Buffer));
		mFlags.Set(kLastBufferSubmitted, bLastBuffer);

		LOG_STREAMING_VOICES("Music streaming: Submitted initial buffer [0] with {} bytes, last buffer: {}", iBytesRead, bLastBuffer);
	}
	else
	{
		mFlags |= kLastBufferSubmitted;
	}

	CHECK_HRESULT(mpVoice->Start());
}

StreamingVoice::~StreamingVoice()
{
	gpAudioManager->mpAudioEngine->DestroyVoice(mpVoice);
}

StreamingVoice::StreamingVoice(StreamingVoice&& rToMove) noexcept
: mFlags(rToMove.mFlags)
, mrLazyChunk(rToMove.mrLazyChunk)
, miCurrentPosition(rToMove.miCurrentPosition)
, miActiveBuffer(rToMove.miActiveBuffer)
, mBuffers(std::move(rToMove.mBuffers))
, mfCurrentVolume(rToMove.mfCurrentVolume)
, mpVoice(rToMove.mpVoice)
{
	rToMove.mpVoice = nullptr;
}

StreamingVoice& StreamingVoice::operator=(StreamingVoice&& rToMove) noexcept
{
	if (this != &rToMove)
	{
		mFlags = rToMove.mFlags;
		miCurrentPosition = rToMove.miCurrentPosition;
		miActiveBuffer = rToMove.miActiveBuffer;
		mBuffers = std::move(rToMove.mBuffers);
		mfCurrentVolume = rToMove.mfCurrentVolume;

		gpAudioManager->mpAudioEngine->DestroyVoice(mpVoice);
		mpVoice = rToMove.mpVoice;
		rToMove.mpVoice = nullptr;
	}

	return *this;
}

float StreamingVoice::GetRemainingTime() const
{
	int64_t iRemainingBytes = mrLazyChunk.header.iSize - miCurrentPosition;
	if (iRemainingBytes <= 0)
	{
		return 0.0f;
	}

	return static_cast<float>(iRemainingBytes) / static_cast<float>(mrLazyChunk.header.audioHeader.waveFormat.nAvgBytesPerSec);
}

bool StreamingVoice::FillBuffer(std::vector<uint8_t>& buffer, int64_t& riBytesRead, bool& rbLastBuffer)
{
	rbLastBuffer = false;
	riBytesRead = 0;

	// Calculate data remaining
	int64_t iRemainingData = mrLazyChunk.header.iSize - miCurrentPosition;
	if (iRemainingData == 0)
	{
		rbLastBuffer = true;
		LOG_STREAMING_VOICES("Music streaming: No remaining data to read, position: {}/{}", miCurrentPosition, mrLazyChunk.header.iSize);
		return false;
	}

	// Calculate how much to read, ensuring we don't exceed buffer size or remaining data
	int64_t iBytesToRead = std::min(iRemainingData, static_cast<int64_t>(buffer.size()));

	// Ensure read size is aligned to ADPCM block boundaries
	if (mrLazyChunk.header.audioHeader.waveFormat.nBlockAlign > 0)
	{
		iBytesToRead = common::RoundDown<int64_t>(iBytesToRead, mrLazyChunk.header.audioHeader.waveFormat.nBlockAlign);
	}

	// If no aligned data to read, we're at the end
	if (iBytesToRead == 0)
	{
		rbLastBuffer = true;
		LOG_STREAMING_VOICES("Music streaming: No aligned data to read, block align: {}, remaining: {}", mrLazyChunk.header.audioHeader.waveFormat.nBlockAlign, iRemainingData);
		return false;
	}

	// Read the data from the chunk at the current position
	bool bSuccess = gpFileManager->ReadChunkData(mrLazyChunk.location.crc, miCurrentPosition, std::span<byte>(reinterpret_cast<byte*>(buffer.data()), iBytesToRead));

	if (!bSuccess)
	{
		LOG_STREAMING_VOICES("Music streaming: Failed to read chunk data at position {}", miCurrentPosition);
		return false;
	}

	// Update the current position and return bytes read
	miCurrentPosition += iBytesToRead;
	riBytesRead = iBytesToRead;

	// Check if this is the last buffer
	if (miCurrentPosition >= mrLazyChunk.header.iSize)
	{
		rbLastBuffer = true;
		LOG_STREAMING_VOICES("Music streaming last buffer: Read {} bytes at position {}/{}", iBytesToRead, miCurrentPosition, mrLazyChunk.header.iSize);
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
			mFlags &= kFadingIn;
		}
	}
	else if (mFlags & kFadingOut)
	{
		mfCurrentVolume -= fDeltaTime / kfCrossfadeDuration;
		if (mfCurrentVolume <= 0.0f)
		{
			mFlags &= kFadingOut;
		}
	}
	mfCurrentVolume = std::clamp(mfCurrentVolume, 0.0f, 1.0f);

	CHECK_HRESULT(mpVoice->SetVolume(VolumeToPower(gMasterVolume.Get(), gMusicVolume.Get(), mfCurrentVolume)));

	return mfCurrentVolume <= 0.0f;
}

void StreamingVoice::OnBufferEnd()
{
	std::lock_guard<std::recursive_mutex> lock(gpAudioManager->mMusicStreamRecursiveMutex);

	if (mFlags & kLastBufferSubmitted)
	{
		return;
	}

	int iNextBuffer = (miActiveBuffer + 1) % static_cast<int>(mBuffers.size());

	bool bLastBuffer = false;
	int64_t iBytesRead = 0;
	if (FillBuffer(mBuffers[iNextBuffer], iBytesRead, bLastBuffer))
	{
		XAUDIO2_BUFFER xaudio2Buffer
		{
			.Flags = bLastBuffer ? XAUDIO2_END_OF_STREAM : 0u,
			.AudioBytes = static_cast<UINT32>(iBytesRead),
			.pAudioData = mBuffers[iNextBuffer].data(),
			.PlayBegin = 0,
			.PlayLength = 0,
			.LoopBegin = 0,
			.LoopLength = 0,
			.LoopCount = 0,
			.pContext = this,
		};
		HRESULT hr = mpVoice->SubmitSourceBuffer(&xaudio2Buffer);
		if (FAILED(hr))
		{
			LOG_STREAMING_VOICES("ProcessNextBuffer: ERROR - Failed to submit buffer for stream, HRESULT: 0x{:08X}", hr);
			mFlags |= kLastBufferSubmitted;
			return;
		}
		miActiveBuffer = iNextBuffer;
		mFlags.Set(kLastBufferSubmitted, bLastBuffer);
	}
	else
	{
		LOG_STREAMING_VOICES("ProcessNextBuffer: stream reached end, marking as inactive");
		mFlags |= kLastBufferSubmitted;
	}
}

} // namespace engine
