# Refactor: Coord-Update Allocation and Jitter Sampling

## Context

Source: /external-refactor-clean on `Engine/Source/Network` (recursive). Two confirmed issues in `Client::ServerCoordUpdateOrResend` (`ClientReceive.cpp`) — the highest-value allocation fix in the directory, and a jitter-measurement skew under scaled server time. Same function/file, one test surface.

## Design

### Engine/Source/Network/Client/ClientReceive.cpp — per-packet 1024-element resize
- Every per-tick coord update with payload does `update.statusChanges.resize(kiMaxStatusChangesPerCell)` (1024 elements; `game::StatusChange` is a type tag + `std::variant` of all-POD payload structs, largest `TransferData` ≈ 200 B, so a single ~200 KB allocation per packet), decompresses, then `resize(iCount)` — which shrinks size but **retains the full capacity**; the vector is then `std::move`d through `ReceivedCoordUpdate` into `CoordFrames::serverUpdates` (`ClientSessionBase.cpp:244-247`), so every buffered server update carries ~200 KB of slack until consumed (megabytes steady-state across 9-16 slots × jitter-buffer depth), allocated fresh per packet (`ClientReceive.cpp:338-340`). [~45m]
- Fix: decompress into a `gpThreadLocal->mWorkbuffer` arena scratch (`DecompressStatusChangeBatch` already borrows workbuffer internally — nested arenas are supported), then `update.statusChanges.assign(pScratch, pScratch + iCount)` for one exact-size allocation per update. (Root CLAUDE.md Workbuffer pattern.)

### Engine/Source/Network/Client/ClientReceive.cpp — jitter expectation ignores timescale
- The jitter sample uses `iExpectedUs = 1'000'000 / kiTickRate` (`:301`, fixed 31,250 µs) while the server broadcasts at wall-clock cadence scaled by `mTimeStep.miTimeMultiply/miTimeDivide`. At half speed every arrival deviates by ~31 ms, the smoothed jitter inflates, `iComputedTargetBehind` (`ClientSessionBase.cpp:295-296`) balloons, and the inflation persists through `common::Smoothed` decay after returning to 1×. The client knows the timescale (it decodes the timespeed broadcast). Fix (pick at grill): skip the jitter sample while the ratio ≠ 1/1 (simpler — the timescale is debug tooling; KISS), or scale the expectation `iExpectedUs = (1'000'000 * miTimeDivide) / (kiTickRate * miTimeMultiply)` via `game::gpGame->mTimeStep`. [~20m]

## Critical files

- `Engine/Source/Network/Client/ClientReceive.cpp` (`ServerCoordUpdateOrResend`)
- Read-only: `Engine/Source/Network/Client/ClientSessionBase.cpp` (`ApplyReceivedUpdatesBase` consumption), `Projects/BrokenEngineSandbox/Source/Network/NetworkSerialization.cpp` (`DecompressStatusChangeBatch`)

## Out of scope

- The event-rate stream allocations in `DecompressAndReadFrame`/`ServerCoordStaticData` (`ClientReceive.cpp:30,259`) — suppression-wrapped, per-event not per-tick; converting to Workbuffer needs a streambuf adapter (YAGNI).
- Server-side `BufferFullFrame` churn — `Refactor_ServerFrameBufferRecycle.md`.
- Any change to wire bytes, `kiMaxStatusChangesPerCell`, or the decompress format.
- The clock-correction controller itself (`ComputeClockCorrectionNs`) — only the sample fed into it.

## Acceptance criteria

- Steady-state client allocation per coord update is one exact-size `statusChanges` buffer (no 1024-capacity carriers in `CoordFrames::serverUpdates`); update application behavior unchanged (replay/reconcile clean in a playtest).
- With debug timescale engaged, `targetBehind` no longer inflates (log-verify `iComputedTargetBehind` before/after).

## Notes

- **Invariant exposure**: client receive path only — no shared-CRC/determinism exposure (StatusChange application order and contents unchanged; only buffer ownership and a clock-controller input sample change). Jitter item: one grill decision (skip-sample vs scale).

## Verification Notes

Verified against source (2026-06-10); one claim corrected:
- Allocation chain confirmed exactly: `resize(kiMaxStatusChangesPerCell)` / `DecompressStatusChangeBatch` / `resize(iCount)` at `ClientReceive.cpp:338-341` on a fresh `ReceivedCoordUpdate` per packet; moved via `mReceivedCoordUpdates.at(slot).push_back(std::move(update))` (`:345`) and again into `CoordFrames::serverUpdates` at `ClientSessionBase.cpp:244-247`. Capacity-retention through `std::move` is real.
- Corrected: `game::StatusChange` payload variants contain **no vectors** — all five alternatives are POD (`StatusChange.h:44-208`; largest `TransferData` is XMVECTORs + scalars, ~200 B). This makes the fix *simpler* than originally framed: `StatusChange` is trivially copyable, so a workbuffer scratch array + `assign(pScratch, pScratch + iCount)` has no construction/destruction hazards. Per-packet cost is one ~200 KB allocation (not 1024 small ones); the slack-carried-per-buffered-update magnitude is unchanged.
- Workbuffer nesting confirmed: `DecompressStatusChangeBatch` (game `NetworkSerialization.cpp:384-419`) borrows the workbuffer internally for its decompress scratch; nested arenas are the established pattern. Alternative shape if the grill prefers: a persistent `Client` member scratch vector reused across packets — same exact-size-assign result without pushing 200 KB through the workbuffer.
- Jitter skew confirmed: `iExpectedUs = 1'000'000 / kiTickRate` at `ClientReceive.cpp:301` is timescale-blind while the server tick cadence is wall-scaled via `SimToWall(kTickNs)` (`ServerSession.cpp:93`); the inflated `mSmoothedJitterUs` feeds `iComputedTargetBehind` at `ClientSessionBase.cpp:295-296` and decays slowly (`common::Smoothed`). Both fix options are feasible; the timescale is readable engine-side via `game::gpGame->mTimeStep` (sanctioned).
