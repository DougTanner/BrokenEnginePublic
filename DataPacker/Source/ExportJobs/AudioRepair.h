#pragma once

namespace audiorepair
{

// Pack-time output sample rate. Every source .wav is resampled to this so source rate == mastering
// rate holds at runtime, letting XAudio2 bypass its per-voice SRC. The engine pins its mastering
// voice to the same value — see kiMasteringSampleRate in Engine/Source/Audio/AudioManager.cpp
// (kept in lockstep). Changing this requires an ExportAudio::GetVersion bump to force re-export.
inline constexpr int64_t kiAudioExportSampleRate = 48000;

// Policy toggles. Music/ assets are loudness-mastered — flat-topping there is intentional limiting,
// so declip defaults off for them. Flipping either toggle requires an ExportAudio::GetVersion bump
// to force re-export.
inline constexpr bool kbDeclipEnabled = true;
inline constexpr bool kbDeclipMusic = false;

// Analyzes and repairs interleaved float samples in place. One LOG(kWarning) line per fix applied,
// prefixed with relativeFile; silent when the file is clean.
// bLoopAsset: whole-buffer loop — skip edge fades, validate seam continuity instead.
// bAllowDeclip: reconstruct short clipped runs (else clipping is warn-only).
// bAllowEdgeFades: fade non-zero onsets/tails (music passes false — the runtime crossfade always
// ramps music in from zero and transitions out before track end, so edge defects are warn-only).
void RepairAudio(std::vector<float>& rfSamples, int64_t iChannels, int64_t iSamplesPerSec, std::string_view relativeFile, bool bLoopAsset, bool bAllowDeclip, bool bAllowEdgeFades);

// Resamples the interleaved float buffer in place from iSourceRate to iTargetRate (per-channel over
// the interleaved layout; iChannels asserted <= 2 by the caller). Offline Kaiser-windowed sinc,
// ~32 taps per output sample — quality over speed. No-op when the rates already match; otherwise
// logs one kInfo line. Duration is preserved (pitch/length unchanged, rate conversion only).
void Resample(std::vector<float>& rfSamples, int64_t iChannels, int64_t iSourceRate, int64_t iTargetRate, std::string_view relativeFile);

} // namespace audiorepair
