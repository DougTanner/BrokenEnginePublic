#pragma once

#if defined(BT_CLIENT)

#include "Frame/Collections/Sounds/Sounds.h"

namespace engine
{

enum class StaticVoiceFlags : uint8_t
{
	kFadingOut          = 0x01,
	kInactive           = 0x02,
	kActivatedThisFrame = 0x04, // Transient: set by the priority pass, consumed+cleared by the deactivation pass; never persists across passes.
};
using StaticVoiceFlags_t = common::Flags<StaticVoiceFlags>;

enum class LoadVoiceFlags : uint8_t
{
	kOneShot = 0x01,
	k3d      = 0x02,
};
using LoadVoiceFlags_t = common::Flags<LoadVoiceFlags>;

class StaticVoice
{
public:

	static bool LoadXAudio2SourceVoice(AudioEngine* pAudioEngine, IXAudio2SourceVoice*& rpVoice, common::crc_t audioCrc, LoadVoiceFlags_t flags);

	StaticVoice() = delete;
	StaticVoice(IXAudio2SourceVoice* pVoice, sound_t id, float fVolume, float fPitch, float fFadeOutTime, FXMVECTOR vecPosition, FXMVECTOR vecVelocity, common::crc_t audioCrc);

	~StaticVoice();

	StaticVoice(const StaticVoice&) = delete;
	StaticVoice& operator=(const StaticVoice&) = delete;

	StaticVoice(StaticVoice&& rToMove) noexcept;
	StaticVoice& operator=(StaticVoice&& rToMove) noexcept;

	StaticVoiceFlags_t mFlags;
	sound_t mId;
	float mfVolume = 0.0f;
	float mfPitch = 1.0f;
	float mfFadeVolume = 1.0f;
	float mfFadeOutTime = 0.0f;
	XMVECTOR mVecPosition {};
	XMVECTOR mVecVelocity {};
	IXAudio2SourceVoice* mpVoice = nullptr;
	common::crc_t mAudioCrc = 0;
};

} // namespace engine

#endif // BT_CLIENT
