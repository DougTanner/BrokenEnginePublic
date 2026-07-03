# Architecture: Shadow/Lighting Uniform Population Dedup

## Context
Source: /external-architecture-review on Engine/Source (recursive). The Render/ uniforms directory's biggest drift surface: the world-sized-texel area-snapping math and the temporal previous-area/reset latch are duplicated token-identically between the Shadow (GlobalUniforms) and Lighting (LightingUniforms) populate paths, with a third partial latch variant in SmokeUniforms. Drift evidence already exists: tile-count derivation disagrees between the copies (floor+min-1 vs ceil).

## Design

### Shared world-sized-texel area helper
- `GlobalUniforms.cpp:239-251` vs `LightingUniforms.cpp:30-42` are token-identical (`fTanHalfFov` → `fWorldTexelX/Y` → floor-to-texel → `f4*Area`) except headroom constant / texel-eye-height / target field; the live-visible-window pair also recurs (`GlobalUniforms.cpp:282-283` vs `LightingUniforms.cpp:81-82`). Extract `ComputeWorldSizedTexelArea(...)` parameterized on the varying inputs; both sites call it [~45m]

### Shared temporal-area latch
- `GlobalUniforms.cpp:257-277` vs `LightingUniforms.cpp:48-68` (verbatim modulo Shadow/Lighting names), third partial variant `SmokeUniforms.cpp:59-65`. Extract a small `TemporalAreaLatch` struct (previous-area storage + reset-flag re-arm + blend-window logic), one instance per subsystem [~45m]
- While consolidating, resolve the tile-count derivation divergence deliberately: `LightingUniforms.cpp:71-72` floors (with a `std::max(1u, ...)` floor-of-1) while `SmokeUniforms.cpp:37-38` / `WindUniforms.cpp:54-55` ceil — document or unify. Note the lighting floor is internally consistent with the occupancy-buffer sizing floor (`BufferManager.cpp:660-661`), and `uiLightTilesX/Y` also feed `LightingDepositEdgeFade` (`ShaderFunctions.h:257`), so a switch to ceil slightly shifts the edge-fade denominator; `Architecture_LightingOccupancyRemoval.md` keeps these uniforms (edge-fade readers) but may delete the occupancy buffer they were sized against — resolve the two plans' floor/ceil answer together [~15m]

## Critical files
- `Engine/Source/Graphics/Render/GlobalUniforms.cpp`, `LightingUniforms.cpp`, `SmokeUniforms.cpp`
- A shared home for the helpers (`Render/Render.h` or a small `RenderAreaUtils` header)

## Out of scope
- GPU dispatch windowing (`Graphics/WindowedLightingShadowDispatch.md` — adjacent but distinct: that plan gates dispatch, this one dedups CPU population math)
- The occupancy-grid decision (`Graphics/Architecture_LightingOccupancyRemoval.md`)
- The Gerstner bank recompute and other per-file quick wins (`Graphics/Refactor_RenderUniformsQuickWins.md`)

## Notes
- Invariant exposure: client/graphics-only, render-path CPU math; no determinism/CRC/wire. Behavior-preserving extraction — the helper must reproduce each site's exact float sequence (values are visible as shadow/lighting crop windows; a texel of drift shows as edge shimmer). `WindowedLightingShadowDispatch` cites both files — land after it or refresh its citations; never interleave
- Grill decision: helper home (Render.h vs new header) — trivial; and whether the floor-vs-ceil tile divergence is deliberate (ask before unifying)

## Verification Notes
- Duplication claims re-verified 2026-07-02: `GlobalUniforms.cpp:239-251` vs `LightingUniforms.cpp:30-42` are token-identical modulo headroom constant / texel-eye-height / target field; the latches `GlobalUniforms.cpp:257-277` vs `LightingUniforms.cpp:48-68` are verbatim modulo Shadow/Lighting names; the visible-window pair `GlobalUniforms.cpp:282-283` vs `LightingUniforms.cpp:81-82` recurs as claimed.
- The `SmokeUniforms.cpp:59-65` latch is only partially analogous (no blend weight, no reset-flag re-arm — clear-gated via `gbSmokeClear`, and smoke's area follows the visible area rather than a world-sized-texel grid); folding smoke into `TemporalAreaLatch` is optional — skip it if it doesn't fit without special-casing.
- Co-schedule with `Graphics/WindowedLightingShadowDispatch.md` per Notes: it rewrites the same `GlobalUniforms.cpp` shadow region (indirect dispatch counts) — land it first or refresh citations; never interleave.
