# Refactor: Render Sibling Function Decomposition

## Context

The predecessor plan `Refactor_RenderFunctionDecomposition.md` (which decomposed `PopulateShadowParameters` and `RenderFrameMain` in the Render directory) **explicitly exempted two sibling functions in `GlobalUniforms.cpp` by name as "flat or near-flat"** and deferred them. A `/next-plan` Step-6 sibling sweep disputed that label: both `PopulateSunAndLighting` and `PopulateWaterParameters` exceed the ~100-line guidance and carry genuine multi-branch control flow (not flat copy lists), and `PopulateWaterParameters` additionally owns moveable function-local statics. The user chose to revisit the exemption as its own follow-up plan rather than fold these into the executing predecessor.

This is a **move-only** decomposition: file-local `static` helpers in the same TU, **zero behavior / ordering / constant change**. The decomposition is the executor's judgement call per function — confirm each seam is truly move-only before extracting; if a function on closer inspection really is a flat copy list, drop it (don't force-extract).

Source TU: `Engine/Source/Graphics/Render/GlobalUniforms.cpp` (whole-file `#if defined(BT_CLIENT)`, client-render-only).

## Design

> Line citations below were verified against current source. They will drift once the predecessor `Refactor_RenderFunctionDecomposition.md` lands in the same TU — re-verify by symbol at execution (see Notes).

### `PopulateSunAndLighting` (`GlobalUniforms.cpp:52`, ~117 lines)

Three stages, mirroring how `PopulateShadowParameters` was split by the predecessor plan:

1. **Sun/moon direction + tilt** (`:54-63`) — builds `vecSunMoonNormal` via Y-rotation + X-tilt, stores `f4SunMoonNormal` (with `w = fSunAngle`). Self-contained; reads only `fSunAngle` and `gSunMoonNormalTilt`.
2. **Piecewise day-cycle color/ambient ramp** (`:65-146`) — a real 6-branch `if`/`else-if` chain over `fSunAngle` (morning→noon→evening→night→pre-morning + the `DEBUG_BREAK()` default), then sun/moon/ambient/intensity stores. **Caveat:** this block shares several local `XMVECTOR` color constants across the chain (`vecSunMorning`/`vecSunNoon`/`vecSunEvening`/`vecMidnight` and their ambient counterparts, plus `vecMoonFloor`, `:68-77`). When extracted, those constants become helper params or helper-locals — keep them single-sourced so no value drifts. `ComputeMoonAmount` (already a file-local helper) stays as-is.
3. **Noon-percent / day-percent feather windows** (`:148-167`) — writes the `rfNoonPercent` / `rfDayPercent` out-params via two small piecewise windows. Self-contained; reads only `fSunAngle`.

### `PopulateWaterParameters` (`GlobalUniforms.cpp:485`, ~143 lines)

Primary seam (clean, self-contained):

- **Camera-relative double-precision UV-reduction block** (`:536-606`, ~70 lines) — owns four function-local statics (`sdReducedTimeOne` / `sdReducedTimeTwo` / `sdReducedTimeThree` / `sfPrevElapsedTime`, `:555-558`), two local lambdas (`RotatedCamera` + the per-frame reduced-time integration), and writes only the `fWaterReducedNormal*` / `fWaterReducedNoiseOrigin*` fields. **The statics move with the block** (matches the predecessor plan's "function-local static latches move with their block" pattern), preserving the single call site so the per-frame integration latch keeps its once-per-frame contract.

Secondary smaller seams (extract if cleanly move-only, otherwise leave inline):

- **Sunset-fade branch** (`:486-498`) — 3-way piecewise over `fSunAngle` writing `fWaterDepthLutSunsetFade`, with the trailing `pow`-shaping (`:498`).
- **Directional branch** (`:509-521`) — 3-way piecewise over `fSunAngle` writing `fWaterDirectional`, with the trailing square (`:521`).

## Critical files

- `Engine/Source/Graphics/Render/GlobalUniforms.cpp` — the only file edited; new helpers are file-local `static` functions in the existing `namespace engine` (no header changes).

## Out of scope

- **Any behavior / ordering / constant change** — pure intra-file extraction; the populated `GlobalLayout` bytes are identical every frame.
- `PopulateShadowParameters` and `RenderFrameMain` — already handled by the predecessor `Refactor_RenderFunctionDecomposition.md`.
- `PopulateLightingParameters` (~79 lines) — under the ~100-line threshold; not in scope.
- `RenderLightingMain` — flat; not in scope.
- Header / public-API changes, `Render.h` globals, cross-TU promotion of any helper — all stay file-local.

## Acceptance criteria

- `GlobalUniforms.cpp` compiles in the client build with no behavior change.
- The four `PopulateWaterParameters` function-local statics live with the extracted UV-reduction helper and are still touched exactly once per frame (single call site preserved).
- No new symbols added to `Render.h` or any header; no constant values changed.

## Notes

- **No determinism / CRC / network / `kiVersion` / `.pack`-layout exposure** — render-side, move-only, client-only (`BT_CLIENT`). The functions populate host-visible GPU uniform buffers only; nothing here feeds sim state or replays.
- **Sequencing:** land this **after or co-scheduled with** `Refactor_RenderFunctionDecomposition.md`, since both edit `GlobalUniforms.cpp` (the predecessor splits `PopulateShadowParameters`/`RenderFrameMain` in the same TU; landing this first would leave the predecessor's line citations stale, and vice-versa). At execution, re-verify all line citations by symbol — they will have drifted from the predecessor's edits.
- The decomposition is the executor's judgement call per function: confirm each seam is genuinely move-only before extracting; if a function on closer inspection is a flat copy list, drop it rather than force-extract.
