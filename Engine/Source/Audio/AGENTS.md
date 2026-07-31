# Audio - XAudio2 Presentation

Client-only 3D and streaming audio through DirectXTK `AudioEngine`. `AudioManager` is exposed as `gpAudioManager`; static voices own spatial one-shots and persistent sounds, while streaming voices own music playback and crossfades.

## Runtime Contracts

- The mastering graph is pinned to 48 kHz to match DataPacker audio. Preserve that rate through device resets; Windows may convert only at the final shared-mode output.
- Construct one stable `AudioEngine` and attach voice subsystems to it once. Static and streaming voices retain its raw pointer, so recover a silent or lost device by resetting the existing engine rather than replacing it.
- Treat endpoint enumeration, graph reset, and XAudio2 results as trust boundaries. A missing device leaves a usable silent engine; recovery must clear stale source voices before normal playback resumes.
- `AudioManager::Update` runs post-render on the main thread and owns XAudio2 pumping and buffer submission. The streaming fill worker produces buffer data, the main thread consumes it, and XAudio2 buffer callbacks publish only atomic completion. AudioEngine reset/error notifications are delivered on the caller thread.
- Wait for the fill worker before mutating streaming containers. Destroy faded streams after releasing the streaming mutex so source-voice teardown cannot deadlock callback completion. `Clear` is the deliberate exception: it destroys while still holding that mutex, which is safe only because the XAudio2 buffer-completion callback is a bare atomic increment that never takes the mutex. Keep that callback lock-free.
- Suspend stops XAudio2 processing before clearing voices. Teardown distinguishes voices already destroyed by device loss from voices still owned by the engine.

## Simulation Boundary

- Audio is presentation-only. One-shot emission and persistent-voice lifecycle skip reconciliation replay and never enter deterministic Frame state or CRCs.
- Fades and crossfades use wall-clock time. Pitch variation uses a time-seeded audio-only random stream.
- Static voice capacity is scarce: prioritize by audible attenuation, cull inaudible one-shots, and use hysteresis plus fades for persistent voice eviction and reacquisition. Apply final 3D mix before playback and route near-unity pitch ratios through `SnapFrequencyRatio` to avoid unnecessary resampling.
- Camera-eye distance drives manual fade while the world-plane look-at point drives pan and Doppler. Keep these listener roles separate when changing spatialization.

## Asset and Thread Ownership

- Static sounds use resident chunks; music streams random-access ranges from lazy chunks without loading the full track.
- Source data and the mastering graph share the 48 kHz contract with DataPacker audio repair (`../../../DataPacker/Source/ExportJobs/AGENTS.md`).
- Audio calls follow the parent main-loop allocation and affinity rules.

## See Also

- File (`../File/AGENTS.md`) - Lazy chunk ownership
