# Architecture: Render Region Ownership & Dead Stores

## Context

Source: /external-architecture-review on `Engine/Source/Graphics/Render` (non-recursive). `Render/CLAUDE.md:5`'s
"each file owns its region" contract is blurred by vestigial cross-region dead stores inside `GlobalUniforms.cpp`,
one dead uniform write no shader reads, lighting fields populated from the terrain function, the Lighting region
split across two files, and a clamp asymmetry across the documented ownership exception's two writers.

## Design

### Engine/Source/Graphics/Render/GlobalUniforms.cpp — dead cross-region stores [~5m]
- `PopulateShadowParameters` writes `rGlobalLayout.fWaterReducedNoiseOriginX = 0.0f` (`:225`) — a Water field,
  unconditionally overwritten by `PopulateWaterParameters` (`:603`), which runs later in the same
  `RenderFrameGlobal` call (`:634` after `:632`). Delete the line.
- `PopulateTerrainParameters` zeroes `fWaterReducedNormalOriginX/Y` and `fWaterReducedNormalOriginTwoX/Y`
  (`:434-437`) — Water fields, unconditionally overwritten at `:586-590`. Delete the four lines.

### GlobalUniforms.cpp — dead `f4SunMoonNormal.w` write [~5m]
- `PopulateSunAndLighting` stashes the raw sun angle into `rGlobalLayout.f4SunMoonNormal.w` (`:63`). Repo-wide
  shader grep found no `.w` consumer (all GLSL reads are `.xyz`/`.xy`: `Water.frag:189,211`, `Terrain.frag:112`,
  `Model.frag:251`, `WaterSkyboxOne.frag:145`, `ShaderFunctions.h:41,80`). Delete the line; the preceding
  `XMStoreFloat4` (`:62`) leaves `w = 0.0` — correct for a direction per the root-CLAUDE.md W invariant. This also
  makes `Render/CLAUDE.md:22`'s "only resolved floats reach the shader" claim unconditionally true.

### GlobalUniforms.cpp — time-of-day lighting fields in the terrain function [~5m]
- `PopulateTerrainParameters` ends by writing `fLightingTimeOfDayMultiplier` / `fLightingWaterSkyboxOne`
  (`:458-459`, under a `// Time of day` comment) — day-cycle-derived Lighting fields. Move the two writes to the
  end of `PopulateSunAndLighting` (`:52`), right after it derives `rfDayPercent` (`:159-167`) — that is where every
  other day-cycle product resolves (`Render/CLAUDE.md:22`) and it needs no new parameter. Do NOT move them into
  `PopulateLightingParameters`: with the consolidation below, that function runs inside `RenderLightingGlobal`
  (`RenderFrameGlobal:609`) *before* `PopulateSunAndLighting` (`:630`) computes `fDayPercent`.

### Lighting region split across two files — consolidation decision [~15m]
- Lighting GlobalLayout population is the only region spanning two files: world-area/temporal/tile/readout
  population lives in `GlobalUniforms.cpp`'s `PopulateLightingParameters` (`:346-424`), while all other lighting
  population lives in `LightingUniforms.cpp`'s `RenderLightingGlobal` (`:14-95`). Shadow, by contrast, is fully
  self-contained in `GlobalUniforms.cpp`. Move `PopulateLightingParameters` (with its function-local statics and
  the `gbLightingTemporalReset` consume) into `LightingUniforms.cpp` and call it from `RenderLightingGlobal` —
  verified free of ordering hazards: it reads only `gpTextureManager`/`gpSwapchainManager`/`game::gpCamera` plus
  wrappers (`gFov`, `gLightingTemporalBlend`, `gSpreadPassCount`), reads no GlobalLayout field, and writes only its
  own GlobalLayout fields (`f4LightingArea`/`f4LightingAreaPrevious`/`fLightingTemporalBlend`/`uiLightTilesX/Y`)
  plus the `giLighting*` Profile readouts, so running it inside `RenderLightingGlobal` (`RenderFrameGlobal:609`,
  before the current `:631` call site) changes no observable state ordering. Single-call-site static latches move
  intact. `LightingUniforms.cpp` already provides every wrapper symbol the function needs (`LightingWrappersBase.h`;
  `gFov` via the `WrapperBase.h` each wrapper header includes) — coordinate with
  `Architecture_RenderIncludeHygiene.md` if its include edits land in the same session.

