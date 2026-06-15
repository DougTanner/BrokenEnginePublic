#pragma once

#if defined(BT_CLIENT)

#include "Frame/Collections/Sounds/Sounds.h"

namespace engine
{

inline constexpr float VolumeToPower(float fMasterVolume, float fSoundVolume, float fLocalVolume = 1.0f)
{
	// More natural-feeling volume controls
	float fVolume = fMasterVolume * fSoundVolume * fLocalVolume;
	return fVolume * fVolume;
}

enum class StaticVoiceFlags : uint8_t
{
	kFadingOut = 0x01,
	kInactive  = 0x02,
};
using StaticVoiceFlags_t = common::Flags<StaticVoiceFlags>;

class StaticVoice
{
public:

	static bool LoadXAudio2SourceVoice(AudioEngine* pAudioEngine, IXAudio2SourceVoice*& rpVoice, common::crc_t audioCrc, bool bOneShot, bool b3d);

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
	float mfFadeOutVolume = 1.0f;
	float mfFadeOutTime = 0.0f;
	XMVECTOR mVecPosition {};
	XMVECTOR mVecVelocity {};
	IXAudio2SourceVoice* mpVoice = nullptr;
	common::crc_t mAudioCrc = 0;
};

} // namespace engine

#endif // BT_CLIENT
