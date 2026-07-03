#pragma once

#if defined(BT_CLIENT)

namespace engine
{

inline constexpr float VolumeToPower(float fMasterVolume, float fSoundVolume, float fLocalVolume = 1.0f)
{
	// More natural-feeling volume controls
	float fVolume = fMasterVolume * fSoundVolume * fLocalVolume;
	return fVolume * fVolume;
}

// XAudio2 bypasses its per-voice sample-rate converter only when the frequency ratio is exactly 1.0
// (and source rate == mastering rate — both pinned to 48 kHz). Slow-Doppler and non-randomized-pitch
// voices compute ratios like 0.9997 that force the resampler for no audible reason; snap those to 1.0.
inline constexpr float kfFrequencyRatioSnapEpsilon = 0.003f; // ~5 cents, inaudible

inline constexpr float SnapFrequencyRatio(float fRatio)
{
	return (fRatio > 1.0f - kfFrequencyRatioSnapEpsilon && fRatio < 1.0f + kfFrequencyRatioSnapEpsilon) ? 1.0f : fRatio;
}

inline void DestroyXAudio2SourceVoice(AudioEngine* pAudioEngine, IXAudio2SourceVoice*& rpVoice)
{
	if (rpVoice != nullptr)
	{
		rpVoice->Stop(0, XAUDIO2_COMMIT_NOW);
		rpVoice->FlushSourceBuffers();
		if (pAudioEngine != nullptr)
		{
			std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
			pAudioEngine->DestroyVoice(rpVoice);
			std::chrono::steady_clock::duration elapsed = std::chrono::steady_clock::now() - start;
			if (elapsed > std::chrono::milliseconds(100))
			{
				LOG(kAudio, kWarning, "DestroyXAudio2SourceVoice took {}ms", std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count());
			}
		}
		rpVoice = nullptr;
	}
}

} // namespace engine

#endif // BT_CLIENT
