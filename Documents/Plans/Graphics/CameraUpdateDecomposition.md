<!-- broken-engine-plan/v1 {"createdUtc":"2026-08-07T13:08:47.888Z","dependsOn":[]} -->
# Decompose Camera::Update into stage member functions

## Context

`Camera::Update(const FrameInterpolate&)` (`Projects/BrokenEngineSandbox/Source/Graphics/Camera.cpp:70-310`) is a 240-line function at cyclomatic complexity 32 — the file's entire structural-erosion hot mass (Phase-0 metrics 2026-08-07: file 0.931 vs corpus 0.553). It executes sequential, state-disjoint stages communicating only through named `Camera` members plus two locals (`fDeltaTime`, `vecTargetPosition`). Stage boundaries and their ordering dependencies were source-verified by /external-deep-analysis Phase-3 review: jump tracking reads pre-zoom `mfCameraEyeHeight` (:227-232), zoom writes it (:237-279), and the finishing stage consumes the new height (:281-299), so preserving statement order preserves semantics.

## Design

Pure code motion into private stage member functions in the same TU, no statement reordering, no DirectXMath call or operand changes, W-component conventions and member write order identical:

- `XMVECTOR ResolveTargetPosition(const FrameInterpolate& rFrameInterpolate, bool bFreeCameraActive)` — currently :119-188, with the player-not-found diagnostic block (currently :158-176) extracted into its own helper (mandatory: leaving it inline keeps ResolveTargetPosition above complexity ten).
- `void UpdateJump(FXMVECTOR vecTargetPosition, float fDeltaTime)` — currently :190-235.
- `void UpdateEyeHeight(float fDeltaTime)` — currently :237-279. `fEyeHeightBeforeZoom` (currently :237) stays captured by the caller before this call because the inline lighting-reset comparison (currently :284-287) consumes it.
- The preamble (time/shake/sun-angle/free-camera, currently :76-117) and the finishing sequence (texel references, pose, vibration, matrices, persistence, currently :281-309) stay inline in `Update`.

Declarations are added to the `Camera` class in `Camera.h`; the helpers are multi-statement, invariant-coupled mutations permitted by style rule 49 (`Documents/C++StyleGuide.txt:193-196`).

## Critical files

- `Projects/BrokenEngineSandbox/Source/Graphics/Camera.cpp`
- `Projects/BrokenEngineSandbox/Source/Graphics/Camera.h` — new member function declarations only

## In scope

- The stage extractions above inside `Camera::Update(const FrameInterpolate&)` and the matching declarations in `Camera.h`
- Moving each stage's existing comments with its code

## Out of scope

- Any behavior, math, constant, logging, or comment-content change; any statement reordering
- The lighting temporal reset block (currently :284-287) beyond keeping it inline — owned by `Documents/Plans/Graphics/LightingTemporalOutwardZoomReset.md`
- `Update(const Frame&)` forwarding overload; `SunAngle`; file-scope helpers

## Risk tier and invariants

Tier 1 — local behavior-preserving decomposition, no public-surface or invariant exposure; client-only render path outside the CRC. DirectXMath function-form and W conventions must survive byte-for-byte in moved code.

## Acceptance criteria

- No function resulting from the decomposition exceeds cyclomatic complexity ten (advisory metric adopted as this plan's criterion; re-run the Phase-0 snapshot: `pwsh -NoProfile -File .agents/skills/code-quality-metrics/scripts/Invoke-CodeQualityMetrics.ps1 -Mode Snapshot -Target Projects/BrokenEngineSandbox/Source/Graphics/Camera.cpp -Scope Exact ... -Phase0Hints` and confirm the highComplexityFunctions hint is gone).
- Client compiles; a harness zoom+jump scenario shows bit-identical camera pose before/after (same input sequence, compare captured eye position/height).

## Notes

- `Documents/Plans/Documentation/CameraTimingSimScaledWording.md` rewords comments in the same function; independently landable in either order.
