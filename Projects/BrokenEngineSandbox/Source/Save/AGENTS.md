# Save - Server Persistence and Replay

## Overview

`GameSaveLoad` owns server save/load/reset and deterministic replay. Files are versioned by `game::Frame::kiVersion`; navigation and elevation are derived and are not persisted.

## Save and Load

- Grid saves use atomic replacement and write coord frames in sorted key order. Return the actual commit result to callers.
- Deserialization is a trust boundary. Validate version headers, counts, capacities, IDs, and stream completion before applying the loaded state.
- A failed or truncated read clears partial frame/fleet state so callers can rebuild a fresh game. Apply counters and clocks only after full validation.
- Successful loads preserve the saved simulation clock and resynchronize clients to that state. Fresh-game paths reset server fleet management explicitly.
- Server load/reset and replay-load entry cancel pending raw profile samples and unpublished event arms before mutating or restoring frame state; this diagnostic cleanup does not alter the persisted payload or replay stream.

## Replay

- The replay manifest is the marker that commits one replay generation — a single recording instance, named in code by `kReplayManifestGenerationDomain` and `ComputeReplayGenerationDigest` — plus the checksummed list of that generation's files. Recording start invalidates it before replacing any component; recording stop publishes it only after writers and metadata succeed. Playback validates its authoritative component identities, byte counts, SHA-256 digests, and replay generation root before reading components or applying staged state.
- Writer state persists across frames. A writer's first coord eviction is terminal: retain that coord's last complete frame and never resume the writer. Missing retained terminal state invalidates that coord's replay files without skipping attempts for other writers or metadata.
- Playback readers retire independently at recorded endpoints. Loop only after the last reader retires, and stop the current fixed-tick iteration before loading the next loop.
- Successfully applying replay state starts a fresh fixed-step wall-clock interval. Replay validation and file I/O are outside simulation time; never carry their elapsed time or accumulated tick debt into the restored loop, or a batch of extra ticks can break replay CRCs.
- Replay compares recorded checksums with resimulation each tick. Transfer harvest remains disabled during playback so the deterministic stream matches recording.
- Replay coord lists are deterministic and manifest counts are bounded before allocation.

## Affinity

The subsystem is whole-file `BT_SERVER`. Replay operations are available only when `kbDebugInput` enables their tick-time implementation.

## See Also

- `../../../../Engine/Source/File/AGENTS.md`
- Frame serialization: `../Frame/AGENTS.md`
