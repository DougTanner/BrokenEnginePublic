# Audit: record-once passes that run unconditionally while their feature is disabled

## Context

Several record-once command-buffer passes run **every frame regardless of whether their feature is enabled or its
amplitude is at the disabled value**, because the engine bans CB re-record outside the swapchain / settings / device-loss
recreate paths (see `Engine/Source/Graphics/AGENTS.md`, "CB re-record is BANNED"). When a feature is "off," the only
cheap levers available are (1) writing `0` into an indirect-dispatch/draw count (the existing
`Pipeline::WriteIndirectBuffer(iCommandBuffer, 0)` idiom) and (2) in-shader early-outs. Recorded `vkCmdClearColorImage`,
`vkCmdFillBuffer`, `vkCmdUpdateBuffer`, layout transitions, and copies **cannot** be cheaply indirect-gated — skipping
them requires a destroy-tier re-record on the enable/disable *edge*, which is the expensive path.

**Precedent to document and respect: the shadow temporal pass + copy accepted the fixed cost rather than adding
edge-triggered re-record machinery.** Gating the shadow temporal-accumulation dispatch + the history
`vkCmdCopyImage` when shadows are disabled was considered and **rejected in favor of accepting the fixed cost**.
Reasoning: the CB re-record ban means gating-when-disabled needs a destroy-tier re-record fired on the
enable/disable transition (fragile, drains in-flight frames), and a recorded copy/dispatch can't be turned into a
cheap per-frame indirect no-op the way a draw count can. The measured cost was small enough that the complexity was
not worth it. **This audit's default expected outcome is "accept," matching that precedent, unless a specific pass
measures non-trivially.**

This plan is therefore fundamentally a **measure-then-decide audit**, not an implementation plan. It enumerates the
sibling passes that run-while-disabled, measures each at representative resolutions with the feature off, and records a
per-pass decision (accept fixed cost vs. gate on the enable/disable edge). Only the passes that measurement justifies
get gating; the rest are documented as accepted.

## Sites to measure (verify each at execution)

- **`Engine/Source/Graphics/Managers/CommandBufferRecordGlobal.cpp:269-445` — `RecordSmokeSpreadPipeline`** (closest
  structural twin). Runs unconditionally: two `vkCmdUpdateBuffer` active-tile resets, the dilate computes
  (`kPipelineSmokeOccupancyDilate` `:297`, `kPipelineSmokeOccupancyDilateRemap` `:396`), `vkCmdFillBuffer` occupancy
  clears (`:328`, `:427`), `vkCmdClearColorImage` on both smoke textures (`:347`, `:432`), the layout transitions, and
  the two indirect spread dispatches (`:355`, `:440`). When smoke is disabled, `SmokeUniforms.cpp:81-90` writes `0`
  into `kPipelineSmokeClearA/B`'s indirect count — but the dilates, fills, clears, and transitions above are **not**
  gated and still execute. Timer: `kGpuTimerSmokeSpread` (`ProfileManagerBase.h:98`).
- **`Engine/Source/Graphics/Managers/CommandBufferRecordGlobal.cpp:121-267` — `RecordWindSpreadPipeline`** (no indirect
  enable/disable gate at all). Runs unconditionally: `vkCmdUpdateBuffer` resets on both active-tile buffers (`:129-130`),
  both occupancy dilates (`kPipelineWindOccupancyDilateA/B` `:161-162`), `vkCmdFillBuffer` occupancy clears (`:215-216`),
  the layout transitions, and the two indirect spread dispatches (`:252`, `:262`). The wind enable/disable gate lives
  **only in-shader** (`WindSpreadOne/Two.comp` check `fWindTextureIndex` and early-return), so the dispatch still
  launches and the surrounding clears/dilates/resets always run. Timer: `kGpuTimerWindSpread` (`ProfileManagerBase.h:97`).
- **`Engine/Source/Graphics/Managers/CommandBufferRecordMain.cpp:158-178` — object-shadows render + blur.** The
  object-shadow render pass (`:158-165`, `kDynamicModelPipelineModelShadow` draws) and the separable blur
  (`kPipelineObjectShadowsBlurH/V` `:172`/`:175`) run every frame. The "disabled value" here is **not a static slider** —
  `fObjectShadowsIntensity` is a day-cycle blend
  (`GlobalUniforms.cpp:189-190`: `fDayPercent * gObjectShadowsNoon + (1-fDayPercent) * gObjectShadowsSunset`, then
  `*= pow(fDayPercent, 0.1)`), so it approaches 0 at night but is a continuous runtime value, **not** an on/off setting.
  That makes an edge-triggered re-record harder than for smoke/wind (there is no clean enable/disable transition to hang
  the re-record on — it would need a threshold latch with hysteresis). Note this explicitly in the per-pass decision.
  Timers: `kGpuTimerObjectShadows` (`ProfileManagerBase.h:109`), `kGpuTimerObjectShadowsBlur` (`:110`).
- **`Engine/Source/Graphics/Managers/CommandBufferRecordMain.cpp:130-156` — wind deposit passes A/B.** The two wind
  deposit render passes (`kDynamicPipelineWindDepositA/B` + `…AxisAlignedA/B`) run every frame with their layout
  transitions and `RecordBeginRenderPass`/`RecordEndRenderPass`, even when wind is disabled. The per-pipeline maps may
  be empty when nothing deposits (the inner `for` loops then do nothing), but the render-pass begin/end + transitions
  still execute. Timer: `kGpuTimerWindDeposit` (`ProfileManagerBase.h`, near the wind/smoke timers).

