#include "StaticVoice.h"

#include "Audio/AudioManager.h"
#include "File/FileManager.h"
#include "Frame/Pools/Sounds.h"

#include "Ui/Wrapper.h"

namespace engine
{

using enum StaticVoiceFlags;

StaticVoice::StaticVoice(AudioEngine* pAudioEngine, const SoundInfo& rSoundInfo, const Sound& rSound)
: miFrameId(rSound.iId)
, mfVolume(rSoundInfo.fVolume)
, mfPitch(rSoundInfo.fPitch)
, mfFadeOutVolume(rSoundInfo.fVolume)
, mfFadeOutTime(rSoundInfo.fFadeOutTime)
{
	if (StaticVoice::LoadVoice(pAudioEngine, mpVoice, rSoundInfo.uiCrc, false, true))
	{
		CHECK_HRESULT(mpVoice->SetVolume(CalculateVolume(gMasterVolume.Get(), gSoundVolume.Get(), mfVolume)));
		CHECK_HRESULT(mpVoice->Start());
		mFlags |= kLoaded;
	}
}

StaticVoice::~StaticVoice()
{
	Destroy();
}

bool StaticVoice::LoadVoice(AudioEngine* pAudioEngine, IXAudio2SourceVoice*& rpVoice, common::crc_t audioCrc, bool bOneShot, bool b3d)
{
	if (pAudioEngine == nullptr || !pAudioEngine->IsAudioDevicePresent()) [[unlikely]]
	{
		return false;
	}

	if (!gpFileManager->IsChunkReady(audioCrc))
	{
		gpFileManager->RequestChunkLoad(audioCrc, engine::LoadPriority::kNormal);
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

void StaticVoice::Destroy()
{
	if (gpAudioManager->mpAudioEngine == nullptr || mpVoice == nullptr)
	{
		return;
	}

	if (SUCCEEDED(mpVoice->Stop()))
	{
		mpVoice->FlushSourceBuffers();
	}
	gpAudioManager->mpAudioEngine->DestroyVoice(mpVoice);
	mpVoice = nullptr;
}

} // namespace engine