### Engine/Source/Graphics/Render/MainUniforms.cpp — clamp asymmetry across the ownership exception [~5m]
- The documented ownership exception pairs `GlobalUniforms.cpp:530`'s count write (clamped:
  `std::min(gWaterLowCount.Get<int64_t>(), gWaterLowMax.Get())`) with `MainUniforms.cpp`'s array fill, but the fill
  loop bound uses unclamped `gWaterLowCount.Get<int64_t>()` (`:294`, loop `:316`). Today only wrapper ranges keep
  the fill inside `pf4LowWavesOne[256]` (`ShaderLayoutsBase.h:452`): `gWaterLowCount`'s allowed set tops out at 255
  and `gWaterLowMax` spans [0, 255] (`WaterWrappersBase.cpp:70-71`) — overflow is not currently possible, so this
  is lockstep hygiene, not a live bug. Apply the same `std::min(..., gWaterLowMax.Get())` clamp to the local
  `iCount` so both writers share the bound. (Medium has no max wrapper — both sites symmetric, no change.)

## Critical files
- `Engine/Source/Graphics/Render/GlobalUniforms.cpp`
- `Engine/Source/Graphics/Render/LightingUniforms.cpp`
- `Engine/Source/Graphics/Render/MainUniforms.cpp`
- `Engine/Source/Graphics/Render/CLAUDE.md` (region-ownership prose if the consolidation lands)

## Out of scope
- The documented ownership exception itself (`MainUniforms.cpp` zeroing `iWaterLowCount`/`iWaterMediumCount` when
  amplitude fades clamp to zero) — verified correctly co-gated; stays.
- The Render.h sub-entry-point surface (`RenderLightingGlobal/Main` etc. having no external callers) — cost/benefit
  marginal since Render.h is also the documented home of the cross-file globals; not worth a build-graph churn.
- The hex-shield tunables living game-side while the collection/shader/layout are engine-side
  (`MainUniforms.cpp:385-397`) — root cause outside this directory; sanctioned read direction.
- The wind recreate gap (`Graphics/WindHistoryResetOnRecreate.md`) and the empty-active-coords early return
  (`Graphics/Architecture_EmptyCoordsStaleIndirectCounts.md`) — existing plans.

## Acceptance criteria
- No function writes another region's GlobalLayout fields except the documented `MainUniforms.cpp` ownership
  exception; the dead stores and the `.w` write are gone; both water-count writers clamp identically; client builds
  clean and renders identically.

## Notes
- No determinism/CRC/network/`kiVersion` exposure — render-side uniform population only; all deletions verified
  dead (overwritten later in the same frame, or never read by any shader).
- One grill decision: land the `PopulateLightingParameters` relocation (recommended — restores one-file-per-region)
  or keep it in `GlobalUniforms.cpp` with only the dead-store/clamp fixes.

## Verification Notes

Verified 2026-06-11 against current source.
- Dead stores confirmed: `:225` (`fWaterReducedNoiseOriginX`) unconditionally overwritten at `:603`; `:434-437`
  (`fWaterReducedNormalOrigin*`) unconditionally overwritten at `:586-590`; `PopulateWaterParameters` has no early
  returns; both writers run inside the same `RenderFrameGlobal` invocation (`:632`→`:633`→`:634`) before the global
  submit, so no GPU read can observe the intermediate values.
- `f4SunMoonNormal.w` dead write re-verified adversarially: repo-wide grep (engine + game shaders + C++) finds only
  swizzled `.xy`/`.xyz` reads (`Water.frag:189,211`, `Terrain.frag:112`, `Model.frag:251`,
  `WaterSkyboxOne.frag:145`, `ShaderFunctions.h:41,80`) — no `.w` read, no whole-`vec4` consumer, no CPU readback,
  no game-side shader references. `XMVector4Normalize` of the rotated `(1,0,0,0)` direction leaves `w == 0.0`
  after the `:63` deletion — correct for a direction per the root-CLAUDE.md W invariant.
- **Corrected an internal conflict**: the time-of-day item originally targeted `PopulateLightingParameters`, but
  the consolidation item moves that function into `RenderLightingGlobal`, which runs (`RenderFrameGlobal:609`)
  before `fDayPercent` exists (`PopulateSunAndLighting`, `:630`). Destination rewritten to the end of
  `PopulateSunAndLighting`, where `rfDayPercent` is derived (`:159-167`) — no new parameters, consistent with the
  day-cycle section of `Render/CLAUDE.md:22`.
- Relocation safety re-verified by scanning `:346-424`: no read of any GlobalLayout field, no dependence on the
  other `Populate*` calls; reads-list tightened to include the three wrappers; `gbLightingTemporalReset` consume is
  position-independent within the frame.
- Clamp asymmetry concretized: overflow impossible today (`gWaterLowCount` allowed max 255 < `pf4LowWavesOne[256]`);
  item retained as lockstep hygiene with the bound stated.
- No items dropped; no score adjustment suggested (the time-of-day fix changes destination, not scope).
