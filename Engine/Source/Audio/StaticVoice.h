#pragma once

namespace engine
{

struct SoundInfo;
struct Sound;

enum class StaticVoiceFlags : uint8_t
{
	kFadingOut = 0x01,
};
using StaticVoiceFlags_t = common::Flags<StaticVoiceFlags>;

class StaticVoice
{
public:

	static bool LoadXAudio2SourceVoice(AudioEngine* pAudioEngine, IXAudio2SourceVoice*& rpVoice, common::crc_t audioCrc, bool bOneShot, bool b3d);

	StaticVoice() = delete;
	StaticVoice(IXAudio2SourceVoice* pVoice, const SoundInfo& rSoundInfo, const Sound& rSound);

	virtual ~StaticVoice();

	StaticVoice(const StaticVoice&) = delete;
	StaticVoice& operator=(const StaticVoice&) = delete;

	StaticVoice(StaticVoice&& rToMove) noexcept;
	StaticVoice& operator=(StaticVoice&& rToMove) noexcept;

	StaticVoiceFlags_t mFlags;
	int64_t miFrameId = 0;
	float mfVolume = 0.0f;
	float mfPitch = 1.0f;
	float mfFadeOutVolume = 1.0f;
	float mfFadeOutTime = 0.0f;
	XMVECTOR mVecPosition {};
	XMVECTOR mVecVelocity {};
	IXAudio2SourceVoice* mpVoice = nullptr;
};

} // namespace engine
