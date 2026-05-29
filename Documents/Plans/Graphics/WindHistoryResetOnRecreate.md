# Wind: re-arm clean state on a Graphics device-lost / settings recreate

## Context

The Shadow Temporal-Accumulation follow-ups session landed `engine::gbShadowTemporalReset`
(`Engine/Source/Graphics/Render/Render.h:23`): a file-scope flag set by
`RenderTargetTextures::CreateShadowTextures` (`RenderTargetTextures.cpp:78`) and consumed in
`PopulateShadowParameters` (`GlobalUniforms.cpp:267-273`). A Graphics device-lost / settings recreate rebuilds
`mShadowHistoryTexture` with **undefined contents** while the function-local `static` latch
(`sbPreviousShadowAreaInitialized`, `sf4PreviousShadowArea`, `GlobalUniforms.cpp:265-266`) **survives** the recreate
(it is a TU-lifetime static, not tied to the texture). Without the reset flag, the first post-recreate frame would
EMA-blend stale/undefined history for one hard frame. The flag re-arms the first-frame guard so that frame blends
pure-current (`fShadowTemporalBlend = 1.0`) and re-seeds history.

**Wind has the analogous edge, but softer.** The wind ping-pong index `engine::giWindTextureIndex`
(`Render.h:36`, file-scope, survives recreate) is paired with `mWindTextureOne` / `mWindTextureTwo`, which
`RenderTargetTextures::CreateWindTextures` (`RenderTargetTextures.cpp:234-269`) recreates with **undefined contents**
on a recreate. `CreateWindTextures` does set `gbWindClear = true` (`:236`), but the wind clear path in
`RenderWindGlobal` (`WindUniforms.cpp:75-78`) **only flips the flag back off** — it does not clear the textures or
re-seed any latch:

```cpp
if (gbWindClear)
{
    gbWindClear = false;
}
```

The comment there (`WindUniforms.cpp:70-74`) is explicit: wind clear "relies on the decay/soft-clamp path inside
`WindSpread()` to zero stale content over a few frames." So a recreate leaves undefined garbage in the wind velocity
field that **decays out over several frames** (governed by `gWindDecayHigh/Low` and the soft clamp in
`WindSpreadCommon.h`) rather than being removed in one frame. This is a **soft multi-frame visual transient on a rare
path** (device-lost recovery or a graphics-settings change), not a one-frame hard pop like shadow's.

**Smoke, by contrast, is already fully mitigated.** `CreateSmokeTextures` (`RenderTargetTextures.cpp:168-232`) sets
`gbSmokeClear = true` (`:170`), and `RenderSmokeGlobal` (`SmokeUniforms.cpp:67-79`) on `gbSmokeClear`:
(a) re-seeds the previous-area latch (`f4SmokeArea = f4PreviousSmokeArea = f4CurrentSmokeArea`, `:71-73`), AND
(b) dispatches `kPipelineSmokeClearA` / `kPipelineSmokeClearB` via `WriteIndirectBuffer(..., 1)` (`:75-76`) to
**actually clear** the smoke textures on the GPU before the next spread reads them. Wind's clear path does neither.

## Design

Two candidate mitigations (pick one in the grill; wind's soft-decay makes this lower severity than the shadow case, so
the cheaper option is defensible):

- **(a) Hard GPU clear of the wind textures on recreate — mirror smoke's `gbSmokeClear`.** Reuse `gbWindClear`'s
  existing set site (`CreateWindTextures`, `:236`) but make `RenderWindGlobal`'s clear branch actually clear the two
  wind textures (a `vkCmdClearColorImage`, or a wind-clear compute pipeline mirroring `kPipelineSmokeClearA/B`) so the
  velocity field starts at zero instead of decaying garbage. Strongest fix; matches the smoke precedent exactly; costs
  a clear dispatch + the command-buffer plumbing for it (wind currently has no clear pipeline).
- **(b) Reset `giWindTextureIndex` + force a clean re-seed — mirror shadow's `gbShadowTemporalReset`.** On recreate,
  reset the ping-pong index to a known value and re-arm whatever latch the wind path keeps, so the first post-recreate
  frame does not read stale cross-frame state. Lighter weight (no new clear pipeline), but it does **not** remove the
  undefined texture contents — it only avoids reading them at a stale index; the soft decay still runs. Closer in
  spirit to the shadow reset (which also does not clear the texture, only re-arms the blend guard), and the shadow case
  proved that re-arming the guard is sufficient when the downstream pass tolerates one clean frame.

