# Clock-Error Machinery Cleanup

## Context

Cross-validated by two independent reviewers: the client's "disconnect on sustained clock error" machinery is effectively unreachable dead code, sitting behind a snap that fires first. Plus two riders (a misplaced constant and an unnecessary state reset) and a doc fix.

### (a) Sustained-clock-error disconnect is unreachable

Two constants gate two behaviors on the same `miClockError` magnitude:
- Snap threshold `kiClockSnapThreshold = 28` — **function-local** `static constexpr` in `game::ClientSession::Reconcile` (`Projects/BrokenEngineSandbox/Source/Network/Client/ClientSession.cpp` ~line 213).
- Disconnect threshold `kiClockErrorDisconnectThreshold = 64` and `kiClockErrorDisconnectConsecutiveFrames = 4` (`Engine/Source/Network/NetworkProtocol.h` ~lines 70-71).

Per-frame flow in `ClientSession::Reconcile` (~lines 211-228):
1. `ComputeClockCorrectionNs` → `ClientSessionBase::ComputeClockCorrectionNs` (`ClientSessionBase.cpp` ~lines 367-383): if `|iError| >= 64`, `++miConsecutiveClockErrorFrames`; at `>= 4` it sets `SessionStateFlags::kClockErrorDisconnect`. Otherwise resets the counter to 0.
2. Immediately after (~line 214): `if (miLatestServerTick >= 0 && (kClockErrorDisconnect || |miClockError| >= 28))` → **snap**: reset `miTickCounter`, clear accumulator, `miConsecutiveClockErrorFrames = 0`, `miClockError = 0`, `miLatestServerTick = -1`.

Because `|error| >= 64` implies `|error| >= 28`, any frame that increments the disconnect counter also (when `miLatestServerTick >= 0`) fires the snap the same frame and zeroes the counter. It can never reach 4; `kClockErrorDisconnect` never fires. `Network.md` (~line 20) documents "accumulate a consecutive-frame counter and disconnect if sustained" — describing behavior that cannot occur. Worst case is snap-looping forever instead of disconnecting.

Nuance (interacts with (c)): the one window where the counter is *not* reset is while `miLatestServerTick == -1` (the snap guard `miLatestServerTick >= 0` is false), which is exactly the transient created by (c)'s own reset. Removing that reset (c) closes the window and makes (a)'s unreachability total; keeping the disconnect path alive would have to contend with it.

### (b) `kiClockSnapThreshold` misplaced

The snap threshold lives function-local in the game layer while its siblings (`kiClockErrorDisconnectThreshold`, `...ConsecutiveFrames`) are engine constants in `NetworkProtocol.h`. `Network.md` (~line 20) explicitly calls out the odd location as a gotcha ("defined in the game layer at `ClientSession.cpp` — *not* alongside the engine-layer constants").

### (c) Snap resets `miLatestServerTick = -1` unnecessarily

The snap sets `miLatestServerTick = -1` (`ClientSession.cpp` ~line 227). `ClientSessionBase::GetTargetSimTick()` (`ClientSessionBase.h` ~line 49) returns `-1` when `miLatestServerTick < 0`. In `GameBase::ClientUpdate` (`Engine/Source/GameBase.cpp` ~line 62), the hard-ceiling clamp is gated on `iCeiling >= 0` (~line 63) — so with the ceiling at `-1` the clamp is **skipped**, and the client sim can free-run past the server until the next coord update re-learns `miLatestServerTick` (`ClientSessionBase::ApplyReceivedUpdatesBase` ~line 266: `miLatestServerTick = std::max(miLatestServerTick, rUpdate.iTick)`, only for updates with `iTick > iConfirmedTick`). During a loss burst the ceiling stays off across several frames. The reset appears unnecessary: the snap already set `miClockError = 0` and moved `miTickCounter` to the target, so `miLatestServerTick` need not be discarded — keeping it preserves the hard ceiling across the snap.

### (d) Doc: accumulator-nudge magnitude wrong

`Network.md` (~line 20) says small errors are corrected "up to 4 steps per frame". Implemented (`ClientSessionBase.cpp` ~lines 390-392): `iCorrectionSteps = clamp(iError, -4, 4)`, `iDivisor = (|iError| >= 4) ? 8 : 64`, `correction = -iCorrectionSteps * tickNs / iDivisor`. Max magnitude is `4 * tickNs / 8 = 0.5 tick/frame` (and `< 4/64` tick when `|error| < 4`) — not 4 steps.

## Design

### (a) — Decision plan (present options)

