<!-- broken-engine-plan/v1 {"createdUtc":"2026-08-07T13:09:07.160Z","dependsOn":[]} -->
# Decompose GameBase::Render along its three phase seams

## Context

`GameBase::Render` (`Engine/Source/GameBase.cpp:462-688`) measures cyclomatic complexity 27 and mixes three distinct ordered responsibilities:

1. Camera-coord fallback (lines 481-500) — consumes `mActiveCoords` and member frame state, produces `cameraCoord` and `bHaveRenderableCamera`.
2. Interpolation / camera / visual-update phase (lines 504-617) — consumes those two values, internally produces `fDeltaTime`, owns the allocation-suppressed prune/interpolate block, camera update, and visual-error decay.
3. Deferred-swapchain-recreate + minimized-throttle phase (lines 620-678) — a resource-owning concern (drives the `mMinimizedThrottleTimer` / `mMinimizedThrottleLast` waitable-timer lifecycle, destruction stays in `~GameBase`) whose only outward effect is whether `Render` exits before the render tail.

The seams were verified as real by the /external-deep-analysis Phase-3 reviewer (verdict: CONFIRMED-WITH-CAVEATS): no hidden state crosses them beyond the values named above plus `fCurrentTime`, `rActiveCoords`, and `mRenderInterpolates` retained by the render tail. This file is the highest-structural-erosion file in `Engine/Source` (0.892 vs corpus 0.553); its sibling hot functions (`ServerUpdate`, `ClientUpdate`, `AdvanceRenderClock`) were reviewed and recorded as decomposition-not-warranted residuals — do not extend this plan to them.

## Design

Pure code motion: extract the three phases into private `BT_CLIENT` member functions of `GameBase` declared in `Engine/Source/GameBase.h`, called from `Render` in the exact current order. The deferred-swapchain phase returns whether `Render` must exit early. No logic, ordering, allocation-suppression scope, OS-call handling, or logging changes. Naming follows the style guide (complete words, no `Impl`/`Internal` suffixes).

Caveat carried from verification: do not create forwarding micro-helpers merely to satisfy the complexity number. If a cohesive whole-phase helper still exceeds cyclomatic complexity ten after extraction, record that as a residual instead of splitting further.

## Critical files

- `Engine/Source/GameBase.cpp` — `GameBase::Render`
- `Engine/Source/GameBase.h` — private `BT_CLIENT` member declarations

## In scope

- `GameBase::Render`: extraction of the camera-coord fallback, the interpolation/camera/visual-update phase, and the deferred-swapchain/minimized-throttle phase into private member functions; the matching header declarations

## Out of scope

- `ServerUpdate`, `ClientUpdate`, `AdvanceRenderClock`, `BuildAndDispatchFrameTicks`, and every other function (recorded residuals, not targets)
- Any behavior, ordering, or logging change; any public signature change
- The `mfLastRenderFrameSeconds` sim-vs-wall fix (owned by the coordinated plan below)

## Risk tier and invariants

Tier 2 — frame-sequencing code motion in the client render path. Invariants: frame/tick phase order and `BT_CLIENT`/`BT_SERVER` guard placement preserved exactly; PostRender state stays bit-deterministic with unchanged per-tick CRC (client render is outside the CRC, and this plan must keep it so); no heap allocation added in the main loop (the existing `ScopedSuppressAllocationTracking` scopes move verbatim with their `// Heap:` comments).

## Acceptance criteria

- No function resulting from the decomposition exceeds cyclomatic complexity ten, measured by re-running `code-quality-metrics` Snapshot on `Engine/Source/GameBase.cpp`; a cohesive whole-phase helper above ten is recorded as a residual rather than split cosmetically.
- Client and server both compile (guard-placement check).
- Diff review confirms pure code motion: statement order and conditions unchanged inside each moved phase.

## Coordination

`Documents/Plans/Engine/RenderFrameSecondsWallClock.md` edits the delta measurement and `mfLastRenderFrameSeconds` assignment inside the interpolation phase. The two plans are order-independent: whichever lands second rebases mechanically; regions are identified by symbol, not line number.

## Notes

Origin: /external-deep-analysis (2026-08-07), Phase 2 finding, Phase-3 verified with caveats. Evidence log: `Temp/deep-analysis-gamebase/out-verify.md` (machine-local, not tracked).
