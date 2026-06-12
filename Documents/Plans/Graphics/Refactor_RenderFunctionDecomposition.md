# Refactor: Render Function Decomposition

## Context

Source: /external-refactor-clean on `Engine/Source/Graphics/Render` (non-recursive). Two functions exceed the
~100-line guidance with clean extraction seams; every extraction is a move-only relocation to file-local `static`
helpers in the same TU — zero behavior change. The directory's other long function (`RenderLightingMain`,
`LightingUniforms.cpp:97-202`, ~105 lines) is exempt by inspection: it is a flat slider-copy list with no control
flow, where decomposition adds indirection without reducing complexity.

## Design

### Engine/Source/Graphics/Render/GlobalUniforms.cpp — `PopulateShadowParameters` (`:170-344`, ~174 lines) [~30m]
Three stages behind clean seams (one shared-data caveat: the shadow-area block derives `fWorldTexelX` and
`fFullWidth` (`:249`, `:251`) that the sun-direction extension also consumes (`:307`, `:312`, `:331`) — have the
second helper out-param/return them, or pass the pair into the third; everything else is independent):
- Extract the sunrise/sunset stretch envelope (`:193-223`, the six `kf*Stretch*` constants + the five-way
  angle-window chain writing `fShadowSunriseStretch`/`fShadowSunsetStretch`) into a file-local helper.
- Extract the shadow-area + temporal-latch + visible-window block (`:237-301`, from the texel-size derivation
  through `giShadowActivePixelsX/Y`) into a file-local helper. The function-local statics
  (`sbPreviousShadowAreaInitialized`/`sf4PreviousShadowArea`, `:265-266`) move with their block — still a single
  call site, so the once-per-frame latch contract (`Render/CLAUDE.md:51`) is preserved.
- Extract the sun-direction extension (`:303-338`, the `f4ShadowAreaExtra` / march-direction branch) into a
  file-local helper.

### Engine/Source/Graphics/Render/MainUniforms.cpp — `RenderFrameMain` (`:161-398`, ~237 lines) [~30m]
- Extract the Gerstner wave packing (`:267-382`: wave-time/camera setup, both amplitude fades, and the low/medium
  wave loops, including the `iWaterLowCount`/`iWaterMediumCount` zeroing of the documented ownership exception)
  into a file-local helper taking `rMainLayout`, `rGlobalLayout`, and the camera-eye-height. Keeping the count
  zeroing and the array fill inside one extracted function preserves the co-gating mandate
  (`Render/CLAUDE.md:9`).
- Extract the hex-shield flat copies (`:384-397`) into a file-local helper. Optional: the camera-shake/matrix block
  (`:249-265`) — extract only if the remaining body still reads poorly.

## Critical files
- `Engine/Source/Graphics/Render/GlobalUniforms.cpp`
- `Engine/Source/Graphics/Render/MainUniforms.cpp`

## Out of scope
- `RenderLightingMain` / `PopulateSunAndLighting` / `PopulateWaterParameters` — flat or near-flat bodies under or
  near the guidance; extraction adds hops without removing complexity.
- Any behavior, ordering, or constant change — extractions are move-only; `static` latches keep single call sites.
- The dead stores and field moves in the same functions — owned by
  `Graphics/Architecture_RenderRegionOwnership.md` (land that first; see Order.md dependency).

## Acceptance criteria
- `PopulateShadowParameters` and `RenderFrameMain` each read as a linear sequence of named stage calls; client
  builds clean; rendering identical.

## Notes
- No determinism/CRC/network/`kiVersion` exposure — render-side, move-only.
- Order against the other Render plans: land after `Architecture_RenderRegionOwnership.md` and
  `Architecture_RenderSharedConstants.md` (both edit lines inside the blocks being moved); co-schedule in one
  session so citations stay fresh.

## Verification Notes

Verified 2026-06-11 against current source; all line ranges exact, no drops.
- `PopulateShadowParameters` `:170-344` (~174 lines) and `RenderFrameMain` `:161-398` (~237 lines) confirmed; the
  exempted `RenderLightingMain` is `:97-202` (~105 lines) and is a flat slider-copy list as claimed.
- Stage ranges confirmed: stretch envelope `:193-223` (six `kf*Stretch*` constants `:193-199`, five-way chain
  `:201-220`, writes `:222-223`); shadow-area/latch/visible-window `:237-301` (statics `:265-266`, latch contract
  `Render/CLAUDE.md:51`); sun-direction extension `:303-338`. **Caveat added in place**: stages 2 and 3 share
  `fWorldTexelX`/`fFullWidth`, so the extraction is parameter-passing, not fully independent — design wording
  tightened so the executor doesn't rediscover this mid-edit.
- Gerstner extraction `:267-382` confirmed to fully contain both amplitude fades (`:278-285`) and both count
  zeroings (`:290`, `:354`), preserving the `Render/CLAUDE.md:9` co-gating mandate; hex-shield copies `:384-397`
  and optional camera-shake/matrix block `:249-265` confirmed.
- No score adjustment suggested.