- **Option A — Delete the disconnect machinery.** Remove `kiClockErrorDisconnectThreshold`, `kiClockErrorDisconnectConsecutiveFrames`, `miConsecutiveClockErrorFrames`, `SessionStateFlags::kClockErrorDisconnect`, the accumulate/reset block in `ComputeClockCorrectionNs`, and the `kClockErrorDisconnect` term in the snap condition and its clears (the snap already fires on `|error| >= 28` alone). Update every reset site of the removed members (`ClientSessionBase.cpp` ~lines 61, 318; `ClientSession::ResetForServerLoad` ~lines 350-351). Simplest; matches actual behavior (snap-and-recover, no clock-based disconnect). The existing gap-based disconnects (`Client::TrackReceivedTick` "too many missing frames" ~line 271) remain the real disconnect path.
- **Option B — Make it real.** Escalate to disconnect after N snaps within a window (a snap-rate counter, since a lone snap is normal init/stall-recovery but repeated snapping means the client can't hold clock lock). Preserves the documented intent; adds a new counter + window constant and a design decision on thresholds.

Lean A unless a real "endless snap-loop" scenario is worth defending against — B reintroduces a policy that must be tuned and tested. Resolve via grill.

### (b)

Move `kiClockSnapThreshold` to `NetworkProtocol.h` beside its siblings (rename to the engine namespace/style of the neighbors). Drop the `Network.md` "defined in the game layer ... not alongside the engine-layer constants" caveat once moved.

### (c)

Remove the `miLatestServerTick = -1` assignment from the snap in `ClientSession::Reconcile` (~line 227) — keep the learned value so `GetTargetSimTick()` stays `>= 0` and the `ClientUpdate` hard ceiling remains in force immediately after a snap. (The session-reset paths that legitimately clear it to `-1` — `ResetForServerLoad`, disconnect — are unaffected.)

### (d)

Fix `Network.md` (~line 20): correct the nudge description to the implemented `≤ 0.5 tick/frame` (`4 * tickNs / 8`), and fold in the (a)/(c) outcome — remove or rewrite the "disconnect if sustained" sentence to match whichever (a) option lands, and note the snap no longer discards `miLatestServerTick`.

## Critical files

- `Projects/BrokenEngineSandbox/Source/Network/Client/ClientSession.cpp` — `Reconcile` snap block: `kiClockSnapThreshold` relocation (b), `kClockErrorDisconnect` term / counter clears per (a), `miLatestServerTick = -1` removal (c); `ResetForServerLoad` reset sites if (a)=A.
- `Engine/Source/Network/Client/ClientSessionBase.cpp` — `ComputeClockCorrectionNs` accumulate/reset block (a); reset sites ~lines 61, 318 (a); accumulator nudge is the doc reference for (d).
- `Engine/Source/Network/Client/ClientSessionBase.h` — `GetTargetSimTick()` (behavioral reference for (c)); `miConsecutiveClockErrorFrames` member if (a)=A.
- `Engine/Source/Network/NetworkProtocol.h` — `kiClockSnapThreshold` new home (b); disconnect constants removed if (a)=A.
- `Engine/Source/GameBase.cpp` — `ClientUpdate` hard-ceiling clamp (~lines 62-63): the consumer of `GetTargetSimTick()` whose behavior (c) restores across the snap.
- `Documents/Architecture/Network.md` — clock-error section (~line 20): nudge magnitude + disconnect/`miLatestServerTick` wording (d).

## Out of scope

- The snap mechanism itself (threshold value 28, the tick clamp-at-zero, accumulator/render-clock reset) — this plan does not retune when the snap fires, only what state it discards (c) and the dead disconnect path beside it (a).
- The gap-based disconnect in `Client::TrackReceivedTick` ("too many missing frames", `Client.cpp` ~line 271) — a separate, working disconnect path, unchanged.
- `miCurrentTargetBehind` / jitter-buffer computation and the `targetBehind` hysteresis — untouched.
- `Frame::kiVersion` / CRC / wire — none involved.

## Acceptance criteria

- No path can set `SessionStateFlags::kClockErrorDisconnect` and then fail to act on it (Option A removes it; Option B makes escalation reachable).
- After a clock snap, `GetTargetSimTick()` returns `>= 0` (ceiling active) so the client sim cannot free-run past the server during a subsequent loss burst.
- `kiClockSnapThreshold` is defined once, in `NetworkProtocol.h`, with no game-layer duplicate.
- `Network.md`'s clock-error section matches the code: nudge `≤ 0.5 tick/frame`, and no claim of a disconnect that cannot happen.

## Notes

- **Decision plan (present options)** — item (a) delete-vs-make-real is the one open decision; (b), (c), (d) are determined.
- **Invariant exposure**: no wire change; client clock/disconnect policy and cross-frame tick state only. No `Frame::kiVersion` / CRC / determinism exposure. Item (c) changes runtime ceiling behavior immediately after a snap (Risk: needs a packet-loss-burst playtest to confirm no new startup/recovery stall); (a) Option A is dead-code removal, Option B adds tested policy.
- **Grill decision to pre-stage**: (a) Option A (delete) vs Option B (snap-rate escalation window) — and if B, the snap-count and window constants. Confirm (c) with A: removing `miLatestServerTick = -1` while also deleting the disconnect path means the only prior consumer of the `-1` transient (the un-reset counter window) is gone, so the two changes are mutually reinforcing.
- **Co-scheduling**: touches `ClientSessionBase.{h,cpp}` and `ClientSession.cpp` clock paths; no other queued plan edits these, so standalone. `Network.md` doc edit pairs naturally with the `update-architecture-diagrams` step.
