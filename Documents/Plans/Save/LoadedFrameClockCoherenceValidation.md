# Loaded-Frame Clock Coherence Validation

## Context

The Tier-3 adversarial review of direct-load clock retention found a pre-existing trust-boundary gap that was explicitly outside that change's approved scope. `GameSaveLoad::ReadGrid` deserializes every saved coordinate frame independently (`Projects/BrokenEngineSandbox/Source/Save/GameSaveLoad.cpp:432-443`), validates the frame count, client-coordinate membership, and global-ID counter, then sets the global clock from `mCoordFrames.begin()` (`:453-456`) before the final stream-state gate (`:471-480`). It never proves that all loaded frames carry the same `FrameInterpolateBase::iTick` and `fCurrentTime`, that the selected tick is nonnegative and safe to advance once, or that the selected `fCurrentTime` is finite.

Repository-produced saves are coherent: `RunFrameTick` stamps every active frame from the same global tick/time (`Projects/BrokenEngineSandbox/Source/Frame/FrameTick.cpp:65-69`), `ServerSession::SyncActiveFrames` removes inactive frames before subsequent ticks (`Projects/BrokenEngineSandbox/Source/Network/Server/ServerSession.cpp:395-410`), and `WriteGrid` serializes the remaining current frames. A crafted or corrupt multi-cell save can nevertheless pass current validation with divergent clocks, an all-equal negative or maximum tick, or an all-equal non-finite time. Divergent clocks also let unordered-map iteration choose the server clock arbitrarily. Tick and time are serialized and included in the shared frame checksum (`Engine/Source/Frame/FrameBase.cpp:8-14,35-51`), while `GameBase::SetTickCounter` requires a nonnegative tick and the next server update increments it (`Engine/Source/GameBase.h:164`; `Engine/Source/GameBase.cpp:158-161`). Accepting any such clock violates the loaded-grid and next-tick invariants used by direct load, startup autoload, replay setup, and client resynchronization.

## Design

1. In `GameSaveLoad::ReadGrid`, capture the first deserialized frame's tick and current time as the candidate loaded clock. Reject the candidate with `common::CorruptStreamException` unless `0 <= iTick < std::numeric_limits<int64_t>::max()` and `fCurrentTime` is finite, so `SetTickCounter` accepts the tick and the next server update can increment it without signed overflow.
2. Validate every subsequently deserialized frame against that candidate before accepting the grid. Require exact integer-tick equality and bit-identical finite current-time values; float numeric equality alone is insufficient because signed-zero encodings serialize and checksum differently.
3. Keep clock adoption atomic with the existing all-or-nothing read boundary: retain the validated candidate in locals and call `SetTickCounter` / `SetCurrentTime` only after client-coordinate membership and `fileStream.good()` have passed. Any clock-coherence failure follows the existing catch path, which clears the partial grid and fleet state and lets each caller fall back to a fresh game.
4. Keep the current file shape and valid-save behavior. Do not normalize, repair, or choose among inconsistent clocks.

## Critical files

- `Projects/BrokenEngineSandbox/Source/Save/GameSaveLoad.cpp` — `GameSaveLoad::ReadGrid`, per-frame validation, failure cleanup, and final loaded-clock adoption.
- `Projects/BrokenEngineSandbox/Source/Frame/FrameTick.cpp` — `RunFrameTick`, read-only source of the single-clock production invariant.
- `Engine/Source/Frame/FrameBase.cpp` / `.h` — read-only serialization and shared-checksum ownership of `FrameInterpolateBase::iTick` / `fCurrentTime`.
- `Projects/BrokenEngineSandbox/Source/AGENTS.md` — confirm or refine the all-or-nothing save/load contract if the existing wording is no longer complete.

## Out of scope

- Save/replay format, `Frame::kiVersion`, `.pack` data, wire protocol, or frame/collection layout changes.
- Backward compatibility or salvage of corrupt/internally inconsistent saves.
- General range or finite validation for other deserialized frame, fleet, or static-data fields.
- Changes to direct-load clock retention, startup-autoload policy, replay semantics, client clock correction, active-set pruning, or `Game::Reset`.
- Unit tests.

## Acceptance criteria

- Server target compiles with no compile or link errors.
- A normal multi-cell save created through the agent harness loads successfully; all loaded frames remain queryable, the saved tick is retained, and the first subsequent update observes saved tick + 1.
- Starting from that valid save, a temporary fixture whose second serialized frame carries a different tick is rejected through the harness `load` command (`resetToFresh: true`); the server exposes only fresh fallback state, not a partially loaded grid or the fixture's clock.
- Two separate tick-domain fixtures whose serialized frame ticks are all changed identically to `-1` and `INT64_MAX`, respectively, are each rejected with the same clean-fallback observation. This proves that an internally coherent candidate must still be nonnegative and advanceable.
- A separate time-domain fixture whose candidate and every serialized frame current-time value are all changed identically to the same NaN or infinity encoding is rejected with the same clean-fallback observation. This fixture is independent of the distinct second-frame tick-mismatch fixture, so rejection proves candidate finiteness rather than only cross-frame identity validation.
- Construct the corrupt fixtures by patching only the repeated serialized `iTick` / `fCurrentTime` clock bytes in copies of the harness-produced save and verify each expected multi-frame clock sequence before loading.
- Static inspection confirms every successful `ReadGrid` entry point receives the same validated clock, while every failure returns before global clock adoption.
- Final diff contains no serialization order/width, version, protocol, layout, or checksum-algorithm change; known-good save and replay bytes remain accepted unchanged.

## Notes

- **Risk trigger:** Tier 3 trust-boundary hardening on deterministic, serialized, CRC-participating cross-frame state. The validation is a no-op for repository-produced saves but changes corrupt-file behavior and therefore needs the final-evidence gate.
- `iTick` is an integer and has no non-finite encoding; it must be in `[0, INT64_MAX - 1]` so the next tick remains representable. Finite validation applies to `fCurrentTime`, while both fields require cross-frame identity.
- Server-only implementation path. No client/server guard-affinity, allocation-tracked hot path, `.pack`, shader, or new-file project-membership exposure.
- Successful direct loads preserve the clock after validation; this plan independently strengthens what counts as a successful grid read.
