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

// Slot states transition Empty -> Filling -> Ready -> Submitted -> Empty.
// Worker thread owns Empty -> Filling -> Ready; main thread owns Ready -> Submitted -> Empty.
// Acquire/release on store/load establishes happens-before for the non-atomic payload (mSlotBytesRead / mbSlotLastBuffer).
// The Filling intermediate state is written with relaxed order — safe because StreamingVoices::mMutex brackets the
// entire fill, so no consumer can observe an intermediate slot. The atomic state machine is what synchronizes
// across the worker -> main publication boundary; the mutex is what serialises Update / Play / Clear / fill.
// A third party (XAudio2's callback thread) writes only to miBuffersConsumed and never to mSlotStates.
enum class SlotState : uint8_t
{
	kEmpty     = 0,
	kFilling   = 1,
	kReady     = 2,
	kSubmitted = 3,
};

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
	void FillSlot(int64_t iSlot);
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
	int64_t miNextSubmit = 0;
	int64_t miNextConsume = 0;
	bool mbStarted = false;
	std::atomic<bool> mbFillFailed = false;
	std::atomic<int64_t> miBuffersConsumed = 0;
	std::atomic<uint8_t> mSlotStates[kiBufferCount] {};
	int64_t mSlotBytesRead[kiBufferCount] {};
	bool mbSlotLastBuffer[kiBufferCount] {};
	uint8_t mBuffers[kiBufferCount][kiBufferSize] {};
	IXAudio2SourceVoice* mpVoice = nullptr;
};

} // namespace engine

#endif // BT_CLIENT
