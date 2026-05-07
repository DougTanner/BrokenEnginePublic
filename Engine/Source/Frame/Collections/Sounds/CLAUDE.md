# /Engine/Source/Frame/Collections/Sounds/

Client-only 3D spatial audio source data. Pure data carrier — no playback here; `gpAudioManager` reads the SOA arrays.

## Unique Aspects

- **Owner-driven lifecycle**: All phase functions are no-ops. Owners call `Add`/`Remove` and push state each frame via `Sync`; `Transfer` is empty because owners handle cross-coord transfer.
- **Looping voices only**: tracked voices in this collection are always looping (XAUDIO2_LOOP_INFINITE). Fire-and-forget cues should use `gpAudioManager->PlayOneShot3d` instead of adding a tracked slot here.
- **Visual UUID counter**: uses `GenerateSoundUuid()` so client-side sound churn never perturbs the shared entity UUID sequence.
- **No TypeRegistry registration**: `Register()` is empty despite a `crc` field — sounds are never rendered, so no pre-blur texture hookup.
- **Position W=1 in Sync**: stored as points, not directions — load-bearing for spatialization math downstream. See root [CLAUDE.md](../../../../../CLAUDE.md) Key Patterns "XMVECTOR W invariant".
