#pragma once

#if defined(BT_CLIENT)

namespace DirectX
{
class AudioEngine;
}

namespace engine
{

class StreamingVoice;
struct LazyChunk;

class StreamingVoices
{
public:

	void Init(AudioEngine* pAudioEngine);

	void Play(common::crc_t uiAudioCrc);
	void SetNextTrackCallback(std::function<common::crc_t()> callback);

	void CheckTrackTransition();
	void Update(float fDeltaTime);
	void Clear(bool bNullVoicesBeforeDestroy);

	int64_t GetStreamCount() const;

private:

	void SubmitBuffers(StreamingVoice& rStream);
	void CreateStream(common::crc_t uiAudioCrc);
	void TransitionCurrentToPrevious();

	AudioEngine* mpAudioEngine = nullptr;

	std::mutex mMutex;
	std::unique_ptr<StreamingVoice> mpCurrentStream;
	std::vector<std::unique_ptr<StreamingVoice>> mPreviousStreams;
	std::vector<std::unique_ptr<StreamingVoice>> mStreamsToDestroy;
	std::function<common::crc_t()> mGetNextTrack;
};

} // namespace engine

#endif // BT_CLIENT
