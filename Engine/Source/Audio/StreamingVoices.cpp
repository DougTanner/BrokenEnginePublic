#include "StreamingVoices.h"

#if defined(BT_CLIENT)

#include "File/FileManager.h"
#include "Memory/MemoryManager.h"

namespace engine
{

StreamingVoices::~StreamingVoices()
{
	// Defensive: drain any in-flight worker fill before members tear down. ~AudioManager always calls Clear first
	// (which Wait()s too), so this is belt-and-suspenders, but keeps the contract local to this class.
	mFillWorker.Wait();
}

void StreamingVoices::Init(AudioEngine* pAudioEngine)
{
	mpAudioEngine = pAudioEngine;
}

void StreamingVoices::Play(common::crc_t uiAudioCrc)
{
	mFillWorker.Wait();

	std::lock_guard<std::mutex> lock(mMutex);

	// Heap: make_unique<StreamingVoice> (with triple buffers) and vector push_back for crossfade list.
	// These outlive the call (persist until fade-out completes), so workbuffer/pre-alloc won't work.
	ScopedSuppressAllocationTracking suppress;

	if (mpCurrentStream != nullptr)
	{
		TransitionCurrentToPrevious();
	}

	if (mpAudioEngine == nullptr || !mpAudioEngine->IsAudioDevicePresent()) [[unlikely]]
	{
		return;
	}

	CreateStream(uiAudioCrc);
}

void StreamingVoices::SetNextTrackCallback(std::function<common::crc_t()> callback)
{
	std::lock_guard<std::mutex> lock(mMutex);

	mGetNextTrack = std::move(callback);
}

void StreamingVoices::CheckTrackTransition()
{
	mFillWorker.Wait();

	std::lock_guard<std::mutex> lock(mMutex);

	if (mpCurrentStream != nullptr)
	{
		float fRemaining = mpCurrentStream->GetRemainingTime();

		bool bShouldTransition = (fRemaining <= kfCrossfadeDuration) || (mpCurrentStream->miCurrentPosition >= mpCurrentStream->mpLazyChunk->header.iSize) || (mpCurrentStream->mFlags & StreamingVoiceFlags::kLastBufferSubmitted);
		if (bShouldTransition && mGetNextTrack)
		{
			common::crc_t uiNextTrackCrc = mGetNextTrack();

			TransitionCurrentToPrevious();
			CreateStream(uiNextTrackCrc);
		}
	}
}

void StreamingVoices::Update(float fDeltaTime)
{
	// Heap: Member vector collects faded-out streams for deferred destruction outside the mutex.
	// Allocation reused across frames. Suppression covers potential growth and destructor calls.
	ScopedSuppressAllocationTracking suppress;

	mFillWorker.Wait();

	{
		std::lock_guard<std::mutex> lock(mMutex);

		// Consumer-only: drain consumed slots and submit any worker-filled slots. File I/O lives on mFillWorker.
		if (mpCurrentStream != nullptr)
		{
			DrainConsumedAndSubmitReady(*mpCurrentStream);
		}
		for (const std::unique_ptr<StreamingVoice>& pStream : mPreviousStreams)
		{
			DrainConsumedAndSubmitReady(*pStream);
		}

		if (mpCurrentStream != nullptr)
		{
			mpCurrentStream->UpdateVolume(fDeltaTime);
		}

		for (auto it = mPreviousStreams.begin(); it != mPreviousStreams.end();)
		{
			if ((*it)->UpdateVolume(fDeltaTime))
			{
				mStreamsToDestroy.push_back(std::move(*it));
				it = mPreviousStreams.erase(it);
			}
			else
			{
				++it;
			}
		}

		// Wake worker to refill any kEmpty slots before releasing the mutex; the worker will take it itself.
		if (mpCurrentStream != nullptr || !mPreviousStreams.empty())
		{
			mFillWorker.Wake([this]() { FillReadyBuffers(); });
		}
	}

	// Destruction happens here, after mutex is released
	// DestroyVoice() can now safely wait for OnBufferEnd callbacks
	mStreamsToDestroy.clear();
}

void StreamingVoices::Clear(bool bNullVoicesBeforeDestroy)
{
	mFillWorker.Wait();

	std::lock_guard<std::mutex> lock(mMutex);

	if (bNullVoicesBeforeDestroy)
	{
		if (mpCurrentStream != nullptr)
		{
			mpCurrentStream->mpVoice = nullptr;
		}

		for (std::unique_ptr<StreamingVoice>& pStream : mPreviousStreams)
		{
			if (pStream != nullptr)
			{
				pStream->mpVoice = nullptr;
			}
		}

		for (std::unique_ptr<StreamingVoice>& pStream : mStreamsToDestroy)
		{
			if (pStream != nullptr)
			{
				pStream->mpVoice = nullptr;
			}
		}
	}

	mpCurrentStream.reset();
	mPreviousStreams.clear();
	mStreamsToDestroy.clear();
}

int64_t StreamingVoices::GetStreamCount() const
{
	return (mpCurrentStream != nullptr ? 1 : 0) + static_cast<int64_t>(mPreviousStreams.size()) + static_cast<int64_t>(mStreamsToDestroy.size());
}

void StreamingVoices::DrainConsumedAndSubmitReady(StreamingVoice& rStream)
{
	int64_t iConsumed = rStream.miBuffersConsumed.exchange(0, std::memory_order_acquire);
	for (int64_t i = 0; i < iConsumed; ++i)
	{
		rStream.mSlotStates[rStream.miNextConsume].store(static_cast<uint8_t>(SlotState::kEmpty), std::memory_order_release);
		rStream.miNextConsume = (rStream.miNextConsume + 1) % kiBufferCount;
	}

	while (!(rStream.mFlags & StreamingVoiceFlags::kLastBufferSubmitted))
	{
		uint8_t uiState = rStream.mSlotStates[rStream.miNextSubmit].load(std::memory_order_acquire);
		if (uiState != static_cast<uint8_t>(SlotState::kReady))
		{
			break;
		}

		int64_t iBytesRead = rStream.mSlotBytesRead[rStream.miNextSubmit];
		bool bLastBuffer = rStream.mbSlotLastBuffer[rStream.miNextSubmit];

		if (iBytesRead == 0)
		{
			// Worker hit EOF or read failure with nothing to submit; mark done and recycle the slot.
			rStream.mFlags.Set(StreamingVoiceFlags::kLastBufferSubmitted);
			rStream.mSlotStates[rStream.miNextSubmit].store(static_cast<uint8_t>(SlotState::kEmpty), std::memory_order_release);
			rStream.miNextSubmit = (rStream.miNextSubmit + 1) % kiBufferCount;
			break;
		}

		XAUDIO2_BUFFER xaudio2Buffer
		{
			.Flags = bLastBuffer ? XAUDIO2_END_OF_STREAM : 0u,
			.AudioBytes = static_cast<UINT32>(iBytesRead),
			.pAudioData = rStream.mBuffers[rStream.miNextSubmit],
			.PlayBegin = 0,
			.PlayLength = 0,
			.LoopBegin = 0,
			.LoopLength = 0,
			.LoopCount = 0,
			.pContext = &rStream,
		};
		HRESULT hr = rStream.mpVoice->SubmitSourceBuffer(&xaudio2Buffer);
		if (FAILED(hr))
		{
			LOG(kAudio, kWarning, "SubmitSourceBuffer failed, HRESULT: 0x{:08X}", hr);
			rStream.mFlags.Set(StreamingVoiceFlags::kLastBufferSubmitted);
			break;
		}

		rStream.mSlotStates[rStream.miNextSubmit].store(static_cast<uint8_t>(SlotState::kSubmitted), std::memory_order_release);
		rStream.miNextSubmit = (rStream.miNextSubmit + 1) % kiBufferCount;

		if (!rStream.mbStarted)
		{
			CHECK_HRESULT(rStream.mpVoice->Start());
			rStream.mbStarted = true;
		}

		if (bLastBuffer)
		{
			rStream.mFlags.Set(StreamingVoiceFlags::kLastBufferSubmitted);
			break;
		}
	}
}

void StreamingVoices::FillReadyBuffers()
{
	std::lock_guard<std::mutex> lock(mMutex);

	auto fillOne = [](StreamingVoice& rStream)
	{
		if (rStream.mbFillFailed.load(std::memory_order_acquire))
		{
			return;
		}
		for (int64_t iSlot = 0; iSlot < kiBufferCount; ++iSlot)
		{
			if (rStream.mSlotStates[iSlot].load(std::memory_order_acquire) != static_cast<uint8_t>(SlotState::kEmpty))
			{
				continue;
			}
			rStream.mSlotStates[iSlot].store(static_cast<uint8_t>(SlotState::kFilling), std::memory_order_relaxed);
			rStream.FillSlot(iSlot);
			rStream.mSlotStates[iSlot].store(static_cast<uint8_t>(SlotState::kReady), std::memory_order_release);
			if (rStream.mbFillFailed.load(std::memory_order_acquire))
			{
				return;
			}
		}
	};

	if (mpCurrentStream != nullptr)
	{
		fillOne(*mpCurrentStream);
	}
	for (const std::unique_ptr<StreamingVoice>& pStream : mPreviousStreams)
	{
		fillOne(*pStream);
	}
}

void StreamingVoices::CreateStream(common::crc_t uiAudioCrc)
{
	const LazyChunk& rLazyChunk = gpFileManager->GetLazyChunkMap().at(uiAudioCrc);
	IXAudio2SourceVoice* pVoice = nullptr;
	mpAudioEngine->AllocateVoice(&rLazyChunk.header.audioHeader.waveFormat, SoundEffectInstance_Default, false, &pVoice);
	if (pVoice != nullptr)
	{
		mpCurrentStream = std::make_unique<StreamingVoice>(pVoice, &rLazyChunk);
	}
	else
	{
		LOG(kAudio, kDebug, "CreateStream AllocateVoice failed for CRC {:#018x}", uiAudioCrc);
	}
}

void StreamingVoices::TransitionCurrentToPrevious()
{
	mpCurrentStream->mFlags.Clear(StreamingVoiceFlags::kFadingIn);
	mpCurrentStream->mFlags.Set(StreamingVoiceFlags::kFadingOut);
	mPreviousStreams.push_back(std::move(mpCurrentStream));
}

} // namespace engine

#endif // defined(BT_CLIENT)
