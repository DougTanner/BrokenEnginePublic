# `/Engine/Source/Audio/`

3D spatial audio via DirectXTK `AudioEngine` (XAudio2 wrapper). Global: `gpAudioManager`. Client-only (see hub for the `BT_CLIENT` convention).

## Key Classes

- **AudioManager** - XAudio2 device lifecycle, focus/suspend, device-reset callbacks. Constructor enumerates render endpoints to bind the OS default (falls back to first active), enables the mastering limiter, and caches the mastering-voice channel count for the 3D mix. Delegates to the static and streaming voice subsystems. `Update` refreshes the listener position before driving voice lifecycle so the priority pass sees current-frame listener and fade state.
- **StaticVoices** - 3D spatial one-shots + frame-driven looping/persistent voices. Hard-capped pool of XAudio2 source voices keyed by `crc` (retired voices are recycled, not destroyed). Recursive mutex (3D path re-enters 2D path). 3D sources asserted mono; the device-not-ready path requests a chunk load and bails for that frame.
- **StreamingVoices** - Music streaming. Two layered mechanisms: a per-stream triple-buffer ring (worker fills slots, main thread submits them) and a stream-level crossfade — `Play` and `CheckTrackTransition` move the current stream onto a fading-out previous-streams list and start the new one, the latter auto-advancing via a next-track callback for gapless playlists. File I/O runs on a dedicated `common::PersistentWorker` (`kThreadStreamingVoiceFill`); a per-slot atomic state machine publishes worker-filled slots to the main thread, whose `Update` is consumer-only. XAudio2's callback thread only bumps an atomic consumed-counter. Strict alternation: every public method calls `mFillWorker.Wait()` at entry; only `Update` calls `Wake(...)` at exit. Voice `Start()` is deferred to first buffer submission.

## Frame-Phase Invariants

- `AudioManager::Update` runs post-render on the main thread and owns all XAudio2 pumps and buffer submission.
- One-shot emit and static voice lifecycle both assert post-render phase and early-out during reconciliation replay — replay ticks must not mutate audio state in either direction.
- Static voice lifecycle reads post-render sound ids against the interpolation SOA; listener position comes from the client player row.

## Voice Lifetime

- Two teardown modes: XAudio2-already-destroyed (post-reset/device-loss — just null pointers) vs. we-still-own-the-voices (explicit `DestroyVoice` with >100ms teardown warning).
- `Suspend` stops the XAudio2 processing thread *first* so `DestroyVoice` returns instantly. `Resume` lets normal flow recreate voices on demand.
- Faded-out streaming voices are parked in a deferred-destruction list drained *after* the mutex releases — `DestroyVoice` must wait for `OnBufferEnd` without deadlocking the callback.

## Volume & 3D

- Combined volume squared for a perceptual curve; sfx and music use separate global sliders.
- 3D mix layers X3DAudio (matrix / Doppler / LPF) with a manual piecewise distance fade as a hard override past the physical attenuation floor. The natural attenuation curve drives the priority pass; the audible-floor only clamps the final mix after prioritization, so it never makes a distant sound look loud to the prioritizer.
- Two listener points, decoupled by purpose. `mVecListenerPosition` (camera eye, full XYZ) drives the manual fade distance — altitude inflates 3D listener-to-emitter distance so high zoom pulls ground emitters into the fade band. `mX3dAudioListener.Position` (camera look-at on the world plane at `gBaseHeight`) drives X3DAudio pan/Doppler — pinning it to the world plane keeps the listener-to-ground-emitter vector XY-only, so pan azimuth reflects 'left-of-screen → left-ear' instead of being collapsed near-center by altitude. Listener velocity is zero; Doppler still applies from emitter motion.
- Fade band shape (start/end distance, X3DAudio curve scaler, audible-floor multiplier) is camera-height-lerped per the canonical "Camera-Height-Conditional Uniforms" pattern (see `Engine/Source/Graphics/Render/CLAUDE.md`). Each quantity is a height-lerp wrapper quartet in `Engine/Source/Ui/SoundSettingsWrappersBase.cpp`, exposed in the **Sound > Tweaks** sub-tab; `UpdateListenerPosition` resolves them against eye height each frame. The audible floor is **relative** (multiplier × the sound's own volume), so a full-voice sound floors at a faint level past fade-end while a zero-volume voice stays silent regardless of distance — required because per-category volume wrappers default to 0.
- Audio reads `game::gpCamera` directly for eye position / height — established cross-layer pattern in `Engine/Source/`, not a `game::gpGame` violation.

## Voice Prioritization

- Static voice slots are scarce; allocation is closest/loudest-wins, not first-come. Per-frame priority pass scores entries by attenuated volume and reclaims XAudio2 voices from the lowest-scoring sounds when over budget.
- One-shots are hard-culled at submission when their attenuated volume falls below the cull threshold — they never enter the pool.
- Looping/persistent entries out of audible range release their XAudio2 voice but keep the bookkeeping entry (inactive state); they re-acquire a voice and fade in via the existing fade-volume on re-entry. Activation/deactivation use a hysteresis band around the cull threshold to prevent flicker at the boundary.
- Pre-emption (budget eviction and range-fall-out) does not instant-stop. The displaced voice enters a fade-out state and ramps down over the sound's configured fade-out time; on completion it returns to the per-CRC pool and the entry goes inactive. A bounded fade-out pool sits above the main voice cap so the freed slot can be handed to the incoming sound immediately while the outgoing one finishes its tail. Overflow `DEBUG_BREAK`s and the deactivation retries next frame.
- Re-entry mid-fade is click-free: if a fading-out entry becomes audible again, the fade is cancelled in place and the regular fade-in ramp resumes from the current volume — no restart, no zero-volume blip.
- Lifecycle is a fixed sequence of single-purpose passes per frame: invalidation marking, priority (handles both inactive and fading-out re-entry), deactivation marking, fade-out advance + retirement, fade-in ramp. Mark-only passes never mutate XAudio2 state directly; the dedicated advance pass owns the transitions.

## Device Reset

- Lost-device detection in `Update` calls `AudioEngine::Reset`, re-caches mastering voice channel count, then clears all voices.
- Device-reset callbacks (which run on the XAudio2 callback thread) defer both static-voice and streaming-voice clear via atomic flags consumed at the top of the next `Update`. Deferring keeps the fill worker (`Wake`/`Wait` are single-threaded by contract) driven exclusively from the main thread.
- `kAudio` log channel is off by default.
