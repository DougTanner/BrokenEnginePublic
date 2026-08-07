<!-- broken-engine-plan/v1 {"createdUtc":"2026-08-07T13:09:05.526Z","dependsOn":[]} -->
# Fix: mfLastRenderFrameSeconds stores sim-scaled seconds against its wall-clock contract

## Context

`GameBase::Render` (`Engine/Source/GameBase.cpp:516-517`) measures the render-frame delta and stores it into `mfLastRenderFrameSeconds` only after `TimeStep::WallToSim` has applied the current timespeed ratio. The member's contract (`Engine/Source/GameBase.h:195-199`) documents wall-clock seconds, and both consumers rely on that: camera blend / shake decay / `mfTime` (`Projects/BrokenEngineSandbox/Source/Graphics/Camera.cpp:72-80`) and visual-error-offset decay (`Engine/Source/GameBase.cpp:606-617`).

Root cause: one value serves two contracts. `AdvanceRenderClock` needs the sim-scaled delta (`dSimDeltaSeconds`) so the render clock tracks simulation speed; the display-rate consumers need the unscaled wall delta. Line 517 caches the sim-scaled value for both.

Reachability: non-1:1 ratios are a live client state — `ServerSession::BroadcastTimespeedIfChanged` (`Projects/BrokenEngineSandbox/Source/Network/Server/ServerSession.cpp:451`) broadcasts them and `ClientSession::ProcessReceivedGamePackets` (`Projects/BrokenEngineSandbox/Source/Network/Client/ClientSession.cpp:96`) applies them to the client `TimeStep`; debug input and the agent timescale command produce them. At 2:1 timespeed, camera blend, shake decay, and reconciliation-error decay advance twice per wall second; at fractional speed they lag.

Authority order: the header and consumer comments outrank the contradictory code behavior — wall-clock intent is trusted.

Verified by /external-deep-analysis of `Engine/Source/GameBase.cpp` (Phase-3 reviewer verdict: CONFIRMED, functional client bug).

## Design

In `GameBase::Render`, measure `mRenderTimer.GetDeltaNs(true)` exactly once at the existing sampling location. Store its unscaled wall-clock seconds in `mfLastRenderFrameSeconds`. Apply `mTimeStep.WallToSim` to that same measured delta only to produce `dSimDeltaSeconds` for `AdvanceRenderClock`. Call order and sampling location stay unchanged; no other consumer changes.

## Critical files

- `Engine/Source/GameBase.cpp` — `GameBase::Render`, lines 516-517 region
- `Engine/Source/GameBase.h` — `mfLastRenderFrameSeconds` comment only if wording needs to name the sim/wall split

## In scope

- `GameBase::Render`: the delta measurement and the `mfLastRenderFrameSeconds` assignment; the `dSimDeltaSeconds` computation feeding `AdvanceRenderClock`

## Out of scope

- `AdvanceRenderClock` internals (its input stays sim-scaled by design)
- `Camera::Update`, visual-error decay math, and every other consumer
- Timespeed broadcast/receive paths

## Risk tier and invariants

Tier 2 — scoped client runtime behavior in one subsystem. Client-only render/interpolate state: no PostRender bit-determinism, per-tick CRC, wire, or serialization exposure. Frame/tick phase order, `BT_CLIENT` guard placement, and main-loop allocation behavior unchanged.

## Acceptance criteria

- With timespeed 1:1, rendered behavior is byte-identical in intent (same measured delta reaches both consumers as today).
- With a non-1:1 timespeed (agent timescale command), camera shake decay and visual-error decay advance at wall rate while the render interpolation clock still advances at sim rate.
- Client and server both compile.

## Coordination

`Documents/Plans/Engine/GameBaseRenderDecomposition.md` moves the surrounding interpolation phase of `GameBase::Render` into a private member function. The two plans are order-independent: identify this plan's region by the `mRenderTimer.GetDeltaNs` measurement and the `mfLastRenderFrameSeconds` assignment (wherever the decomposition placed them), not by line number.

## Notes

Origin: /external-deep-analysis (2026-08-07), Phase 1 Lens B finding, Phase-3 verified. Evidence log: `Temp/deep-analysis-gamebase/out-verify.md` (machine-local, not tracked).
