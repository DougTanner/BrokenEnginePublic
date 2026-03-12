#pragma once

#if defined(BT_CLIENT)

namespace engine
{

struct LazyChunk;

constexpr int64_t kiBufferCount = 3;
constexpr int64_t kiBufferSize = 16 * 1024;
constexpr float kfCrossfadeDuration = 1.0f;

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

	StreamingVoice(StreamingVoice&& rToMove) noexcept;
	StreamingVoice& operator=(StreamingVoice&& rToMove) noexcept;

	float GetRemainingTime() const;
	bool FillBuffer(std::vector<uint8_t>& rBuffer, int64_t& riBytesRead, bool& rbLastBuffer);
	bool UpdateVolume(float fDeltaTime);

	// IVoiceNotify
	virtual void OnBufferEnd();
	virtual void OnCriticalError() {}
	virtual void OnReset() {}
	virtual void OnUpdate() {}
	virtual void OnDestroyEngine() noexcept {}
	virtual void OnTrim() {}
	virtual void GatherStatistics([[maybe_unused]] AudioStatistics& rStats) const {}
	virtual void OnDestroyParent() noexcept {}

	StreamingVoiceFlags_t mFlags = StreamingVoiceFlags::kFadingIn;
	const LazyChunk* mpLazyChunk = nullptr;
	int64_t miCurrentPosition = 0;
	float mfCurrentVolume = 0.0f;
	int64_t miActiveBuffer = 0;
	std::vector<std::vector<uint8_t>> mBuffers;
	IXAudio2SourceVoice* mpVoice = nullptr;
};

} // namespace engine

#endif // BT_CLIENT
