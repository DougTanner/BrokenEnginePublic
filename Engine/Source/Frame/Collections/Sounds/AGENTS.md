# /Engine/Source/Frame/Collections/Sounds/

Client-only 3D spatial audio source data. Pure data carrier — no playback, no GPU pipeline here. Game parents own lifetime via `sound_t` handles (hub owner-driven Sync pattern). `StaticVoices` (owned by `AudioManager`) reads the SOA arrays each frame to drive XAudio2 voice priority, activation, and 3D spatialization. See `../../../Audio/AGENTS.md`.

## Unique Aspects

- Looping voices only: tracked entries here are always looping. Fire-and-forget cues use `AudioManager::PlayOneShot3d` instead of adding a tracked slot.
- Position forced W=1 in `Sync`: explicitly set rather than trusting the caller, so listener-distance math gets point semantics (the "XMVECTOR W invariant").
- Dedicated UUID stream: spawns give the generator-based insertion helper a callable that invokes `GenerateSoundUuid()` exactly once; the separate counter keeps client-side sound churn from perturbing determinism.
