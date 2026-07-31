#include "StreamingVoices.h"

#if defined(BT_CLIENT)

#include "StreamingVoice.h"

#include "File/FileManager.h"

namespace engine
{

StreamingVoices::StreamingVoices() = default;

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

	// An explicit track supersedes any pending transition retry.
	muiRetryTrackCrc = 0;

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

// Deliberately skips mFillWorker.Wait() (the only mutating public method that does): it swaps
// only mGetNextTrack, which the fill worker never reads — the sole reader is main-thread
// CheckTrackTransition, under the same mutex.
void StreamingVoices::SetNextTrackCallback(std::function<common::crc_t()> callback)
{
	std::lock_guard<std::mutex> lock(mMutex);

	mGetNextTrack = std::move(callback);
}

void StreamingVoices::CheckTrackTransition()
{
	mFillWorker.Wait();

	std::lock_guard<std::mutex> lock(mMutex);

	if (mGetNextTrack && (mpCurrentStream == nullptr || mpCurrentStream->ShouldTransition()))
	{
		// A failed CreateStream (voice allocation) must not consume another playlist entry: retry the
		// same track next call instead of advancing past it.
		common::crc_t uiNextTrackCrc = muiRetryTrackCrc != 0 ? muiRetryTrackCrc : mGetNextTrack();
		if (mpCurrentStream != nullptr)
		{
			TransitionCurrentToPrevious();
		}
		CreateStream(uiNextTrackCrc);
		muiRetryTrackCrc = mpCurrentStream == nullptr ? uiNextTrackCrc : 0;
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
			mpCurrentStream->DrainConsumedAndSubmitReady();
		}
		for (const std::unique_ptr<StreamingVoice>& pStream : mPreviousStreams)
		{
			pStream->DrainConsumedAndSubmitReady();
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

	// Unlike Update above, the stream containers are destroyed inside this lock scope. That is only safe because
	// StreamingVoice::OnBufferEnd is a bare atomic increment that never takes mMutex, so DestroyVoice() can wait for
	// outstanding callbacks while we still hold it. Keep that callback lock-free.
	std::lock_guard<std::mutex> lock(mMutex);

	if (bNullVoicesBeforeDestroy)
	{
		if (mpCurrentStream != nullptr)
		{
			mpCurrentStream->DetachXAudio2Voice();
		}

		for (std::unique_ptr<StreamingVoice>& pStream : mPreviousStreams)
		{
			if (pStream != nullptr)
			{
				pStream->DetachXAudio2Voice();
			}
		}

		for (std::unique_ptr<StreamingVoice>& pStream : mStreamsToDestroy)
		{
			if (pStream != nullptr)
			{
				pStream->DetachXAudio2Voice();
			}
		}
	}

	mpCurrentStream.reset();
	mPreviousStreams.clear();
	mStreamsToDestroy.clear();
	// Post-clear playback restarts from the next-track callback, not a stale retry.
	muiRetryTrackCrc = 0;
}

// Deliberately off-contract: no mFillWorker.Wait(), no mMutex. Safe because the fill worker
// never mutates the three stream containers (only stream internals, under mMutex) and every
// container mutation happens on the main thread — the same thread all callers run on
// (AudioManager::Update's stream counter and Suspend's teardown log).
int64_t StreamingVoices::GetStreamCount() const
{
	ASSERT(common::gpMultithreading->IsMainThread());
	return (mpCurrentStream != nullptr ? 1 : 0) + static_cast<int64_t>(mPreviousStreams.size()) + static_cast<int64_t>(mStreamsToDestroy.size());
}

void StreamingVoices::FillReadyBuffers()
{
	std::lock_guard<std::mutex> lock(mMutex);

	if (mpCurrentStream != nullptr)
	{
		mpCurrentStream->FillReadyBuffers();
	}
	for (const std::unique_ptr<StreamingVoice>& pStream : mPreviousStreams)
	{
		pStream->FillReadyBuffers();
	}
}

void StreamingVoices::CreateStream(common::crc_t uiAudioCrc)
{
	const LazyChunk& rLazyChunk = gpFileManager->GetLazyChunkMap().at(uiAudioCrc);
	IXAudio2SourceVoice* pVoice = nullptr;
	mpAudioEngine->AllocateVoice(&rLazyChunk.header.audioHeader.waveFormat, SoundEffectInstance_Default, false, &pVoice);
	if (pVoice != nullptr)
	{
		mpCurrentStream = std::make_unique<StreamingVoice>(mpAudioEngine, pVoice, &rLazyChunk);
	}
	else
	{
		char pcHex[20] {};
		LOG(kAudio, kWarning, "CreateStream AllocateVoice failed for CRC {}", common::ToHex(std::span(pcHex), uiAudioCrc));
	}
}

void StreamingVoices::TransitionCurrentToPrevious()
{
	mpCurrentStream->BeginFadeOut();
	mPreviousStreams.push_back(std::move(mpCurrentStream));
}

} // namespace engine

#endif // defined(BT_CLIENT)
