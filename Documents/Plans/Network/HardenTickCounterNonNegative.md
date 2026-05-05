# Harden Tick Counter Against Negative Values

Source: session audit follow-up to the save-load `ASSERT(inputs.iTargetTick >= 0)` fix in `ClientReconciler::Run()`.

## Context

`ClientReconciler::Run()` recently tripped `ASSERT(inputs.iTargetTick >= 0)` after a save load. Root cause: `ClientDataReceiver.cpp` called `gpGame->SetTickCounter(iTick - iInitialTargetBehind)` where `iInitialTargetBehind = 4` and the post-load server's `iTick` was 3, producing -1. The fix was a `std::min` clamp at that one site. This plan captures two related items the audit found that share the same underflow shape: a second negative-tick path (clock snap), and a fail-fast assert at the funnel through which all `SetTickCounter` calls pass.

## Out of scope

- Other reconciliation invariants (covered by `Network/ReconcileWasteAndInvariant.txt`).
- `mTickRemainderNs` / `ResetRenderClock` semantics — only `miTickCounter` is in scope.
- `SetCurrentTime` clamping — sim time can be reasoned about independently and is not asserted on `>= 0`.
- Investigating whether `miCurrentTargetBehind` itself can be tightened against jitter; we only neutralize the underflow at the call site.

## Acceptance criteria

- `iSnapTick` in `ClientSession::Reconcile`'s clock-snap branch can never be negative.
- `engine::GameBase::SetTickCounter` `DEBUG_BREAK`s on any negative input.
- Save-load + immediate clock-snap scenarios do not assert.
- All three existing `SetTickCounter` callers continue to compile and run unchanged.

## Item A — Clock-snap clamp in `ClientSession::Reconcile`

**File**: `Projects/BrokenEngineSandbox/Source/Network/Client/ClientSession.cpp`
**Symbol**: `ClientSession::Reconcile`, clock-snap branch (currently line 201).

The snap path has the same underflow shape as the initial-setup path that just got patched. Today:

```cpp
int64_t iSnapTick = miLatestServerTick - miCurrentTargetBehind;
gpGame->SetTickCounter(iSnapTick);
```

Gated only by `miLatestServerTick >= 0` and either `mbClockErrorDisconnect` or `std::abs(miClockError) >= 28`. `miCurrentTargetBehind` is computed from observed jitter via `ComputeClockCorrectionNs` and can exceed `miLatestServerTick` if a snap fires shortly after a save load while the jitter buffer is large but the server tick is small. Reachable but rare.

**Change**: clamp at zero, matching the comment style added in `ClientDataReceiver.cpp`.

```cpp
// Clamp at 0 so a fresh post-load server (latestServerTick < currentTargetBehind)
// doesn't drive the client tick negative.
int64_t iSnapTick = std::max<int64_t>(0, miLatestServerTick - miCurrentTargetBehind);
gpGame->SetTickCounter(iSnapTick);
```

The `LOG` line below already prints `iSnapTick` — leave it untouched; it remains correct and now logs the clamped value.

## Item B — Fail-fast assert in `engine::GameBase::SetTickCounter`

**File**: `Engine/Source/GameBase.h`
**Symbol**: `engine::GameBase::SetTickCounter` (currently line 177).

`SetTickCounter` is the funnel through which every tick-counter mutation passes. Asserting non-negativity here catches any future regression at the source rather than downstream in `ClientReconciler::Run()`.

Today:

```cpp
void SetTickCounter(int64_t iTickCounter) { miTickCounter = iTickCounter; }
```

**Change**:

```cpp
void SetTickCounter(int64_t iTickCounter) { ASSERT(iTickCounter >= 0); miTickCounter = iTickCounter; }
```

**Callers verified non-negative** (full project, three sites — confirm via Grep at execution time):

1. `Projects/BrokenEngineSandbox/Source/Network/Client/ClientSession.cpp:203` — clock-snap branch. Becomes safe after Item A's clamp.
2. `Projects/BrokenEngineSandbox/Source/Network/Client/ClientSession.cpp:323` — reset path. Verify at execution time that the value is non-negative (expected: literal `0` or equivalent).
3. `Projects/BrokenEngineSandbox/Source/Network/Client/ClientDataReceiver.cpp:101` — initial-setup path. Already clamped via `std::min<int64_t>(kiInitialTargetBehind, iTick)` (the originating bug fix).

Item A must land in the same change as Item B, otherwise the assert can fire on the clock-snap path.

## Critical files

- `Projects/BrokenEngineSandbox/Source/Network/Client/ClientSession.cpp`
- `Projects/BrokenEngineSandbox/Source/Network/Client/ClientDataReceiver.cpp` (caller verification only — no edits)
- `Engine/Source/GameBase.h`

## Notes

- Items A and B are independent in implementation but share one goal (no negative tick) and one PR boundary. Land both together so the new assert never fires on existing code paths.
- If a fourth `SetTickCounter` caller appears between authoring and execution, audit it against the non-negative invariant before adding the assert.
