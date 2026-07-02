# Client Full-State Edge Fixes

## Context

Two game-client full-state-handling edge bugs. Both violate the documented same-frame activation/adoption invariant (engine `Network/Client/CLAUDE.md`: a full state activates its slot at receive time; because `Poll()` clears receive buffers at entry, the game layer must adopt the frame in the same frame or lose it while keeping the activated slot).

### (a) Stall-path adoption loss

While desync-stalled, `ClientSession::PollNetwork` (`Projects/BrokenEngineSandbox/Source/Network/Client/ClientSession.cpp:59-118`) still calls `PollConnection()` (`:64`), which calls `mpClientNetwork->Poll()` (`:308`) — that clears all receive buffers at entry and **activates slots at receive time** (`Client::ServerCoordFullState` sets `kActive`, `Engine/Source/Network/Client/ClientReceive.cpp:215`). But `PollConnection` returns `false` when `IsStalled()` (`ClientSession.cpp:328-331`), so `PollNetwork` early-returns at `:66` — **before** `ParsePlayerEvents` (`:76`), `ApplyReceivedStaticData` / `ApplyReceivedFullStates` (`:114-115`), and `ApplyReceivedUpdates` (`:117`).

Consequences during a stall:

- A full state arriving mid-stall is destroyed on the next `Poll()` while its slot stays `kActive` — the `CoordFrames` entry may never be created. Then `ClientSessionBase::ApplyReceivedUpdatesBase` (`Engine/Source/Network/Client/ClientSessionBase.cpp:257`) does `game::gpGame->mCoordFrames.at(coord)` on the now-active slot → **throws** on the missing entry.
- Reliable game packets (player assign, fleet sync) received during the stall are silently dropped by the same early return.

### (b) Pending-full-state livelock

A `pendingFullState` whose tick sits past a gap in `serverUpdates` is never injectable:

- `RunPrimaryReplay` injects only when `pendingFullState->iTick == iConfirmedTick` (`Projects/BrokenEngineSandbox/Source/Network/Client/ReconcileReplay.cpp:167-177`).
- The catch-up replay injects only at `iTick == pendingFullState->iTick` **inside** the consecutive replay range (`ReconcileReplayTick.cpp:284-289`), which cannot reach past a gap in `serverUpdates`.

So when the matching tick is unreachable, the coord re-enters full replay every frame. The repeated-work `DEBUG_BREAK` guard (`ReconcileReplay.cpp:305-313`) is explicitly bypassed while `pendingFullState.has_value()` (`:310` `&& !rFrames.pendingFullState.has_value()`), masking the loop — until the gap fills or the update buffer overflows (`ClientSessionBase.cpp:268-275`, itself a `DEBUG_BREAK`). Reachable via duplicate/late full states (e.g. a double resync).

## Design

### (a) — grill decision, options pre-staged in Notes

Ensure receive buffers are not silently discarded while stalled. Two viable approaches:

- **A1 — skip `mpClientNetwork->Poll()` while stalled.** ENet queues incoming packets; do not drain them until the stall clears, so nothing activates or is destroyed mid-stall. Cleanest match to the invariant; must confirm nothing else in `PollConnection` (disconnect detection, debug-frame drain, LAN discovery) depends on `Poll()` running each frame during a stall.
- **A2 — run the adoption steps before the stall short-circuit.** Reorder so at minimum `ApplyReceivedFullStates` (and ideally static data + player events) runs before the `IsStalled()` early return, so an activated slot always gets its `CoordFrames` entry adopted in the same frame.

Either restores the same-frame invariant; the pick hinges on what else `PollConnection` must keep doing during a stall.

### (b)

In the no-replay-possible case (`pendingFullState` present but its tick is unreachable — past a gap, never at `iConfirmedTick`, and the consecutive replay range cannot cover it), **adopt the pending full state directly as the new confirmed frame**: move it into the ring, recompute its CRC, set `iConfirmedTick`/`iConfirmedOffset`, and discard the older speculative ring — mirroring the initial-setup direct-adoption path in `ClientDataReceiver::ApplyReceivedFullStates` (`Projects/BrokenEngineSandbox/Source/Network/Client/ClientDataReceiver.cpp:82-120`, the `iConfirmedTick < 0` block). This breaks the livelock and matches server intent (a full state is authoritative for its tick).

## Critical files

- `Projects/BrokenEngineSandbox/Source/Network/Client/ClientSession.cpp` — `PollNetwork` / `PollConnection` stall ordering (a).
- `Projects/BrokenEngineSandbox/Source/Network/Client/ReconcileReplay.cpp` — `RunPrimaryReplay` injection gate and the repeated-work `DEBUG_BREAK` guard; the no-replay-possible adoption branch (b).
- `Projects/BrokenEngineSandbox/Source/Network/Client/ReconcileReplayTick.cpp` — `ReconcileReplayCoord` catch-up injection (b, reference).
- `Projects/BrokenEngineSandbox/Source/Network/Client/ClientDataReceiver.cpp` — `ApplyReceivedFullStates` initial-setup direct-adoption (fix reference for b); `pendingFullState` set site (`:121-135`).
- `Engine/Source/Network/Client/ClientSessionBase.cpp` — `ApplyReceivedUpdatesBase` `mCoordFrames.at(coord)` throw site (a, symptom).

## Out of scope

- No change to the engine slot state machine, `ClassifyFullState`, or the wire format — this is game-layer full-state adoption/replay only (engine-side lifecycle is `Network/SubscriptionLifecycleRaceHardening.md`).
- No change to the CRC fast-path walk, rollback-base selection, jitter-adaptive throttle, or the reconciliation sim math — (b) is state selection only.
- Not reworking desync-stall entry/exit or the debug-frame capture flow.
- No `Frame::kiVersion` / protocol-version change.

## Acceptance criteria

- A full state (and reliable game packets) received while stalled is not lost: no `mCoordFrames.at(coord)` throw when the stall clears, and player assign / fleet sync delivered during a stall are applied.
- A `pendingFullState` whose tick sits past a `serverUpdates` gap is adopted as the new confirmed frame instead of re-triggering full replay every frame; the repeated-work loop no longer relies on the `has_value()` guard bypass to avoid `DEBUG_BREAK`.

## Notes

- **Invariant exposure**: (b) touches the rollback/replay path (determinism/CRC-adjacent) but changes **frame/state selection only — no sim math** (adopts an authoritative full state as confirmed, mirroring an existing path). (a) is receive-ordering, no sim exposure. Both game-client-only (`BT_CLIENT`). No wire/`kiVersion` change (Risk: cross-frame reconcile state, hard to verify).
- **Grill decision to pre-stage**: option A1 (skip `Poll()` while stalled) vs A2 (run adoption steps before the short-circuit) for fix (a) — see Design. Recommend A1 unless `PollConnection`'s in-stall responsibilities (disconnect detection, debug-frame drain) require `Poll()` each frame, in which case A2. Fix (b) has no open decision.
- Largely independent of the engine-layer plans (`ResyncFullStateRepair.md`, `SubscriptionLifecycleRaceHardening.md`), but conceptually paired: those restore correct slot activation; this ensures the game layer adopts the resulting full states. Can land in the same network session or standalone.
