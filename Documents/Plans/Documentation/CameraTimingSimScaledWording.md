<!-- broken-engine-plan/v1 {"createdUtc":"2026-08-07T13:08:44.980Z","dependsOn":[]} -->
# Correct wall-clock wording for the sim-scaled render delta

## Context

Camera timing documentation and comments call the per-render-frame delta "wall-clock"/"real time", but the value is simulation-scaled: `Engine/Source/GameBase.cpp:516-517` populates `mfLastRenderFrameSeconds` from `mTimeStep.WallToSim(mRenderTimer.GetDeltaNs(true))`, which applies the active time ratio (`Engine/Source/Frame/TimeStep.h:27-32`). A non-1.0 client time ratio is reachable (debug input `Projects/BrokenEngineSandbox/Source/Game.cpp:789-800`; server handling `Projects/BrokenEngineSandbox/Source/Network/Server/ServerSession.cpp:254-264`; client application `Projects/BrokenEngineSandbox/Source/Network/Client/ClientSession.cpp:96-115`).

The sim-scaled behavior is correct and deliberate: the render clock advances in sim seconds (`GameBase.cpp:371-418`, interpolation offset at `:568-570`), so camera easing must advance in the same units to stay in step with the interpolated player (`Players.cpp:481-503`). Supplying an unscaled delta would change camera pose under non-1.0 time ratios. The defect is the wording only. Confirmed by /external-deep-analysis Phase-3 verification (2026-08-07).

## Design

Reword every claim that these deltas are wall-clock/real time to state they are sim-scaled (wall delta times the active time ratio; identical to wall time at ratio 1.0, which is why the camera stays in step with the interpolated player). Retain all calculations, values, and ordering — zero behavior change.

## Critical files

- `Projects/BrokenEngineSandbox/Source/Graphics/Camera.cpp` — comment block at the top of `Camera::Update(const FrameInterpolate&)` (currently :72-80) and the velocity-from-position comment (currently :145)
- `Projects/BrokenEngineSandbox/Source/Graphics/AGENTS.md` — "Timing and Ownership" first bullet (currently :7)
- `Engine/Source/GameBase.cpp` — the stale wall-clock description near `mfLastRenderFrameSeconds` consumption guidance (currently :606-610)

## In scope

- Comment and documentation wording at the four regions above; nothing else.

## Out of scope

- Any change to `TimeStep`, `mfLastRenderFrameSeconds`, camera math, or timing behavior
- Other AGENTS.md content in the touched files

## Risk tier and invariants

Tier 1 — documentation/comment-only, no behavior or signature change. No determinism/CRC exposure (client render path, wording only).

## Acceptance criteria

- No remaining comment or AGENTS.md sentence describes `mfLastRenderFrameSeconds` or the camera update delta as wall-clock/real time; the sim-scaled contract and its player-tracking rationale are stated once at the owning site.
- Diff contains only comment/markdown bytes.

## Notes

- `Documents/Plans/Graphics/CameraUpdateDecomposition.md` relocates code around the Camera.cpp comment regions; the two plans are independently landable in either order (line numbers cited here are evidence, not anchors).