## Design

This is a **measure-then-decide** audit. For each site:

1. **Measure.** Read the relevant GPU timer (above) with the feature disabled, at representative resolutions
   (e.g. 1080p and 4K), parked and panning. Use the same profiling overlay the shadow Item-1 analysis used
   (`kGpuTimerShadow` / `kGpuTimerObjectShadows` etc.). Record absolute microseconds, not just "feels free."
2. **Decide per pass:**
   - **Accept fixed cost** (expected default, per the shadow precedent) — if the disabled-state cost is small relative
     to the frame budget. Document the measured number and the accept rationale in the plan/code comment so the next
     reader does not re-litigate it.
   - **Gate when disabled** — only if a pass measures non-trivially. Gating a recorded clear/dilate/dispatch requires a
     **destroy-tier re-record fired on the enable/disable edge** (the CB re-record ban forbids per-frame re-record).
     For smoke/wind that edge is the existing `sbSmoke`/`sbWind` toggle latch (`SmokeUniforms.cpp:50-55`,
     `WindUniforms.cpp:22-27`); for object-shadows there is **no** clean edge (day-cycle continuous), so gating it would
     need a new intensity-threshold latch with hysteresis — call out that this is strictly more work and higher risk
     than the smoke/wind case, and likely lands on "accept."
3. **Implement only what measurement justifies.** Do not pre-emptively gate everything; the point of the audit is to
   avoid adding re-record machinery for sub-microsecond savings.

## Out of scope

- The inline-area-mapping DRY cleanup (`Graphics/InlineAreaMappingDryToHelper.md`).
- The wind history-reset-on-recreate correctness item (`Graphics/WindHistoryResetOnRecreate.md`).
- The shadow temporal pass+copy itself — Item 1 of the prior plan already analyzed and **accepted** it; this audit only
  documents that precedent and applies the same methodology to the siblings.
- Any gating implementation beyond what the measurement step justifies. If everything measures "accept," the plan lands
  as a documented accept-with-numbers and no code change (and is removed from `Order.md` like any executed plan).
- The water-displacement indirect-dispatch perf work — that is the separate `Graphics/WaterDisplacementIndirectCompute.md`
  plan (different mechanism: it converts a constant direct dispatch to indirect, not gating-when-disabled).

## Acceptance criteria

- Each of the four sites has a recorded disabled-state GPU-timer measurement at ≥2 representative resolutions.
- Each site has a written per-pass decision: **accept** (with the measured cost and rationale) or **gate** (with the
  edge-trigger mechanism named).
- For object-shadows specifically, the decision explicitly addresses the day-cycle-continuous "disabled value" problem
  (no clean on/off edge) and why that pushes toward accept (or, if gated, how the threshold-latch + hysteresis is
  defined).
- Any implemented gating fires only on the destroy-tier re-record edge (never per-frame), drains in-flight frames per
  the existing `Destroy()` path, and is validated to not regress the enabled-state timer.
- The plan is removed from `Order.md` and deleted once every site has an accept/gate decision recorded (even if the net
  code change is zero).

## Critical files

- `Engine/Source/Graphics/Managers/CommandBufferRecordGlobal.cpp` (`RecordWindSpreadPipeline` `:121`,
  `RecordSmokeSpreadPipeline` `:269`)
- `Engine/Source/Graphics/Managers/CommandBufferRecordMain.cpp` (wind deposit `:130-156`, object-shadows render +
  blur `:158-178`)
- `Engine/Source/Graphics/Render/SmokeUniforms.cpp` (`sbSmoke` toggle latch `:50-55`; the `kPipelineSmokeClearA/B`
  indirect-count gate `:81-97` — the only currently-gated part)
- `Engine/Source/Graphics/Render/WindUniforms.cpp` (`sbWind` toggle latch `:22-27`)
- `Engine/Source/Graphics/Render/GlobalUniforms.cpp` (`fObjectShadowsIntensity` day-cycle derivation `:189-190` — why
  object-shadows has no clean enable/disable edge)
- `Engine/Source/Profile/ProfileManagerBase.h` (timers: `kGpuTimerShadow` `:93`, `kGpuTimerWindSpread` `:97`,
  `kGpuTimerSmokeSpread` `:98`, `kGpuTimerObjectShadows` `:109`, `kGpuTimerObjectShadowsBlur` `:110`)

## Notes

- The shadow Item-1 "accept" decision is the load-bearing precedent: the same CB-re-record-ban reasoning applies to
  every site here, so "accept" is the likely outcome for most. The audit's value is *measuring* rather than guessing,
  and *recording the numbers* so the question is settled.
- Effort 3 reflects the multi-pass measurement + per-pass design decision (not a single mechanical change); Risk 2
  reflects that any *implemented* gating touches record-once command buffers and needs a re-record on the edge.
- Object-shadows is the awkward case (continuous day-cycle intensity, no on/off toggle) and is the most likely to be
  documented-and-accepted rather than gated.
