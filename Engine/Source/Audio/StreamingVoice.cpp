#include "StreamingVoice.h"

#include "AudioManager.h"
#include "File/FileManager.h"

namespace engine
{

using enum StreamingVoiceFlags;

StreamingVoice::StreamingVoice(common::crc_t audioCrc)
: mrLazyChunk(gpFileManager->GetLazyChunkMap().at(audioCrc))
{
#if 1 // DT: TEMP
	int64_t iBytesFor8Seconds = 8 * mrLazyChunk.header.audioHeader.waveFormat.nAvgBytesPerSec;
	iBytesFor8Seconds = (iBytesFor8Seconds / mrLazyChunk.header.audioHeader.waveFormat.nBlockAlign) * mrLazyChunk.header.audioHeader.waveFormat.nBlockAlign;
	if (iBytesFor8Seconds >= mrLazyChunk.header.iSize)
	{
		miCurrentPosition = 0;
		LOG_STREAMING_VOICES("Music streaming: Track shorter than 8 seconds, starting from beginning");
	}
	else
	{
		miCurrentPosition = mrLazyChunk.header.iSize - iBytesFor8Seconds;
		miCurrentPosition = (miCurrentPosition / mrLazyChunk.header.audioHeader.waveFormat.nBlockAlign) * mrLazyChunk.header.audioHeader.waveFormat.nBlockAlign;
		LOG_STREAMING_VOICES("Music streaming: Starting playback at position {} (8 seconds from end of {} total bytes)", miCurrentPosition, mrLazyChunk.header.iSize);
	}
#endif

	LOG_STREAMING_VOICES("Music streaming: Initializing stream for CRC {:#018x}, data size: {} bytes, block align: {} bytes, buffer size: {} bytes", audioCrc, mrLazyChunk.header.iSize, mrLazyChunk.header.audioHeader.waveFormat.nBlockAlign, miBufferSize);

	// Allocate streaming buffers, rounded up to block alignment
	if (mrLazyChunk.header.audioHeader.waveFormat.nBlockAlign > 0)
	{
		miBufferSize = (kiBufferSize / mrLazyChunk.header.audioHeader.waveFormat.nBlockAlign) * mrLazyChunk.header.audioHeader.waveFormat.nBlockAlign;
	}

	mBuffers.resize(kiBufferCount);
	for (auto& pBuffer : mBuffers)
	{
		pBuffer = std::make_unique<uint8_t[]>(miBufferSize);
	}

	gpAudioManager->mpAudioEngine->AllocateVoice(&mrLazyChunk.header.audioHeader.waveFormat, SoundEffectInstance_Default, false, &mpVoice);
	if (mpVoice == nullptr)
	{
		return;
	}

	CHECK_HRESULT(mpVoice->SetVolume(0.0f));

	// Fill and submit the first buffer
	bool bLastBuffer = false;
	int64_t iBytesRead = 0;
	if (FillBuffer(mBuffers[0].get(), miBufferSize, iBytesRead, bLastBuffer))
	{
		// Submit the first buffer
		XAUDIO2_BUFFER xaudio2Buffer
		{
			.Flags = bLastBuffer ? XAUDIO2_END_OF_STREAM : 0u,
			.AudioBytes = static_cast<UINT32>(iBytesRead),
			.pAudioData = mBuffers[0].get(),
			.PlayBegin = 0,
			.PlayLength = 0,
			.LoopBegin = 0,
			.LoopLength = 0,
			.LoopCount = 0,
			.pContext = this,
		};
		CHECK_HRESULT(mpVoice->SubmitSourceBuffer(&xaudio2Buffer));
		miActiveBuffer = 0;
		mFlags |= kStreamActive;
		mFlags.Set(kLastBufferSubmitted, bLastBuffer);

		LOG_STREAMING_VOICES("Music streaming: Submitted initial buffer [0] with {} bytes, last buffer: {}", iBytesRead, bLastBuffer);
	}

	CHECK_HRESULT(mpVoice->Start());
}

StreamingVoice::~StreamingVoice()
{
	if (gpAudioManager->mpAudioEngine == nullptr || mpVoice == nullptr)
	{
		return;
	}

	mpVoice->Stop();
	mpVoice->FlushSourceBuffers();
	gpAudioManager->mpAudioEngine->DestroyVoice(mpVoice);
	mpVoice = nullptr;
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

bool StreamingVoice::FillBuffer(uint8_t* pBuffer, int64_t iBufferSize, int64_t& riBytesRead, bool& rbLastBuffer)
{
	// Fill a streaming buffer with audio data from the chunk, ensuring block alignment
	rbLastBuffer = false;
	riBytesRead = 0;

	// Calculate how much data is remaining
	int64_t iRemainingData = mrLazyChunk.header.iSize - miCurrentPosition;
	if (iRemainingData == 0)
	{
		rbLastBuffer = true;
		LOG_STREAMING_VOICES("Music streaming: No remaining data to read, position: {}/{}", miCurrentPosition, mrLazyChunk.header.iSize);
		return false;
	}

	// Calculate how much to read, ensuring we don't exceed buffer size or remaining data
	int64_t iBytesToRead = std::min(iRemainingData, iBufferSize);

	// Ensure read size is aligned to ADPCM block boundaries
	if (mrLazyChunk.header.audioHeader.waveFormat.nBlockAlign > 0)
	{
		// Round down to nearest block boundary
		iBytesToRead = (iBytesToRead / mrLazyChunk.header.audioHeader.waveFormat.nBlockAlign) * mrLazyChunk.header.audioHeader.waveFormat.nBlockAlign;

		// If we would read 0 bytes but have remaining data, read at least one block
		if (iBytesToRead == 0 && iRemainingData >= mrLazyChunk.header.audioHeader.waveFormat.nBlockAlign)
		{
			iBytesToRead = mrLazyChunk.header.audioHeader.waveFormat.nBlockAlign;
		}
	}

	// If no aligned data to read, we're at the end
	if (iBytesToRead == 0)
	{
		rbLastBuffer = true;
		LOG_STREAMING_VOICES("Music streaming: No aligned data to read, block align: {}, remaining: {}", mrLazyChunk.header.audioHeader.waveFormat.nBlockAlign, iRemainingData);
		return false;
	}

	// Read the data from the chunk at the current position
	bool bSuccess = gpFileManager->ReadChunkData(mrLazyChunk.location.crc, miCurrentPosition, pBuffer, iBytesToRead);

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

void StreamingVoice::ProcessNextBuffer()
{
	// Process a streaming buffer for this voice
	// Note: This is called from OnBufferEnd with mutex already locked

	// Verify voice is still valid
	if (mpVoice == nullptr)
	{
		LOG_STREAMING_VOICES("ProcessNextBuffer: ERROR - Voice pointer is null for stream");
		mFlags &= kStreamActive;
		return;
	}

	// Find the next buffer to fill
	int iNextBuffer = (miActiveBuffer + 1) % static_cast<int>(mBuffers.size());

	// Fill the next buffer with audio data
	bool bLastBuffer = false;
	int64_t iBytesRead = 0;
	if (FillBuffer(mBuffers[iNextBuffer].get(), miBufferSize, iBytesRead, bLastBuffer))
	{
		// Submit the filled buffer
		XAUDIO2_BUFFER xaudio2Buffer
		{
			.Flags = bLastBuffer ? XAUDIO2_END_OF_STREAM : 0u,
			.AudioBytes = static_cast<UINT32>(iBytesRead),
			.pAudioData = mBuffers[iNextBuffer].get(),
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
			LOG_STREAMING_VOICES("ProcessNextBuffer: ERROR - Failed to submit buffer for stream, HRESULT: 0x{:08X}",  hr);
			mFlags &= kStreamActive;
			return;
		}
		miActiveBuffer = iNextBuffer;
		mFlags.Set(kLastBufferSubmitted, bLastBuffer);
	}
	else if (bLastBuffer)
	{
		// Mark stream as complete
		mFlags &= kStreamActive;
		mFlags |= kLastBufferSubmitted;
		LOG_STREAMING_VOICES("ProcessNextBuffer: stream reached end, marking as inactive");
	}
}

// Apply cross-fade volume to this voice
void StreamingVoice::SetCrossFadeVolume(float fProgress, float fMasterVolume, float fMusicVolume, bool bIsCurrent)
{
	if (mpVoice == nullptr)
	{
		return;
	}

	// Calculate base music volume
	float fMusicVol = CalculateVolume(fMasterVolume, fMusicVolume);

	// Apply cross-fade curve (cosine for current, sine for next)
	float fMultiplier = bIsCurrent ? std::cos(fProgress * XM_PIDIV2) : std::sin(fProgress * XM_PIDIV2);

	CHECK_HRESULT(mpVoice->SetVolume(fMusicVol * fMultiplier));
}

// Set music volume on this voice
void StreamingVoice::SetMusicVolume(float fMasterVolume, float fMusicVolume)
{
	if (mpVoice == nullptr)
	{
		return;
	}

	float fMusicVol = CalculateVolume(fMasterVolume, fMusicVolume);
	CHECK_HRESULT(mpVoice->SetVolume(fMusicVol));
}

void StreamingVoice::OnBufferEnd()
{
	std::lock_guard<std::mutex> lock(gpAudioManager->mMusicStreamMutex);

	if (mFlags & kStreamActive && !(mFlags & kLastBufferSubmitted))
	{
		ProcessNextBuffer();
	}
}

} // namespace engine
