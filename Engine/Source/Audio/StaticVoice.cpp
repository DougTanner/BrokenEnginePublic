#include "StaticVoice.h"

#if defined(BT_CLIENT)

namespace engine
{

using enum StaticVoiceFlags;

bool StaticVoice::LoadXAudio2SourceVoice(AudioEngine* pAudioEngine, IXAudio2SourceVoice*& rpVoice, common::crc_t audioCrc, bool bOneShot, bool b3d)
{
	if (pAudioEngine == nullptr || !pAudioEngine->IsAudioDevicePresent()) [[unlikely]]
	{
		Log(kLogAudio, "StaticVoice::LoadXAudio2SourceVoice Audio device not present");
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
		Log(kLogAudio, "StaticVoice::LoadXAudio2SourceVoice AllocateVoice failed for CRC {:#018x}", audioCrc);
		return false;
	}

	CHECK_HRESULT(rpVoice->SetVolume(0.0f));

	XAUDIO2_BUFFER xaudio2Buffer
	{
		.Flags = XAUDIO2_END_OF_STREAM,
		.AudioBytes = static_cast<UINT32>(rLazyChunk.header.iSize),
		.pAudioData = reinterpret_cast<const BYTE*>(rLazyChunk.pData),
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

StaticVoice::StaticVoice(IXAudio2SourceVoice* pVoice, sound_t id, float fVolume, float fPitch, float fFadeOutTime, FXMVECTOR vecPosition, FXMVECTOR vecVelocity)
: mpVoice(pVoice)
, mId(id)
, mfVolume(fVolume)
, mfPitch(fPitch)
, mfFadeOutVolume(1.0f)
, mfFadeOutTime(fFadeOutTime)
, mVecPosition(vecPosition)
, mVecVelocity(vecVelocity)
{
	ASSERT(mfFadeOutTime > 0.0f);

	CHECK_HRESULT(mpVoice->SetVolume(VolumeToPower(gMasterVolume.Get(), gSoundVolume.Get(), mfVolume)));
	CHECK_HRESULT(mpVoice->Start());
}

StaticVoice::~StaticVoice()
{
	DestroyXAudio2SourceVoice(mpVoice);
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
		mId = rToMove.mId;
		mfVolume = rToMove.mfVolume;
		mfPitch = rToMove.mfPitch;
		mfFadeOutVolume = rToMove.mfFadeOutVolume;
		mfFadeOutTime = rToMove.mfFadeOutTime;
		mVecPosition = rToMove.mVecPosition;
		mVecVelocity = rToMove.mVecVelocity;

		DestroyXAudio2SourceVoice(mpVoice);
		mpVoice = rToMove.mpVoice;
		rToMove.mpVoice = nullptr;
	}

	return *this;
}

} // namespace engine

#endif // BT_CLIENT