Recommended starting point for the grill: **(b)** is the closer structural twin to what just landed (shadow re-arms a
latch rather than clearing), is cheaper, and wind's existing soft-decay already masks the garbage — so a hard GPU clear
(option a) may be over-engineering for a rare path. But if the visual transient is objectionable in a playtest
(undefined velocities can briefly fling smoke/vegetation), escalate to (a). Decide in the grill against a captured
recreate (toggle a graphics setting that recreates wind textures and watch the field).

Name the reference patterns when implementing: shadow's `engine::gbShadowTemporalReset` (`Render.h:23`,
`GlobalUniforms.cpp:267-273`) and smoke's `engine::gbSmokeClear` (`Render.h:30`, `SmokeUniforms.cpp:67-79`).

## Out of scope

- Smoke (already fully mitigated via `gbSmokeClear` + `kPipelineSmokeClearA/B`).
- The inline-area-mapping DRY cleanup (`Graphics/InlineAreaMappingDryToHelper.md`).
- The disabled-pass gating perf audit (`Graphics/DisabledPassGatingPerfAudit.md`).
- Reworking the wind decay/soft-clamp model itself — this plan only addresses the recreate-edge garbage, not the
  steady-state decay tuning.

## Acceptance criteria

- After a Graphics device-lost or settings recreate that triggers `CreateWindTextures`, the wind velocity field does
  not display undefined garbage decaying over multiple frames — either it is hard-cleared (option a) or the first
  post-recreate frame reads a known-clean state at a reset index (option b).
- The chosen mechanism is wired off the same `gbWindClear` set site (`CreateWindTextures`, `RenderTargetTextures.cpp:236`)
  and consumed in `RenderWindGlobal` (`WindUniforms.cpp:75-78`), so the existing enable/disable-toggle path
  (`WindUniforms.cpp:22-27`) and the recreate path share one code route.
- The CB re-record ban is honored: any new clear dispatch is recorded once and gated per-frame via an indirect-dispatch
  count (mirror `kPipelineSmokeClearA/B`'s `WriteIndirectBuffer(iCommandBuffer, 0|1)` pattern), never a re-record.
- Playtest: toggle the graphics setting that recreates wind textures (and trigger a device-lost recovery if reachable)
  and confirm no visible wind/smoke/vegetation flick on the first frames after recovery.

## Critical files

- `Engine/Source/Graphics/Render/Render.h` (the `gbWindClear` / `giWindTextureIndex` / `gbSmokeClear` /
  `gbShadowTemporalReset` flags — `:23`, `:30`, `:35-36`)
- `Engine/Source/Graphics/Managers/RenderTargetTextures.cpp` (`CreateWindTextures` `:234`, `gbWindClear = true` `:236`;
  `CreateSmokeTextures` `:168` as the mitigated reference; `CreateShadowTextures` `:68`/`gbShadowTemporalReset` `:78`)
- `Engine/Source/Graphics/Render/WindUniforms.cpp` (`RenderWindGlobal`, the no-op clear branch `:75-78`, ping-pong
  toggle `:66`)
- `Engine/Source/Graphics/Render/SmokeUniforms.cpp` (`RenderSmokeGlobal` `gbSmokeClear` branch `:67-79` — the
  re-seed-latch + GPU-clear template)
- `Engine/Source/Graphics/Render/GlobalUniforms.cpp` (`PopulateShadowParameters` `:170`, the `gbShadowTemporalReset`
  consume + latch re-arm `:267-285` — the latch-reset template)
- If option (a): a new wind-clear pipeline mirroring `kPipelineSmokeClearA/B` (PipelineManager + the dispatch in
  `CommandBufferRecordGlobal::RecordWindSpreadPipeline`, `CommandBufferRecordGlobal.cpp:121`).

## Notes

- Severity is lower than the shadow case because wind garbage *soft-decays* (the comment at `WindUniforms.cpp:72-74`
  documents this as the intended-but-weak behavior), whereas shadow stale-history would show one hard EMA-blended
  frame. Hence the Impact-2 score, not 3.
- This is a correctness/robustness fix on a rare path (recreate), so it needs a playtest to validate — there is no
  unit-style check for "wind field looks right after device-lost." That drives the Risk-2 (touches wind runtime
  visuals).
- Wind reads (does not write) smoke's `f4SmokeArea` / `f4PreviousSmokeArea` per the global-pass ordering contract, so
  if option (a) adds a wind clear it must not disturb smoke's area latch — keep the clear confined to the wind textures.
