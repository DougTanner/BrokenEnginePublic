<!-- broken-engine-plan/v1 {"createdUtc":"2026-07-19T18:49:18.000Z","dependsOn":[]} -->
# Loaded-Frame Clock Coherence Validation

## Context

The Tier-3 adversarial review of direct-load clock retention found a pre-existing trust-boundary gap that was explicitly outside that change's approved scope. `GameSaveLoad::ReadGrid` (`Projects/BrokenEngineSandbox/Source/Save/GameSaveLoad.cpp:543-636`) deserializes every saved coordinate frame independently in its per-frame loop (`:579-590`), validates the version header, frame count, global-ID counter, and client-coordinate membership, then sets the global clock from `mCoordFrames.begin()->second.pCurrent->interpolate` (`:600-604`) — inside the `try`, before the final `fileStream.good()` gate (`:618-627`). It never proves that all loaded frames carry the same `FrameInterpolateBase::iTick` and `fCurrentTime`, that the selected tick is nonnegative and safe to advance once, or that the selected `fCurrentTime` is finite.

Repository-produced saves are coherent: `RunFrameTick` stamps every active frame from the same global tick/time (`Projects/BrokenEngineSandbox/Source/Frame/FrameTick.cpp:66-69`), `ServerSession::SyncActiveFrames` removes inactive frames before subsequent ticks (`Projects/BrokenEngineSandbox/Source/Network/Server/ServerSession.cpp:357-380`), and `GameSaveLoad::WriteGrid` serializes the remaining current frames. A crafted or corrupt multi-cell save can nevertheless pass current validation with divergent clocks, an all-equal negative or maximum tick, or an all-equal non-finite time. Divergent clocks also let unordered-map iteration choose the server clock arbitrarily. Tick and time are serialized and included in the shared frame checksum (`Engine/Source/Frame/FrameBase.cpp` — `FrameInterpolateBase::Crcs` `:8-22`, `Write`/`Read` `:35-57`), while `GameBase::SetTickCounter` requires a nonnegative tick (`Engine/Source/GameBase.h:164`, an ASSERT that must never see raw file data) and the next advancing server update increments it (`Engine/Source/GameBase.cpp:169-170`). Accepting any such clock violates the loaded-grid and next-tick invariants used by direct load (`ServerLoad`), startup autoload (`Autoload`), replay setup (`SaveLoadReplay`), and client resynchronization.

## Design

All edits land inside `GameSaveLoad::ReadGrid`; every failure below throws `common::CorruptStreamException`, which the existing `catch` already converts into the clean-fallback `false` return.

1. In the per-frame deserialization loop, capture the first deserialized frame's `interpolate.iTick` and `interpolate.fCurrentTime` as the candidate loaded clock. Reject the candidate unless `0 <= iTick < std::numeric_limits<int64_t>::max()` and `fCurrentTime` is finite, so `SetTickCounter`'s nonnegative requirement is proven before the call and the next server update can increment the tick without signed overflow.
2. Validate every subsequently deserialized frame against that candidate before accepting the grid. Require exact integer-tick equality and bit-identical finite current-time values (compare the `float` bit patterns, not numeric equality); float numeric equality alone is insufficient because signed-zero encodings serialize and checksum differently.
3. Keep clock adoption atomic with the existing all-or-nothing read boundary: retain the validated candidate in locals and move the `SetTickCounter` / `SetCurrentTime` calls out of the `try` block to after the `fileStream.good()` gate, beside the existing deferred `SetNextGlobalId` adoption (`:629-632`), so they run only once client-coordinate membership and stream state have both passed. Any clock-coherence failure follows the existing catch path, which clears the partial grid and fleet state (`mCoordFrames.clear()` + `mpFleetManager->ResetState()`) and lets each caller fall back to a fresh game.
4. Keep the current file shape and valid-save behavior. Do not normalize, repair, or choose among inconsistent clocks.

## Scope contract

The listed scope is both target and ceiling: make the smallest complete change satisfying the design and acceptance criteria, and add no abstractions, configuration options, refactors, or fixes to adjacent code encountered along the way. Naming a file grants no permission to touch anything in it beyond the named regions plus the mechanical necessities (includes, declarations) the named change requires.

### In scope

