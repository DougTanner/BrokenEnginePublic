#include "StaticVoice.h"

#include "Audio/AudioManager.h"
#include "File/FileManager.h"
#include "Frame/Pools/Sounds.h"

namespace engine
{

using enum StaticVoiceFlags;

bool StaticVoice::LoadXAudio2SourceVoice(AudioEngine* pAudioEngine, IXAudio2SourceVoice*& rpVoice, common::crc_t audioCrc, bool bOneShot, bool b3d)
{
	if (pAudioEngine == nullptr || !pAudioEngine->IsAudioDevicePresent()) [[unlikely]]
	{
		return false;
	}

	if (!gpFileManager->IsChunkReady(audioCrc))
	{
		gpFileManager->RequestChunkLoad(std::to_array<common::crc_t>({audioCrc}), LoadPriority::kHigh);
		return false;
	}

	const LazyChunk& rLazyChunk = gpFileManager->GetLazyChunkMap().at(audioCrc);

	// 3d sounds should have only one channel, re-export the sound as mono
	ASSERT(!b3d || rLazyChunk.header.audioHeader.waveFormat.nChannels == 1);

	pAudioEngine->AllocateVoice(&rLazyChunk.header.audioHeader.waveFormat, SoundEffectInstance_Default, bOneShot, &rpVoice);
	if (rpVoice == nullptr)
	{
		return false;
	}

	CHECK_HRESULT(rpVoice->SetVolume(0.0f));

	XAUDIO2_BUFFER xaudio2Buffer
	{
		.Flags = XAUDIO2_END_OF_STREAM,
		.AudioBytes = static_cast<UINT32>(rLazyChunk.header.iSize),
		.pAudioData = reinterpret_cast<const BYTE*>(rLazyChunk.data.data()),
		.PlayBegin = 0,
		.PlayLength = 0,
		.LoopBegin = 0,
		.LoopLength = 0,
		.LoopCount = bOneShot ? 0u : XAUDIO2_LOOP_INFINITE,
		.pContext = nullptr,
	};
	CHECK_HRESULT(rpVoice->SubmitSourceBuffer(&xaudio2Buffer));

	return true;
}

StaticVoice::StaticVoice(IXAudio2SourceVoice* pVoice, const SoundInfo& rSoundInfo, const Sound& rSound)
: mpVoice(pVoice)
, miFrameId(rSound.iId)
, mfVolume(rSoundInfo.fVolume)
, mfPitch(rSoundInfo.fPitch)
, mfFadeOutVolume(1.0f)
, mfFadeOutTime(rSoundInfo.fFadeOutTime)
{
	ASSERT(mfFadeOutTime > 0.0f);

	CHECK_HRESULT(mpVoice->SetVolume(VolumeToPower(gMasterVolume.Get(), gSoundVolume.Get(), mfVolume)));
	CHECK_HRESULT(mpVoice->Start());
}

StaticVoice::~StaticVoice()
{
	if (mpVoice != nullptr)
	{
		mpVoice->Stop(0, XAUDIO2_COMMIT_NOW);
		mpVoice->FlushSourceBuffers();

		if (gpAudioManager->mpAudioEngine != nullptr)
		{
			gpAudioManager->mpAudioEngine->DestroyVoice(mpVoice);
		}
	}
}

StaticVoice::StaticVoice(StaticVoice&& rToMove) noexcept
{
	*this = std::move(rToMove);
}

StaticVoice& StaticVoice::operator=(StaticVoice&& rToMove) noexcept
{
	if (this != &rToMove)
	{
		mFlags = rToMove.mFlags;
		miFrameId = rToMove.miFrameId;
		mfVolume = rToMove.mfVolume;
		mfPitch = rToMove.mfPitch;
		mfFadeOutVolume = rToMove.mfFadeOutVolume;
		mfFadeOutTime = rToMove.mfFadeOutTime;
		mVecPosition = rToMove.mVecPosition;
		mVecVelocity = rToMove.mVecVelocity;

		if (gpAudioManager->mpAudioEngine != nullptr)
		{
			gpAudioManager->mpAudioEngine->DestroyVoice(mpVoice);
		}
		mpVoice = rToMove.mpVoice;
		rToMove.mpVoice = nullptr;
	}

	return *this;
}

} // namespace engine
