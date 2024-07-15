#pragma once

namespace game
{

struct Frame;

}

namespace engine
{

enum class VoiceFlags : uint8_t
{
	kFadingOut = 0x01,
};
using VoiceFlags_t = common::Flags<VoiceFlags>;

struct Voice
{
	VoiceFlags_t flags;
	int64_t iId = 0;
	int64_t iFrameId = 0;
	float fVolume = 0.0f;
	float fPitch = 1.0f;
	float fFadeOutVolume = 0.0f;
	float fFadeOutTime = 0.0f;
	DirectX::XMVECTOR vecPosition {};
	DirectX::XMVECTOR vecVelocity {};
	IXAudio2SourceVoice* pIXAudio2SourceVoice = nullptr;
};

class AudioManager : public DirectX::IVoiceNotify
{
public:

	AudioManager();
	virtual ~AudioManager();

	void Update(const game::Frame& rFrame);
	IXAudio2SourceVoice* PlayOneShot(common::crc_t audioCrc, bool b3d, float fVolume, float fPitch = 1.0f);
	void XM_CALLCONV PlayOneShot(common::crc_t audioCrc, DirectX::FXMVECTOR vecPosition, float fVolume, float fPitch = 1.0f);

	// IVoiceNotify
	virtual void __cdecl OnBufferEnd();
	virtual void __cdecl OnCriticalError() {}
	virtual void __cdecl OnReset() {}
	virtual void __cdecl OnUpdate() {}
	virtual void __cdecl OnDestroyEngine() noexcept {}
	virtual void __cdecl OnTrim() {}
	virtual void __cdecl GatherStatistics([[maybe_unused]] DirectX::AudioStatistics& stats) const {}
	virtual void __cdecl OnDestroyParent() noexcept {}

	std::unique_ptr<DirectX::AudioEngine> mpAudioEngine;
	common::Timer mRealTime;

private:

	void LoadVoice(IXAudio2SourceVoice*& rpVoice, common::crc_t audioCrc, bool bOneShot, bool bMusic, bool b3d);
	void XM_CALLCONV Apply3d(IXAudio2SourceVoice* pIXAudio2SourceVoice, DirectX::FXMVECTOR vecPosition, DirectX::FXMVECTOR vecVelocity, float fVolume, float fPitch);

	int64_t miMenuMusicIndex = 0;
	int64_t miGameMusicIndex = 0;
	IXAudio2SourceVoice* mpMenuMusicVoice = nullptr;
	IXAudio2SourceVoice* mpGameMusicVoice = nullptr;
	float mfMenuMusicVolume = 0.0f;
	float mfGameMusicVolume = 0.0f;

	int64_t miNextId = 1;
	std::vector<Voice> mVoices;

	DirectX::XMVECTOR mVecListenerPosition {};
	X3DAUDIO_LISTENER mX3dAudioListener
	{
		.OrientFront = {0.0f, 0.0f, -1.0f},
		.OrientTop = {0.0f, -1.0f, 0.0f},
		.Position = {0.0f, 0.0f, 0.0f},
		.Velocity = {0.0f, 0.0f, 0.0f},
	};
};

inline AudioManager* gpAudioManager = nullptr;

} // namespace engine
