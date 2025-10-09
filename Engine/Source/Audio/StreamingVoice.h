#pragma once

namespace engine
{

// Buffer size for music streaming (16KB)
static constexpr int64_t kiBufferSize = 16 * 1024;

enum class StreamingVoiceFlags : uint8_t
{
	kStreamActive = 0x01,
	kLastBufferSubmitted = 0x02,
};
using StreamingVoiceFlags_t = common::Flags<StreamingVoiceFlags>;

// StreamingVoice represents a music stream with buffered playback
// Used for streaming large audio files (background music)
class StreamingVoice
{
public:

	StreamingVoice() = default;
	~StreamingVoice();

	// Move-only (contains unique_ptr)
	StreamingVoice(StreamingVoice&&) = default;
	StreamingVoice& operator=(StreamingVoice&&) = default;
	StreamingVoice(const StreamingVoice&) = delete;
	StreamingVoice& operator=(const StreamingVoice&) = delete;

	// Get remaining playback time for music streams (in seconds)
	float GetRemainingTime() const;

	// Fill a streaming buffer with audio data from the chunk, ensuring block alignment
	bool FillBuffer(uint8_t* pBuffer, int64_t iBufferSize, int64_t& riBytesRead, bool& rbLastBuffer);

	// Process streaming buffer completion, filling and submitting the next buffer
	void ProcessNextBuffer(class AudioManager* pAudioManager);

	// Initialize music stream with chunk data and allocate streaming buffers
	bool InitializeMusicStream(common::crc_t audioCrc, class AudioManager* pAudioManager);

	// Static factory method to create and initialize a music streaming voice
	static std::unique_ptr<StreamingVoice> CreateMusicStream(class AudioEngine* pEngine, common::crc_t audioCrc, class AudioManager* pCallback);

	// Apply cross-fade volume to this voice
	void SetCrossFadeVolume(float fProgress, float fMasterVolume, float fMusicVolume, bool bIsCurrent);

	// Set music volume on this voice
	void SetMusicVolume(float fMasterVolume, float fMusicVolume);

	// Music streaming members
	StreamingVoiceFlags_t mFlags;
	common::ChunkLocation mChunkLocation {};          // File offset and size info
	int64_t miCurrentPosition = 0;                    // Current read position in audio data
	int64_t miDataChunkSize = 0;                      // Total audio data size
	int64_t miBlockAlign = 0;                         // ADPCM block alignment size
	int64_t miBufferSize = 0;                         // Size of each streaming buffer
	int64_t miActiveBuffer = 0;                       // Currently playing buffer index
	std::vector<std::unique_ptr<uint8_t[]>> mbuffers; // Streaming buffer pool (3 buffers for music)

	// XAudio2 voice pointer
	IXAudio2SourceVoice* mpVoice = nullptr;
};

} // namespace engine
