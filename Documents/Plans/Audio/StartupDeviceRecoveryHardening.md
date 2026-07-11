# Startup Audio Device Recovery Hardening

## Context

`AudioManager` has two startup-only device gaps:

- `InitializeAudioSubsystems` calls the pinned-format `AudioEngine::Reset`, logs and continues when it returns `false`, then unconditionally dereferences `GetInterface()`. DirectXTK enters silent mode for `ERROR_NOT_FOUND` / `AUDCLNT_E_DEVICE_IN_USE`, so `GetInterface()` is null and startup can access-violate if the endpoint disappears or becomes busy during rate pinning.
- `InitializeAudioEndpoint` can finish with `mpAudioEngine == nullptr` when no active render endpoint exists. `AudioManager::Update` can reset only an existing engine, so attaching a device later cannot recover audio without restarting the process.

DirectXTK already supplies the needed recovery primitive: without `AudioEngine_ThrowOnNoAudioHW`, its constructor returns a stable silent-mode `AudioEngine` for a missing or busy default device, and `Reset` can later create the live graph. `StaticVoices` and `StreamingVoices` retain raw pointers to the engine, so keeping that object stable is the narrow ownership-preserving design.

## Design

1. Make construction always produce one stable `AudioEngine`. Preserve default-then-first-active endpoint selection when a device exists; when enumeration finds none, construct against the OS default (`deviceId == nullptr`) and retain DirectXTK's silent-mode shell. Never replace the engine after voice subsystems receive its pointer.
2. Separate one-time ownership attachment from live-graph configuration. Initialize `StaticVoices` / `StreamingVoices` and register `AudioManager` notifications exactly once against the stable engine even when silent. Preserve startup ordering that avoids notifications from the initial pin attempt; a silent startup registers after establishing there is no live graph.
3. Make live-graph setup report success. Build `mPinnedOutputFormat` only from a live output with nonzero native channels, attempt the existing 48 kHz pin, and on `false` or exception use the device-default fallback. Never dereference `GetInterface`, cache mastering channels, or emit live-format diagnostics unless the final graph is present.
4. Extend `Update` recovery for an initially silent engine. If no valid pinned format exists, reset to the default format to discover native channels, derive the pinned format, then pin; because a failed pin tears down that graph, restore the default-format graph as final fallback. With a valid pinned format, retain pin-first/fallback behavior.
5. Consume synchronous `OnReset` clear requests only after the final reset attempt, perform one paired clear, and request exactly one next playlist track. Keep suspend/resume gating and teardown ordering unchanged.
6. Update `Engine/Source/Audio/AGENTS.md` with the stable-engine/silent-start contract and pinned-format validity requirement.

## Critical files

- `Engine/Source/Audio/AudioManager.h` — stable-engine and pinned-format-valid state/helper surface.
- `Engine/Source/Audio/AudioManager.cpp` — endpoint creation, subsystem initialization, constructor ordering, and `Update` recovery.
- `Engine/Source/Audio/AGENTS.md` — startup silent-mode and recovery invariants.
- `ThirdParty/DirectXTK/Audio/AudioEngine.cpp` — read-only API evidence; do not modify.

## Out of scope

- Replacing DirectXTK/miniaudio, endpoint preference UI, or persisted endpoint choices.
- Preserving interrupted-track CRC/playback position; recovery continues with the callback's next track.
- General mixing/voice refactors or suspend/resume behavior changes.
- Server audio, simulation/CRC state, network wire formats, saves/replays, `.pack` layout, or `kiVersion`.
- Permanent test-only APIs or unit tests.

## Acceptance criteria

- Normal startup preserves endpoint selection, the 48 kHz mastering policy, channel caching, and music/SFX behavior.
- A pinned-format startup `Reset` returning `false` leaves the client silent or on the device-default fallback without dereferencing a null `IXAudio2`.
- Startup with no active endpoint retains a silent engine; attaching/enabling an endpoint later recovers without restart and initializes the next playlist track exactly once.
- A failed pin after default recovery restores a usable default graph instead of leaving DirectXTK silent.
- Two suspend/resume cycles still produce exactly one stream initialization per recovery and no audio errors.
- Client build passes. Live verification covers missing-at-startup and pin-failure using real/OS-disabled hardware or temporary non-landing fault injection, with agent-harness logs as evidence.

## Notes

- **Invariant exposure:** client-only presentation state. No PostRender CRC data, deterministic RNG order, wire protocol, save/replay schema, `.pack` bytes, or server guard scope changes. Retain allocation suppression and allocation-free logging in the audio update.
- Ownership is resolved: keep one stable `AudioEngine` and recover it with `Reset`; recreation would invalidate raw engine pointers held by both voice subsystems.
- Grill decision: choose the available verification route (real/OS-disabled endpoint preferred; otherwise temporary fault injection removed before landing). No production injection seam is proposed.
