#include "StreamingVoices.h"

#if defined(BT_CLIENT)

#include "File/FileManager.h"
#include "Memory/MemoryManager.h"

namespace engine
{

void StreamingVoices::Init(AudioEngine* pAudioEngine)
{
	mpAudioEngine = pAudioEngine;
}

void StreamingVoices::Play(common::crc_t uiAudioCrc)
{
	std::lock_guard<std::mutex> lock(mMutex);

	// Heap: make_unique<StreamingVoice> (with triple buffers) and vector push_back for crossfade list.
	// These outlive the call (persist until fade-out completes), so workbuffer/pre-alloc won't work.
	ScopedSuppressAllocationTracking suppress;

	// Move current stream to previous list for fade out
	if (mpCurrentStream != nullptr)
	{
		TransitionCurrentToPrevious();
	}

	if (mpAudioEngine == nullptr || !mpAudioEngine->IsAudioDevicePresent()) [[unlikely]]
	{
		return;
	}

	// Load new track as current
	CreateStream(uiAudioCrc);
}

void StreamingVoices::SetNextTrackCallback(std::function<common::crc_t()> callback)
{
	std::lock_guard<std::mutex> lock(mMutex);

	mGetNextTrack = std::move(callback);
}

void StreamingVoices::CheckTrackTransition()
{
	std::lock_guard<std::mutex> lock(mMutex);

	// Check if current stream has ended (position >= size or last buffer submitted)
	if (mpCurrentStream != nullptr)
	{
		float fRemaining = mpCurrentStream->GetRemainingTime();

		bool bShouldTransition = (fRemaining <= kfCrossfadeDuration) || (mpCurrentStream->miCurrentPosition >= mpCurrentStream->mpLazyChunk->header.iSize) || (mpCurrentStream->mFlags & StreamingVoiceFlags::kLastBufferSubmitted);
		if (bShouldTransition && mGetNextTrack)
		{
			common::crc_t uiNextTrackCrc = mGetNextTrack();

			// Move current stream to previous list for fade out
			TransitionCurrentToPrevious();

			// Load next track as current
			CreateStream(uiNextTrackCrc);
		}
	}
}

void StreamingVoices::Update(float fDeltaTime)
{
	// Heap: Member vector collects faded-out streams for deferred destruction outside the mutex.
	// Allocation reused across frames. Suppression covers potential growth and destructor calls.
	ScopedSuppressAllocationTracking suppress;

	// Collect streams to destroy outside the lock to prevent deadlock with XAudio2 callbacks
	{
		std::lock_guard<std::mutex> lock(mMutex);

		// Submit pending streaming buffers (file I/O on main thread instead of XAudio2 callback)
		if (mpCurrentStream != nullptr)
		{
			SubmitBuffers(*mpCurrentStream);
		}
		for (const std::unique_ptr<StreamingVoice>& pStream : mPreviousStreams)
		{
			SubmitBuffers(*pStream);
		}

		// Update current stream volume (fade in)
		if (mpCurrentStream != nullptr)
		{
			mpCurrentStream->UpdateVolume(fDeltaTime);
		}

		// Update previous streams (fade out) and remove completed ones
		for (auto it = mPreviousStreams.begin(); it != mPreviousStreams.end();)
		{
			if ((*it)->UpdateVolume(fDeltaTime))
			{
				// Fade out complete, move to deferred destruction list
				mStreamsToDestroy.push_back(std::move(*it));
				it = mPreviousStreams.erase(it);
			}
			else
			{
				++it;
			}
		}
	}

	// Destruction happens here, after mutex is released
	// DestroyVoice() can now safely wait for OnBufferEnd callbacks
	mStreamsToDestroy.clear();
}

void StreamingVoices::Clear(bool bNullVoicesBeforeDestroy)
{
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

void StreamingVoices::SubmitBuffers(StreamingVoice& rStream)
{
	int64_t iConsumed = rStream.miBuffersConsumed.exchange(0, std::memory_order_acquire);

	for (int64_t i = 0; i < iConsumed; ++i)
	{
		if (rStream.mFlags & StreamingVoiceFlags::kLastBufferSubmitted)
		{
			break;
		}

		int64_t iNextBuffer = (rStream.miActiveBuffer + 1) % kiBufferCount;

		bool bLastBuffer = false;
		int64_t iBytesRead = 0;
		if (rStream.FillBuffer(rStream.mBuffers[iNextBuffer], iBytesRead, bLastBuffer))
		{
			XAUDIO2_BUFFER xaudio2Buffer
			{
				.Flags = bLastBuffer ? XAUDIO2_END_OF_STREAM : 0u,
				.AudioBytes = static_cast<UINT32>(iBytesRead),
				.pAudioData = rStream.mBuffers[iNextBuffer],
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
				LOG(kAudio, kWarning, "SubmitBuffers: Failed to submit buffer, HRESULT: 0x{:08X}", hr);
				rStream.mFlags.Set(StreamingVoiceFlags::kLastBufferSubmitted);
				break;
			}
			rStream.miActiveBuffer = iNextBuffer;
			rStream.mFlags.Set(StreamingVoiceFlags::kLastBufferSubmitted, bLastBuffer);
		}
		else
		{
			LOG(kAudio, kDebug, "SubmitBuffers: stream reached end, marking as inactive");
			rStream.mFlags.Set(StreamingVoiceFlags::kLastBufferSubmitted);
		}
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
