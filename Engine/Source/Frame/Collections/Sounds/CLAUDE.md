# /Engine/Source/Frame/Collections/Sounds/

Client-only 3D spatial audio source data. Pure data carrier — no playback, no GPU pipeline here. `StaticVoices` (owned by `AudioManager`) reads the SOA arrays each frame to drive XAudio2 voice priority, activation, and 3D spatialization. See [Audio/CLAUDE.md](../../../Audio/CLAUDE.md).

## Unique Aspects

- **Looping voices only**: tracked entries here are always looping. Fire-and-forget cues use `AudioManager::PlayOneShot3d` instead of adding a tracked slot.
- **Position W=1 in `Sync`**: stored as points, not directions — load-bearing for downstream spatialization. (Overrides the default direction/offset W=0; see root [CLAUDE.md](../../../../../CLAUDE.md) "XMVECTOR W invariant".)
- **Dedicated UUID stream**: spawns mint IDs from `GenerateSoundUuid()`, a separate counter from the shared entity sequence, so client-side sound churn never perturbs determinism.
