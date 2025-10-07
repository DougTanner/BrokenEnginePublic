#include "StreamingVoice.h"

#include "AudioManager.h"
#include "File/FileManager.h"

namespace engine
{

StreamingVoice::~StreamingVoice()
{
	if (gpAudioManager->mpAudioEngine == nullptr || mpVoice == nullptr)
	{
		return;
	}

	if (SUCCEEDED(mpVoice->Stop()))
	{
		mpVoice->FlushSourceBuffers();
	}
	gpAudioManager->mpAudioEngine->DestroyVoice(mpVoice);
	mpVoice = nullptr;
}

float StreamingVoice::GetRemainingTime() const
{
	// Calculate remaining bytes in the stream
	int64_t iRemainingBytes = miDataChunkSize - miCurrentPosition;
	if (iRemainingBytes <= 0)
	{
		return 0.0f;
	}

	const LazyChunk& rLazyChunk = gpFileManager->GetLazyChunkMap().at(mchunkLocation.crc);
	return static_cast<float>(iRemainingBytes) / static_cast<float>(rLazyChunk.header.audioHeader.waveFormat.nAvgBytesPerSec);
}

bool StreamingVoice::FillBuffer(uint8_t* pBuffer, int64_t iBufferSize, int64_t& riBytesRead, bool& rbLastBuffer)
{
	// Fill a streaming buffer with audio data from the chunk, ensuring block alignment
	rbLastBuffer = false;
	riBytesRead = 0;

	// Calculate how much data is remaining
	int64_t iRemainingData = miDataChunkSize - miCurrentPosition;
	if (iRemainingData == 0)
	{
		rbLastBuffer = true;
		LOG("Music streaming: No remaining data to read, position: {}/{}", miCurrentPosition, miDataChunkSize);
		return false;
	}

	// Calculate how much to read, ensuring we don't exceed buffer size or remaining data
	int64_t iBytesToRead = std::min(iRemainingData, iBufferSize);

	// Ensure read size is aligned to ADPCM block boundaries
	if (miBlockAlign > 0)
	{
		// Round down to nearest block boundary
		iBytesToRead = (iBytesToRead / miBlockAlign) * miBlockAlign;

		// If we would read 0 bytes but have remaining data, read at least one block
		if (iBytesToRead == 0 && iRemainingData >= miBlockAlign)
		{
			iBytesToRead = miBlockAlign;
		}
	}

	// If no aligned data to read, we're at the end
	if (iBytesToRead == 0)
	{
		rbLastBuffer = true;
		LOG("Music streaming: No aligned data to read, block align: {}, remaining: {}", miBlockAlign, iRemainingData);
		return false;
	}

	// Read the data from the chunk at the current position
	bool bSuccess = gpFileManager->ReadChunkData(mchunkLocation.crc, miCurrentPosition, pBuffer, iBytesToRead);

	if (!bSuccess)
	{
		LOG("Music streaming: Failed to read chunk data at position {}", miCurrentPosition);
		return false;
	}

	// Update the current position and return bytes read
	miCurrentPosition += iBytesToRead;
	riBytesRead = iBytesToRead;

	// Check if this is the last buffer
	if (miCurrentPosition >= miDataChunkSize)
	{
		rbLastBuffer = true;
	}

	LOG("Music streaming: Read {} bytes at position {}/{}, last buffer: {}", iBytesToRead, miCurrentPosition, miDataChunkSize, rbLastBuffer);

	return true;
}

void StreamingVoice::ProcessNextBuffer(AudioManager* pAudioManager)
{
	// Process a streaming buffer for this voice
	// Note: This is called from OnBufferEnd with mutex already locked
	LOG("ProcessNextBuffer: stream, active={}, position={}/{}", mbStreamActive, miCurrentPosition, miDataChunkSize);

	// Verify voice is still valid
	if (mpVoice == nullptr)
	{
		LOG("ProcessNextBuffer: ERROR - Voice pointer is null for stream");
		mbStreamActive = false;
		return;
	}

	// Find the next buffer to fill
	int iNextBuffer = (miActiveBuffer + 1) % static_cast<int>(mbuffers.size());
	LOG("ProcessNextBuffer:stream switching from buffer {} to {}", miActiveBuffer, iNextBuffer);

	// Fill the next buffer with audio data
	bool bLastBuffer = false;
	int64_t iBytesRead = 0;
	if (FillBuffer(mbuffers[iNextBuffer].get(), miBufferSize, iBytesRead, bLastBuffer))
	{
		// Submit the filled buffer
		XAUDIO2_BUFFER xaudio2Buffer
		{
			.Flags = bLastBuffer ? XAUDIO2_END_OF_STREAM : 0u,
			.AudioBytes = static_cast<UINT32>(iBytesRead),
			.pAudioData = mbuffers[iNextBuffer].get(),
			.PlayBegin = 0,
			.PlayLength = 0,
			.LoopBegin = 0,
			.LoopLength = 0,
			.LoopCount = 0,
			.pContext = pAudioManager,
		};
		HRESULT hr = mpVoice->SubmitSourceBuffer(&xaudio2Buffer);
		if (FAILED(hr))
		{
			LOG("ProcessNextBuffer: ERROR - Failed to submit buffer for stream, HRESULT: 0x{:08X}",  hr);
			mbStreamActive = false;
			return;
		}
		miActiveBuffer = iNextBuffer;
		mbLastBufferSubmitted = bLastBuffer;
		LOG("ProcessNextBuffer: {} stream submitted buffer with {} bytes, last={}", iNextBuffer, iBytesRead, bLastBuffer);
	}
	else if (bLastBuffer)
	{
		// Mark stream as complete
		mbStreamActive = false;
		mbLastBufferSubmitted = true;
		LOG("ProcessNextBuffer: stream reached end, marking as inactive");
	}
}

bool StreamingVoice::InitializeMusicStream(common::crc_t audioCrc, AudioManager* pAudioManager)
{
	// Initialize streaming voice with chunk info and allocate buffers
	const LazyChunk& rLazyChunk = gpFileManager->GetLazyChunkMap().at(audioCrc);
	ASSERT(rLazyChunk.data.size() == 0);

	// Get WAVEFORMATEX from AudioHeader
	const WAVEFORMATEX* pWaveFormat = &rLazyChunk.header.audioHeader.waveFormat;

	LOG("Music streaming: Initializing stream for CRC {:#018x}", audioCrc);

	// Initialize stream with chunk info
	mchunkLocation = rLazyChunk.location;
	miDataChunkSize = static_cast<uint32_t>(rLazyChunk.header.iSize);
	miBlockAlign = rLazyChunk.header.audioHeader.waveFormat.nBlockAlign;

	// Calculate starting position 8 seconds from the end
	// For ADPCM, we need to calculate based on average bytes per second
	int64_t iBytesFor8Seconds = 8 * pWaveFormat->nAvgBytesPerSec;
	// Align to block boundary
	iBytesFor8Seconds = (iBytesFor8Seconds / miBlockAlign) * miBlockAlign;

	// Ensure we don't go past the beginning of the stream
	if (iBytesFor8Seconds >= miDataChunkSize)
	{
		miCurrentPosition = 0;
		LOG("Music streaming: Track shorter than 8 seconds, starting from beginning");
	}
	else
	{
		miCurrentPosition = miDataChunkSize - iBytesFor8Seconds;
		// Ensure position is block-aligned
		miCurrentPosition = (miCurrentPosition / miBlockAlign) * miBlockAlign;
		LOG("Music streaming: Starting playback at position {} (8 seconds from end of {} total bytes)", miCurrentPosition, miDataChunkSize);
	}

	// Allocate 3 streaming buffers
	miBufferSize = kiBufferSize;

	// Round buffer size to block alignment
	if (miBlockAlign > 0)
	{
		miBufferSize = (miBufferSize / miBlockAlign) * miBlockAlign;
	}

	LOG("Music streaming: Initializing stream for CRC {:#018x}, data size: {} bytes, block align: {} bytes, buffer size: {} bytes", audioCrc, miDataChunkSize, miBlockAlign, miBufferSize);

	mbuffers.resize(3);
	for (auto& pBuffer : mbuffers)
	{
		pBuffer = std::make_unique<uint8_t[]>(miBufferSize);
	}

	// Fill and submit the first buffer
	bool bLastBuffer = false;
	int64_t iBytesRead = 0;
	if (FillBuffer(mbuffers[0].get(), miBufferSize, iBytesRead, bLastBuffer))
	{
		// Submit the first buffer
		XAUDIO2_BUFFER xaudio2Buffer
		{
			.Flags = bLastBuffer ? XAUDIO2_END_OF_STREAM : 0u,
			.AudioBytes = static_cast<UINT32>(iBytesRead),
			.pAudioData = mbuffers[0].get(),
			.PlayBegin = 0,
			.PlayLength = 0,
			.LoopBegin = 0,
			.LoopLength = 0,
			.LoopCount = 0,
			.pContext = pAudioManager,
		};
		CHECK_HRESULT(mpVoice->SubmitSourceBuffer(&xaudio2Buffer));
		miActiveBuffer = 0;
		mbStreamActive = true;
		mbLastBufferSubmitted = bLastBuffer;

		LOG("Music streaming: Submitted initial buffer [0] with {} bytes, last buffer: {}", iBytesRead, bLastBuffer);
	}

	return true;
}

// Static factory method to create and initialize a music streaming voice
std::unique_ptr<StreamingVoice> StreamingVoice::CreateMusicStream(AudioEngine* pEngine, common::crc_t audioCrc, AudioManager* pCallback)
{
	if (pEngine == nullptr || !pEngine->IsAudioDevicePresent()) [[unlikely]]
	{
		return nullptr;
	}

	// Get lazy chunk and wave format
	const LazyChunk& rLazyChunk = gpFileManager->GetLazyChunkMap().at(audioCrc);
	ASSERT(rLazyChunk.data.size() == 0);
	const WAVEFORMATEX* pWaveFormat = &rLazyChunk.header.audioHeader.waveFormat;

	LOG("Music streaming: Creating stream for CRC {:#018x}", audioCrc);

	// Create new music voice
	auto pStream = std::make_unique<StreamingVoice>();

	// Create voice for streaming
	pEngine->AllocateVoice(pWaveFormat, SoundEffectInstance_Default, false, &pStream->mpVoice);
	CHECK_HRESULT(pStream->mpVoice->SetVolume(0.0f));

	// Initialize streaming data and allocate buffers
	if (!pStream->InitializeMusicStream(audioCrc, pCallback))
	{
		return nullptr;
	}

	return pStream;
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

} // namespace engine
