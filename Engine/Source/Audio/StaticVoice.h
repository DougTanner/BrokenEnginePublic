#pragma once

namespace engine
{

// Voice flags for tracking sound effect state
enum class VoiceFlags : uint8_t
{
	kFadingOut = 0x01,
};
using VoiceFlags_t = common::Flags<VoiceFlags>;

// StaticVoice represents a sound effect with position, volume, and pitch
// Used for non-streaming audio (explosions, impacts, etc.)
struct StaticVoice
{
	VoiceFlags_t mFlags;
	int64_t miId = 0;
	int64_t miFrameId = 0;
	float mfVolume = 0.0f;
	float mfPitch = 1.0f;
	float mfFadeOutVolume = 0.0f;
	float mfFadeOutTime = 0.0f;
	XMVECTOR mVecPosition {};
	XMVECTOR mVecVelocity {};
	IXAudio2SourceVoice* mpVoice = nullptr;
};

} // namespace engine
