# `/Engine/Source/Audio/`

3D spatial audio system using DirectXTK AudioEngine (XAudio2 wrapper). Entirely client-only (`#if defined(BT_CLIENT)`).

**Global**: `gpAudioManager`

## Key Classes

- **AudioManager** - Orchestrator: XAudio2 device lifecycle, focus handling (`Suspend`/`Resume`), and the `IVoiceNotify` interface for device reset. Delegates all playback to `mStaticVoices` and `mStreamingVoices`; public API is unchanged from callers' perspective.
- **StaticVoices** - 3D spatial audio manager: one-shot fire-and-forget playback and persistent frame-driven voices. Voices are capped, synced from frame sound data each tick, and faded out on removal. X3DAudio provides distance attenuation, Doppler, and speaker panning. One-shot calls are thread-safe via a recursive mutex. Uses XAudio2 voice pooling: removed voices are stopped and pooled by CRC, then reused when the same sound is needed again, avoiding costly `DestroyVoice`/`CreateSourceVoice` round-trips. Supports a skip-invalidation flag to suppress transient voice churn during reconciliation replay.
- **StreamingVoices** - Music streaming manager: triple-buffered playback with crossfade transitions. Buffer submission stays on the main thread; XAudio2 callbacks only increment an atomic counter. Destruction is deferred past mutex release to avoid `DestroyVoice` deadlocks.
- **StaticVoice** / **StreamingVoice** - Per-voice data structs owned by their respective managers. `StaticVoice` stores its audio CRC; its destructor and move-assignment assert `mpVoice == nullptr` since `StaticVoices` manages the XAudio2 voice lifecycle (pooling or destruction).

## Architecture Notes

- `DestroyXAudio2SourceVoice` (free function in `AudioManager.h`) handles safe teardown and logs a warning if `DestroyVoice` takes unexpectedly long.
- Callback-based playlist decoupling: game logic supplies the next-track callback; `StreamingVoices` drives transitions when remaining time crosses a threshold.
- Silent failure points (voice cap reached, stream errors) emit `Log(kAudio, ...)`. `kAudio` is off by default — enable it when debugging audio.
- XAudio2 callbacks are minimal (atomic increment only); all I/O and buffer submission happen on the main thread.
