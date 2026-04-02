#pragma once

#if defined(BT_CLIENT)

namespace engine
{

struct LazyChunk;

inline constexpr int64_t kiBufferCount = 3;
inline constexpr int64_t kiBufferSize = 16 * 1024;
inline constexpr float kfCrossfadeDuration = 1.0f;

enum class StreamingVoiceFlags : uint8_t
{
	kFadingIn            = 0x01,
	kFadingOut           = 0x02,
	kLastBufferSubmitted = 0x04,
};
using StreamingVoiceFlags_t = common::Flags<StreamingVoiceFlags>;

class StreamingVoice : public IVoiceNotify
{
public:

	StreamingVoice() = delete;
	StreamingVoice(IXAudio2SourceVoice* pVoice, const LazyChunk* pLazyChunk);

	~StreamingVoice();

	StreamingVoice(const StreamingVoice&) = delete;
	StreamingVoice& operator=(const StreamingVoice&) = delete;

	StreamingVoice(StreamingVoice&&) = delete;
	StreamingVoice& operator=(StreamingVoice&&) = delete;

	float GetRemainingTime() const;
	bool FillBuffer(uint8_t (&rBuffer)[kiBufferSize], int64_t& riBytesRead, bool& rbLastBuffer);
	bool UpdateVolume(float fDeltaTime);

	// IVoiceNotify
	void OnBufferEnd() override;
	void OnCriticalError() override {}
	void OnReset() override {}
	void OnUpdate() override {}
	void OnDestroyEngine() noexcept override {}
	void OnTrim() override {}
	void GatherStatistics([[maybe_unused]] AudioStatistics& rStats) const override {}
	void OnDestroyParent() noexcept override {}

	StreamingVoiceFlags_t mFlags = StreamingVoiceFlags::kFadingIn;
	const LazyChunk* mpLazyChunk = nullptr;
	int64_t miCurrentPosition = 0;
	float mfCurrentVolume = 0.0f;
	int64_t miActiveBuffer = 0;
	std::atomic<int64_t> miBuffersConsumed = 0;
	uint8_t mBuffers[kiBufferCount][kiBufferSize] {};
	IXAudio2SourceVoice* mpVoice = nullptr;
};

} // namespace engine

#endif // BT_CLIENT