- `Projects/BrokenEngineSandbox/Source/Save/GameSaveLoad.cpp`, function `GameSaveLoad::ReadGrid` only:
	- candidate-clock capture and range/finiteness validation in the per-frame deserialization loop (`:579-590`);
	- per-frame cross-frame tick/time identity validation in that same loop;
	- relocation of the existing `SetTickCounter` / `SetCurrentTime` adoption (`:600-604`) to after the `fileStream.good()` gate, beside `SetNextGlobalId` (`:629-632`), reading from the validated locals.
- `Projects/BrokenEngineSandbox/Source/Save/AGENTS.md` — confirm the "Apply counters and clocks only after full validation" save/load contract wording; refine only if this change makes the existing wording incomplete.

### Out of scope

- Every other function in `GameSaveLoad.cpp` (`WriteGrid`, `ServerLoad`, `Autoload`, `ServerReset`, replay recording/playback) beyond the named `ReadGrid` regions.
- Save/replay format, serialization order or width, `Frame::kiVersion`, `.pack` data, wire protocol, or frame/collection layout changes.
- Backward compatibility or salvage of corrupt/internally inconsistent saves.
- General range or finite validation for other deserialized frame, fleet, or static-data fields.
- Changes to direct-load clock retention, startup-autoload policy, replay semantics, client clock correction, active-set pruning, or `Game::Reset`.
- Unit tests.

## Critical files

- `Projects/BrokenEngineSandbox/Source/Save/GameSaveLoad.cpp` — `GameSaveLoad::ReadGrid`: per-frame validation, failure cleanup, and final loaded-clock adoption. Sole code file changed.
- `Projects/BrokenEngineSandbox/Source/Frame/FrameTick.cpp` — `RunFrameTick`, read-only source of the single-clock production invariant.
- `Engine/Source/Frame/FrameBase.cpp` / `.h` — read-only serialization and shared-checksum ownership of `FrameInterpolateBase::iTick` / `fCurrentTime`.
- `Engine/Source/GameBase.h` / `.cpp` — read-only `SetTickCounter` / `SetCurrentTime` contracts and the per-update tick increment.
- `Projects/BrokenEngineSandbox/Source/Save/AGENTS.md` — save/load contract wording (see scope contract).

## Acceptance criteria

- Server target compiles with no compile or link errors.
- A normal multi-cell save created through the agent harness loads successfully; all loaded frames remain queryable, the saved tick is retained, and the first subsequent update observes saved tick + 1.
- Starting from that valid save, a temporary fixture whose second serialized frame carries a different tick is rejected through the harness `load` command (`resetToFresh: true`); the server exposes only fresh fallback state, not a partially loaded grid or the fixture's clock.
- Two separate tick-domain fixtures whose serialized frame ticks are all changed identically to `-1` and `INT64_MAX`, respectively, are each rejected with the same clean-fallback observation. This proves that an internally coherent candidate must still be nonnegative and advanceable.
- A separate time-domain fixture whose candidate and every serialized frame current-time value are all changed identically to the same NaN or infinity encoding is rejected with the same clean-fallback observation. This fixture is independent of the distinct second-frame tick-mismatch fixture, so rejection proves candidate finiteness rather than only cross-frame identity validation.
- Construct the corrupt fixtures by patching only the repeated serialized `iTick` / `fCurrentTime` clock bytes in copies of the harness-produced save and verify each expected multi-frame clock sequence before loading.
- Static inspection confirms every successful `ReadGrid` entry point (`ServerLoad`, `Autoload`, replay grid load in `SaveLoadReplay`) receives the same validated clock, while every failure returns before global clock adoption.
- Final diff contains no serialization order/width, version, protocol, layout, or checksum-algorithm change; known-good save and replay bytes remain accepted unchanged.

## Notes

- **Risk trigger:** Tier 3 trust-boundary hardening on deterministic, serialized, CRC-participating cross-frame state. The validation is a no-op for repository-produced saves but changes corrupt-file behavior and therefore needs the final-evidence gate.
- `iTick` is an integer and has no non-finite encoding; it must be in `[0, INT64_MAX - 1]` so the next tick remains representable. Finite validation applies to `fCurrentTime` (a `float`), while both fields require cross-frame identity.
- Server-only implementation path (`GameSaveLoad.cpp` is whole-file `BT_SERVER`). No client/server guard-affinity, allocation-tracked hot path, `.pack`, shader, or new-file project-membership exposure.
- Successful direct loads preserve the clock after validation; this plan independently strengthens what counts as a successful grid read.
- Line numbers cite the current tree at plan-modernization time; the named functions and regions are authoritative if lines drift.
