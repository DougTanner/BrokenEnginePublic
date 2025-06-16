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
	XMVECTOR vecPosition {};
	XMVECTOR vecVelocity {};
	IXAudio2SourceVoice* pIXAudio2SourceVoice = nullptr;
};

struct MusicStream
{
	IXAudio2SourceVoice* pVoice = nullptr;           // Associated XAudio2 voice
	common::ChunkLocation chunkLocation {};          // File offset and size info
	uint64_t uiCurrentPosition = 0;                  // Current read position in audio data
	uint32_t uiDataChunkSize = 0;                    // Total audio data size (from offset 0x4A)
	uint32_t uiBlockAlign = 0;                       // ADPCM block alignment size
	std::vector<std::unique_ptr<uint8_t[]>> buffers; // Streaming buffer pool
	size_t uiBufferSize = 0;                         // Size of each streaming buffer
	int64_t iActiveBuffer = 0;                       // Currently playing buffer index
	bool bStreamActive = false;                      // Whether streaming is active
	bool bLastBufferSubmitted = false;               // Whether the last buffer has been submitted
	
	// Destructor to ensure voice cleanup
	~MusicStream();
};

enum class CrossFadeState : uint8_t
{
	kNone,
	kStarting,
	kActive
};

class AudioManager : public IVoiceNotify
{
public:

	AudioManager();
	virtual ~AudioManager();

	void Update(const game::Frame& rFrame);
	IXAudio2SourceVoice* PlayOneShot(common::crc_t audioCrc, bool b3d, float fVolume, float fPitch = 1.0f);
	void XM_CALLCONV PlayOneShot(common::crc_t audioCrc, FXMVECTOR vecPosition, float fVolume, float fPitch = 1.0f);

	// IVoiceNotify
	virtual void OnBufferEnd();
	virtual void OnCriticalError();
	virtual void OnReset();
	virtual void OnUpdate();
	virtual void OnDestroyEngine() noexcept;
	virtual void OnTrim();
	virtual void GatherStatistics([[maybe_unused]] AudioStatistics& stats) const {}
	virtual void OnDestroyParent() noexcept;

	std::unique_ptr<AudioEngine> mpAudioEngine;
	common::Timer mRealTime;

private:

	bool LoadVoice(IXAudio2SourceVoice*& rpVoice, common::crc_t audioCrc, bool bOneShot, bool b3d);
	void XM_CALLCONV Apply3d(IXAudio2SourceVoice* pIXAudio2SourceVoice, FXMVECTOR vecPosition, FXMVECTOR vecVelocity, float fVolume, float fPitch);

	bool LoadMusicVoice(std::unique_ptr<MusicStream>& rpStream, common::crc_t audioCrc);
	
	int64_t miNextId = 1;
	std::vector<Voice> mVoices;

	XMVECTOR mVecListenerPosition {};
	X3DAUDIO_LISTENER mX3dAudioListener
	{
		.OrientFront = {0.0f, 0.0f, -1.0f},
		.OrientTop = {0.0f, -1.0f, 0.0f},
		.Position = {0.0f, 0.0f, 0.0f},
		.Velocity = {0.0f, 0.0f, 0.0f},
	};

	// Music streaming
	bool FillStreamBuffer(MusicStream& rStream, uint8_t* pBuffer, size_t bufferSize, size_t& rBytesRead, bool& rbLastBuffer);
	float GetMusicRemainingTime(const MusicStream& rStream) const;
	void UpdateCrossFade(float fDeltaTime);

	mutable std::mutex mMusicStreamMutex;
	int64_t miMusicIndex = 0;
	std::unique_ptr<MusicStream> mpCurrentMusicStream;
	std::unique_ptr<MusicStream> mpNextMusicStream;
	CrossFadeState mCrossFadeState = CrossFadeState::kNone;
	float mfCrossFadeProgress = 0.0f;
};

inline AudioManager* gpAudioManager = nullptr;

} // namespace engine
