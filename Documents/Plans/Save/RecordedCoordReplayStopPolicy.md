# Recorded Coordinate Replay Stop Policy

## Context

`GameSaveLoad::SyncReplayTick` snapshots one writer per active coordinate at recording start (`Projects/BrokenEngineSandbox/Source/Save/GameSaveLoad.cpp:298-316`). Recording does not freeze that set: `ServerSession::PrepareTick` recomputes it and `SyncActiveFrames` erases `mCoordFrames` entries outside it (`Projects/BrokenEngineSandbox/Source/Network/Server/ServerSession.cpp:44-67,401-417`). Update skips an absent coord's writer (`GameSaveLoad.cpp:378-388`), but stop calls `CurrentFrame(rCoord)` for every retained writer (`:349-353`); that accessor uses `mCoordFrames.at(coord)` and throws after eviction.

Step-10 code audit F001 accepted this as structural. The throw skips that writer, all later writers, `mReplayWriters.clear()`, metadata, and aggregate logging. The toggle is already cleared while the writer map remains populated, so recording can stay stuck. Outside that unchecked lookup, current `SyncReplayTick` folds returned writer and metadata outcomes without boolean short-circuiting, clears writer state, and emits one aggregate result; preserving that persistence contract requires an explicit recorded-coordinate end-state policy, not catch-and-skip.

## Design

**Decision plan (present options).** Grill one end-frame ownership policy:

- **A — eviction handoff (recommended):** before `SyncActiveFrames` erases a recorded coord, transfer its last complete current Frame (or replay-only equivalent) to `GameSaveLoad`. The coord leaves active simulation; stop uses the retained end Frame.
- **B — writer-owned rolling end snapshot:** retain a serializable end Frame as writer updates arrive. Stop is lifetime-independent, but a full-frame copy/serialization each recorded tick may be too costly.
- **C — pin recorded `CoordFrames` until stop:** exclude them from erasure. This is simple but conflates active membership with replay retention; accept only if all consumers are proven active-set gated.

Stop must resolve end state without unchecked `CurrentFrame(rCoord)`. Missing state marks that writer failed, cleans partial siblings as required, and continues. Writer clearing, metadata attempt, and one aggregate result remain unconditional. Never continue simulating an evicted coord solely for persistence.

## Critical files

- `Projects/BrokenEngineSandbox/Source/Save/GameSaveLoad.cpp` / `.h` — writer ownership/update/stop/recording state.
- `Projects/BrokenEngineSandbox/Source/Network/Server/ServerSession.cpp` / `.h` — `SyncActiveFrames` boundary for option A/C.
- `Engine/Source/File/DifferenceStream.h` — only if option B or writer-level endpoint retention is selected.
- `Engine/Source/GameBase.h` — keep `CurrentFrame`'s active-map precondition; do not weaken it globally.
- Relevant project/network AGENTS.md files — recorded-coordinate lifetime and active-set policy.

## Out of scope

- Coordinate activation/subscription changes or gameplay retention.
- Set commit/invalidation, owned by `Save/ReplayGenerationCommitAtomicity.md`.
- Replay redesign, slots, backward compatibility, CRC, phases, or protocol.

## Acceptance criteria

- Record multiple coords, evict one from `mCoordFrames`, then stop: every writer and metadata path is attempted, writers clear, `IsRecording()` is false, and one aggregate result appears without an escaping exception.
- The selected policy supplies the evicted coord's last complete end Frame without simulating it after active-set removal.
- Missing end state reports writer failure and cleans partial output while later writers, clearing, metadata, and aggregate logging still execute.
- A successful recording containing an evicted coord has complete siblings and loads/plays normally.
- No-replay active eviction, deterministic tick/CRC results, and ownership remain unchanged.

## Notes

Server-only, high frame-lifetime exposure. No valid replay byte/version change intended. Option B touches generic writer/allocation-tracked tick work; measure any full-frame per-tick cost before approval. Build both targets and use `/agent-harness` for multi-coord recording, eviction, stop, artifact/log/status checks, and playback. This plan must land before `ReplayGenerationCommitAtomicity`, which relies on its end-frame lifetime policy while retaining separate set-commit scope.
