#include "StreamingVoice.h"

#include "AudioManager.h"
#include "File/FileManager.h"
#include "Ui/Wrapper.h"

namespace engine
{

using enum StreamingVoiceFlags;

StreamingVoice::StreamingVoice(IXAudio2SourceVoice* pVoice, const LazyChunk* pLazyChunk)
: mpLazyChunk(pLazyChunk)
, mpVoice(pVoice)
{
	// Calculate buffer size rounded up to block alignment
	int64_t iBufferSize = kiBufferSize;
	if (mpLazyChunk->header.audioHeader.waveFormat.nBlockAlign > 0)
	{
		iBufferSize = common::RoundUp<int64_t>(kiBufferSize, mpLazyChunk->header.audioHeader.waveFormat.nBlockAlign);
	}

	LOG_STREAMING_VOICES("Music streaming: Initializing stream for CRC {:#018x}, data size: {} bytes, block align: {} bytes, buffer size: {} bytes", mpLazyChunk->location.crc, mpLazyChunk->header.iSize, mpLazyChunk->header.audioHeader.waveFormat.nBlockAlign, iBufferSize);

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
	if (mpVoice != nullptr)
	{
		mpVoice->Stop(0, XAUDIO2_COMMIT_NOW);
		mpVoice->FlushSourceBuffers();
		if (gpAudioManager->mpAudioEngine != nullptr)
		{
			gpAudioManager->mpAudioEngine->DestroyVoice(mpVoice);
		}
	}
}

StreamingVoice::StreamingVoice(StreamingVoice&& rToMove) noexcept
{
	*this = std::move(rToMove);
}

StreamingVoice& StreamingVoice::operator=(StreamingVoice&& rToMove) noexcept
{
	if (this != &rToMove)
	{
		mFlags = rToMove.mFlags;
		mpLazyChunk = rToMove.mpLazyChunk;
		miCurrentPosition = rToMove.miCurrentPosition;
		miActiveBuffer = rToMove.miActiveBuffer;
		mBuffers = std::move(rToMove.mBuffers);
		mfCurrentVolume = rToMove.mfCurrentVolume;

		if (gpAudioManager->mpAudioEngine != nullptr)
		{
			gpAudioManager->mpAudioEngine->DestroyVoice(mpVoice);
		}
		mpVoice = rToMove.mpVoice;
		rToMove.mpVoice = nullptr;
	}

	return *this;
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

bool StreamingVoice::FillBuffer(std::vector<uint8_t>& buffer, int64_t& riBytesRead, bool& rbLastBuffer)
{
	rbLastBuffer = false;
	riBytesRead = 0;

	// Calculate data remaining
	int64_t iRemainingData = mpLazyChunk->header.iSize - miCurrentPosition;
	if (iRemainingData == 0)
	{
		rbLastBuffer = true;
		LOG_STREAMING_VOICES("Music streaming: No remaining data to read, position: {}/{}", miCurrentPosition, mpLazyChunk->header.iSize);
		return false;
	}

	// Calculate how much to read, ensuring we don't exceed buffer size or remaining data
	int64_t iBytesToRead = std::min(iRemainingData, static_cast<int64_t>(buffer.size()));

	// Ensure read size is aligned to ADPCM block boundaries
	if (mpLazyChunk->header.audioHeader.waveFormat.nBlockAlign > 0)
	{
		iBytesToRead = common::RoundDown<int64_t>(iBytesToRead, mpLazyChunk->header.audioHeader.waveFormat.nBlockAlign);
	}

	// If no aligned data to read, we're at the end
	if (iBytesToRead == 0)
	{
		rbLastBuffer = true;
		LOG_STREAMING_VOICES("Music streaming: No aligned data to read, block align: {}, remaining: {}", mpLazyChunk->header.audioHeader.waveFormat.nBlockAlign, iRemainingData);
		return false;
	}

	// Read the data from the chunk at the current position
	bool bSuccess = gpFileManager->ReadChunkData(mpLazyChunk->location.crc, miCurrentPosition, std::span<byte>(reinterpret_cast<byte*>(buffer.data()), iBytesToRead));

	if (!bSuccess)
	{
		LOG_STREAMING_VOICES("Music streaming: Failed to read chunk data at position {}", miCurrentPosition);
		return false;
	}

	// Update the current position and return bytes read
	miCurrentPosition += iBytesToRead;
	riBytesRead = iBytesToRead;

	// Check if this is the last buffer
	if (miCurrentPosition >= mpLazyChunk->header.iSize)
	{
		rbLastBuffer = true;
		LOG_STREAMING_VOICES("Music streaming last buffer: Read {} bytes at position {}/{}", iBytesToRead, miCurrentPosition, mpLazyChunk->header.iSize);
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
