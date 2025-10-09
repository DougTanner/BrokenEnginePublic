#pragma once

namespace engine
{

struct LazyChunk;

// Buffer size for music streaming (3 x 16KB)
static constexpr int64_t kiBufferCount = 3;
static constexpr int64_t kiBufferSize = 16 * 1024;

enum class StreamingVoiceFlags : uint8_t
{
	kStreamActive = 0x01,
	kLastBufferSubmitted = 0x02,
};
using StreamingVoiceFlags_t = common::Flags<StreamingVoiceFlags>;

// StreamingVoice represents a music stream with buffered playback
// Used for streaming large audio files (background music)
class StreamingVoice : public IVoiceNotify
{
public:

	StreamingVoice(common::crc_t audioCrc);
	~StreamingVoice();

	StreamingVoice(StreamingVoice&&) = default;
	StreamingVoice& operator=(StreamingVoice&&) = default;
	StreamingVoice(const StreamingVoice&) = delete;
	StreamingVoice& operator=(const StreamingVoice&) = delete;

	float GetRemainingTime() const;
	bool FillBuffer(uint8_t* pBuffer, int64_t iBufferSize, int64_t& riBytesRead, bool& rbLastBuffer);
	void ProcessNextBuffer();
	void SetCrossFadeVolume(float fProgress, float fMasterVolume, float fMusicVolume, bool bIsCurrent);
	void SetMusicVolume(float fMasterVolume, float fMusicVolume);

	// IVoiceNotify
	virtual void OnBufferEnd();
	virtual void OnCriticalError() {}
	virtual void OnReset() {}
	virtual void OnUpdate() {}
	virtual void OnDestroyEngine() noexcept {}
	virtual void OnTrim() {}
	virtual void GatherStatistics([[maybe_unused]] AudioStatistics& stats) const {}
	virtual void OnDestroyParent() noexcept {}

	StreamingVoiceFlags_t mFlags;
	const LazyChunk& mrLazyChunk;
	int64_t miCurrentPosition = 0;                    // Current read position in audio data
	int64_t miBufferSize = 0;                         // Size of each streaming buffer
	int64_t miActiveBuffer = 0;                       // Currently playing buffer index
	std::vector<std::unique_ptr<uint8_t[]>> mBuffers; // Streaming buffer pool (3 buffers for music)

	// XAudio2 voice pointer
	IXAudio2SourceVoice* mpVoice = nullptr;
};

} // namespace engine
