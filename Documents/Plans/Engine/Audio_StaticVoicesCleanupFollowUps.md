# Audio: StaticVoices Cleanup Follow-ups

## Context

The session that landed the zero-volume one-shot cull and removed the `BT_AUDIO_PRIORITY_CULL` feature gate (now permanent priority-cull behavior) surfaced three small follow-ups. None blocks correctness — all are debt cleanup against `Engine/Source/Audio/StaticVoices.{h,cpp}`. Bundled into one plan because they touch the same file and naturally land together in a single editing pass.

## Design

Three distinct items, executed in this order:

### 1. Investigate and retire `mbSkipNextInvalidation` if unused

`mbSkipNextInvalidation` (member at `StaticVoices.h:~80`) and the public `SkipNextInvalidation()` setter (header) are consumed once at the top of `StaticVoices::UpdateLifecycle` in the invalidation pass. Comment says it suppresses fade-out invalidation of stale voices on the first real post-storm tick after a device-reset callback raises the flag.

Now that priority-cull is unconditional, voices auto-deactivate via the priority pass when they drop below the hysteresis floor. The audit hypothesis: the device-reset path may already drain stale voices via `Clear()` (called from the device-reset callback), making the skip-invalidation flag redundant.

Action: grep for callers of `SkipNextInvalidation()` (likely `AudioManager` device-reset / `Suspend` / `Resume` paths). For each caller, verify whether the new priority-cull-driven deactivation already covers the case the flag was protecting. If yes: delete the flag, the setter, and the consumption site in `UpdateLifecycle`. If no: keep, but tighten the comment to explain why priority-cull doesn't subsume it.

### 2. Resolve neutralized audio tuning constants

Two `static constexpr float` constants live as multiply-by-zero / multiply-by-one tuning kludges in `Apply3dVolume` and `ComputeAttenuatedVolume`:

- `kfMaxChannelBleedFactor = 0.0f` (`StaticVoices.cpp:~589` channel-bleed block) — when 0, the entire `pfMatrixCoefficients` mix below it is a no-op (math computes to original L/R).
- `kfMinHeightVolumeScale = 1.0f` (`StaticVoices.cpp:~570, 587, 604`) — when 1, the `std::lerp(1.0f, kfMinHeightVolumeScale, mfChannelBleedT)` collapses to `1.0` regardless of `mfChannelBleedT`, making the entire altitude-bleed scaffolding a no-op.

`Engine/Source/Audio/CLAUDE.md` documents these as "kludge structure kept so playtest can re-tune if pan still feels off." Either:

- (a) Promote to `engine::Wrapper` globals exposed in the Sound Tweaks tab, so live re-tune is possible without recompile, OR
- (b) Commit to the neutralized values and delete the wrapping branches outright (the channel-bleed block, the lerp, the `mfChannelBleedT` field if no other consumer remains).

Recommend (b) as default — the kludges have been neutralized for some time without playtest re-tuning. If pan ever needs re-tuning, the code-archaeology cost of resurrecting the formula from git is lower than the ongoing cost of carrying dead-weight tuning knobs.

### 3. Optionally split `StaticVoices.cpp` at the lifecycle/submission seam

`StaticVoices.cpp` sits at ~595 lines (post-cleanup) — inside the optional `/reduce-file` band (500-1000). Two natural cohesive units:

- **Submission + pool + init** (~180 lines): `Init`, `Clear`, `ClearPool`, `Set3dSettings`, `ReturnVoiceToPool`, `AcquireVoiceFromPool`, `PlayOneShot`, `PlayOneShot3d`. All "outside callers ask us to play sounds" surface.
- **Per-frame priority/cull/mix pipeline** (~340 lines): `UpdateLifecycle`, `UpdateListenerPosition`, `UpdateVolumes`, `ComputeAttenuatedVolume`, `Apply3dVolume`. All "every frame, run the audio mixer" internals.

Proposed split: `StaticVoices.cpp` (per-frame pipeline) + `StaticVoicesOneShot.cpp` (submission + pool + lifecycle init/clear). Members and class definition stay in one `StaticVoices.h` — only implementation files split. Vcxproj filter must add the new `.cpp` under `Engine\Audio` (client-only, matches the `BT_CLIENT` guard already on the existing file).

Optional — execute only if the file has grown further by next pickup or readability complaints surface. Skip if the file remains at this size and the team has no friction with it.

## Out of scope

- Restructuring the `StaticVoice` class itself (the per-voice value type at `StaticVoice.h`) — orthogonal.
- Changing the priority-cull algorithm (closest-loudest-wins, hysteresis band) — landed and stable.
- Adjusting `mfManualFadeVolume` default (`0.15` set at `Game.cpp:58`) or the relative-floor formula in `Apply3dVolume` — landed and stable.
- Refactoring the `StreamingVoices` subsystem — covered by `Engine/Architecture_StreamingVoiceFileIoOffMainThread.md`.
- Pre-existing Hungarian-prefix inconsistencies (`audioCrc` vs `uiAudioCrc`, `sound_t id` vs `sound_t uiId`) flagged by code-style review — pre-date this session, belong in a wider naming sweep if any, not here.

## Acceptance criteria

- Item 1: either `SkipNextInvalidation` is deleted across header + cpp + every caller, OR its comment cites the specific scenario priority-cull doesn't cover (with a `LOG(kAudio, kDebug, …)` confirming it fires in that scenario, optional).
- Item 2: `kfMaxChannelBleedFactor` and `kfMinHeightVolumeScale` are either replaced by Wrappers (item 2a) or both constants and their wrapping math are deleted (item 2b). `mfChannelBleedT` field deleted if item 2b lands and no other consumer references it. `Engine/Source/Audio/CLAUDE.md` updated to reflect chosen path.
- Item 3 (if executed): `StaticVoices.cpp` lands at <300 lines and `StaticVoicesOneShot.cpp` lands at <250 lines; vcxproj filter updated; client builds clean.

## Critical files

- `Engine/Source/Audio/StaticVoices.h` — the `mbSkipNextInvalidation` member + `SkipNextInvalidation()` setter; class definition shared by both halves of any split.
- `Engine/Source/Audio/StaticVoices.cpp` — implementation; all three items touch it.
- `Engine/Source/Audio/AudioManager.cpp` — likely caller of `SkipNextInvalidation()`; must be re-read during item 1.
- `Engine/Source/Audio/CLAUDE.md` — documents the kludge constants; must be updated by item 2.
- `Projects/BrokenEngineSandbox/Platforms/VisualStudio2026/BrokenEngineSandbox.vcxproj` (+ `.filters`) — only modified by item 3 (the file split), and only the client vcxproj since the file is `BT_CLIENT`-only.

## Notes

Items are ordered by independence — item 1 can land alone; item 2 can land alone; item 3 is best as a separate session because the file move makes the diff for items 1 and 2 harder to review.
