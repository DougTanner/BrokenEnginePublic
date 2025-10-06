#pragma once

namespace engine
{

enum class VoiceFlags : uint8_t
{
	kFadingOut = 0x01,
};
using VoiceFlags_t = common::Flags<VoiceFlags>;

// Unified voice class for both sound effects and music streaming
// Mode distinction: Sound effects have empty buffers vector, music streams have 3 buffers
class Voice
{
public:

	Voice() = default;
	~Voice();

	// Move-only (contains unique_ptr)
	Voice(Voice&&) = default;
	Voice& operator=(Voice&&) = default;
	Voice(const Voice&) = delete;
	Voice& operator=(const Voice&) = delete;

	// Get remaining playback time for music streams (in seconds)
	float GetRemainingTime() const;

	// Fill a streaming buffer with audio data from the chunk, ensuring block alignment
	bool FillBuffer(uint8_t* pBuffer, int64_t iBufferSize, int64_t& riBytesRead, bool& rbLastBuffer);

	// Process streaming buffer completion, filling and submitting the next buffer
	void ProcessNextBuffer(class AudioManager* pAudioManager);

	// Initialize music stream with chunk data and allocate streaming buffers
	bool InitializeMusicStream(common::crc_t audioCrc, class AudioManager* pAudioManager);

	// Sound effect members
	VoiceFlags_t mFlags;
	int64_t miId = 0;
	int64_t miFrameId = 0;
	float mfVolume = 0.0f;
	float mfPitch = 1.0f;
	float mfFadeOutVolume = 0.0f;
	float mfFadeOutTime = 0.0f;
	XMVECTOR mvecPosition {};
	XMVECTOR mvecVelocity {};

	// Music streaming members
	common::ChunkLocation mchunkLocation {};          // File offset and size info
	int64_t miCurrentPosition = 0;                    // Current read position in audio data
	int64_t miDataChunkSize = 0;                      // Total audio data size
	int64_t miBlockAlign = 0;                         // ADPCM block alignment size
	int64_t miBufferSize = 0;                         // Size of each streaming buffer
	int64_t miActiveBuffer = 0;                       // Currently playing buffer index
	bool mbStreamActive = false;                      // Whether streaming is active
	bool mbLastBufferSubmitted = false;               // Whether the last buffer has been submitted
	std::vector<std::unique_ptr<uint8_t[]>> mbuffers; // Streaming buffer pool (empty for sound effects, 3 buffers for music)

	// Shared XAudio2 voice pointer (used by both sound effects and music streams)
	IXAudio2SourceVoice* mpVoice = nullptr;
};

} // namespace engine
