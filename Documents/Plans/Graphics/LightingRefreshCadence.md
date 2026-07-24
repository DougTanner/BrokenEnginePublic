<!-- broken-engine-plan/v1 {"createdUtc":"2026-07-21T23:23:30.000Z","dependsOn":[]} -->
# Slider-gated refresh cadence for the lighting spread → combine → temporal chain

## Context

The lighting spread empty-deposit skip shipped: the 40 lighting spread draws are host-visible indirect
draws, and `engine::RenderLightingSpreadIndirect` writes `instanceCount = 0` on frames whose light deposit
is empty. Measured effect (Debug, 1600×904): idle `kGpuTimerLightingSpread` **645 µs → 84–92 µs**, idle
spread render targets byte-identical (SHA-256) to the pre-change build, Vulkan validation clean across
three pipeline recreates and two resizes.

That win is **content gating** and is confined to the idle case: the moment any light deposits, spread
returns to full cost. This plan owns the residual **rate gating** item: refresh the spread → combine →
temporal chain every N frames (N from a new debug slider, default 1 = off) and freeze the combine textures
at the last refreshed lighting on skip frames. Unlike content gating, rate gating also pays off in combat,
where the deposit is never empty.

### Mechanism already in place (verified against current source)

- `Engine/Source/Graphics/Render/Render.h:98` — `inline int64_t giLightingDepositInstances = 0;` (reset at
  the top of `RenderFrameMain`, `MainUniforms.cpp:447`; accumulated by the three deposit `EndRender`
  writers).
- `Engine/Source/Graphics/Render/Render.h:101` — `void RenderLightingSpreadIndirect(int64_t iCommandBuffer);`,
  implemented at `LightingUniforms.cpp:349`.
- `Engine/Source/Graphics/Render/MainUniforms.cpp:483` **and `:553`** — the **two** call sites, each
  immediately after `game::FrameInterpolate::EndRender(iCommandBuffer)`.
- Indirect-compute plumbing all ships; no plumbing work is needed:
  - `Engine/Source/Graphics/Objects/Pipeline.cpp:292` — `Pipeline::RecordComputeIndirect(int64_t iCommandBuffer, VkCommandBuffer vkCommandBuffer, const XMFLOAT4& f4PushConstants)`.
    Note its contract: with `kPushConstants` it asserts `mInfo.iPushConstantBytes == 0` (fixed `XMFLOAT4`
    push form only).
  - `Engine/Source/Graphics/Objects/Pipeline.cpp:344` — `Pipeline::WriteIndirectComputeBuffer(int64_t iCommandBuffer, int64_t iGroupCountX, int64_t iGroupCountY, int64_t iGroupCountZ)`.
  - `Engine/Source/Graphics/Objects/PipelineCreator.cpp:685-694` — `CreateComputePipeline`'s
    `kIndirectHostVisible` branch allocates the real per-framebuffer host-visible
    `VkDispatchIndirectCommand` buffer.
  - Live consumer precedent: `kPipelineWaterDisplacement` sets `.flags = {kCompute, kIndirectHostVisible}`
    (`PipelineManager.cpp:462`, `Create` call at `:459`) and writes its group counts at
    `MainUniforms.cpp:517`.

So this plan only converts the two lighting compute dispatches to indirect and wires the cadence.

## Design

Add a frame counter and a refresh predicate; on skip frames write zero indirect work for the spread,
combine, and temporal passes and hold the published combine data at the prior refresh's output.

### Refresh gate

- **Skip frame**: write zero spread draw instances and zero indirect groups for combine and temporal; do
  not advance the temporal area latch. Deposit continues every frame (cheap). The combine textures hold the
  prior refresh's output. The next refresh clears and rebuilds the active chain (every spread pass begins
  with `LOAD_OP_CLEAR`), so deposits made during skip frames are intentionally unconsumed/dropped.
  The recorded history-copy `vkCmdCopyImage` sequence (`CommandBufferRecordMain.cpp:222-234`) cannot be
  gated without a CB re-record (see the existing comment at `:198-204`); it still executes on skip frames,
  re-copying the held combine content into history — idempotent and self-consistent. After
  `WindowedLightingDispatch.md` lands, its `LightingHistoryCopy.comp` joins the same cached predicate and
  skip frames also write zero history-copy groups.
- **Refresh frame**: everything normal, including the existing empty-deposit spread instance gate.
- Cadence 1 (default) refreshes every frame: spread instance gating, combine, and temporal all behave
  exactly as today, so the default build's output is bit-identical to the current one. Once
  `WindowedLightingDispatch.md` lands, cadence 1 still performs every refresh, but a refresh runs every
  bounded spread pass even with empty deposit so its current rectangle is overwritten.
- Establish the predicate in `RenderLightingGlobal` (`LightingUniforms.cpp:91`) before the
  `PopulateLightingParameters` call at `:195`: advance a local static frame counter, read
  `gLightingUpdateCadence`, force a refresh when `gbLightingTemporalReset` is pending, and cache the result
  in a new `Render.h` inline global (suggested `gbLightingRefreshFrame`, declared alongside
  `giLightingDepositInstances` at `Render.h:98`). `RenderLightingSpreadIndirect` consumes the cached value
  after deposit accumulation, which keeps both `MainUniforms.cpp:483` and `:553` call sites aligned without
  making that function the latch owner.

### Dispatch conversion

Convert the two direct compute dispatches inside `CommandBufferRecordMain::RecordLightingSpreadPipeline`
(defined at `CommandBufferRecordMain.cpp:109`):

- Phase 2 combine — `CommandBufferRecordMain.cpp:193`:
  `vkCmdDispatch(vkCommandBuffer, uiCombineGroupsX, uiCombineGroupsY, 1);`
  `mCombinePipeline` uses a sized push block (`shaders::CombinePushConstantsLayout`,
  `iPushConstantBytes != 0`), which `RecordComputeIndirect`'s push path asserts against — so keep the
  existing manual bind/descriptor/push-constant sequence at `:176-192` unchanged (no shader edit) and
  replace only the `vkCmdDispatch` with an indirect dispatch against the pipeline's per-framebuffer slot,
  mirroring `RecordComputeIndirect`'s slot math (`Pipeline.cpp:302-309`: offset
  `iCommandBuffer * sizeof(VkDispatchIndirectCommand)` into `mIndirectVkBuffer`).
- Phase 3 temporal — `CommandBufferRecordMain.cpp:219`:
  `gpPipelineManager->mLightingTemporalPipeline.RecordCompute(iCommandBuffer, vkCommandBuffer, uiTemporalGroupsX, uiTemporalGroupsY);`
  becomes `RecordComputeIndirect(iCommandBuffer, vkCommandBuffer)` — a drop-in (no push constants).

Both pipelines gain `kIndirectHostVisible` alongside their current flags in
`PipelineManager::CreateLightingPipelines` (`mCombinePipeline` `.flags` at `PipelineManager.cpp:228`,
`mLightingTemporalPipeline` `.flags` at `:249`), following the `kPipelineWaterDisplacement` precedent
(`:462`). The group counts these calls currently compute inline at record time move to the populate path
as `WriteIndirectComputeBuffer` arguments: `RenderLightingSpreadIndirect` writes, per frame, refresh →
the same tile-rounded counts over the combine extent the record path computes today, skip → `0, 0, 0`.

### Temporal latch

`f4LightingAreaPrevious` must advance **only on refresh frames**, so reprojection maps into the area the
history was actually rendered with (matters while the texel ramp rescales during a zoom). The latch update
lives in `PopulateLightingParameters` at `LightingUniforms.cpp:60-61`:

```
static TemporalAreaLatch sTemporalAreaLatch {};
rGlobalLayout.fLightingTemporalBlend = sTemporalAreaLatch.Update(rGlobalLayout.f4LightingArea, gbLightingTemporalReset, gLightingTemporalBlend.Get(), rGlobalLayout.f4LightingAreaPrevious);
```

Gate this on the cached refresh predicate: refresh frame → call `Update` as today; skip frame → do not
call `Update`; instead publish `rGlobalLayout.f4LightingAreaPrevious` from the latch's public
`f4PreviousArea` (last refresh's area) and `fLightingTemporalBlend` from `gLightingTemporalBlend.Get()`
(unused on skip — temporal does not run). The mapped layout is write-only and rewritten per framebuffer,
so both fields must still be written every frame. `TemporalAreaLatch` (`Render.h:55`) itself is unchanged;
a pending `gbLightingTemporalReset` forces a refresh frame, so the existing reset path (first refresh
blends 1, no history fetch) is preserved. The shadow `areaLatch` user in `GlobalUniforms.cpp` is untouched.

### Slider

New `Wrapper gLightingUpdateCadence(1.0f, 1.0f, 4.0f, 1.0f);` — range 1–4, snap step 1, default 1 = off —
using the existing 4-arg snap-step form (precedent `ShadowWrappersBase.cpp:30`:
`Wrapper gObjectShadowsBlurRadius(8.0f, 1.0f, 32.0f, 1.0f);`). Declare
`extern Wrapper gLightingUpdateCadence;` in `LightingWrappersBase.h` and define it in
`LightingWrappersBase.cpp`, both in the `// Write - Temporal` group (next to `gLightingTemporalBlend`,
`LightingWrappersBase.cpp:58-60`). Register it in the matching position of the `// Write - Temporal`
section of `gLightingRegistrar` in
`Engine/Source/Ui/Screens/TweaksScreen/TweaksScreenLighting.cpp` (entry form as at `:17`:
`{"Lighting Blur Sigma", &gLightingBlurSigma},`) — wrapper declaration order and slider order must stay
aligned (TweaksScreen registration contract).

### Expected saving

Roughly (spread + combine + temporal) / 2 at cadence 2. Post-content-gating the idle residual is small
(spread 84–92 µs + combine + temporal), but in combat spread returns to full cost, which is where this
plan's value is — quantify with an A/B in a lit combat scene, not idle.

## Scope contract

The listed scope is target **and** ceiling: make the smallest complete change below; add no abstractions,
configuration, refactors, or fixes to adjacent code encountered along the way. Naming a file grants no
permission to touch anything in it beyond the named regions plus the mechanical necessities (includes,
declarations) the named change requires.

### In scope

- `Engine/Source/Graphics/Render/LightingUniforms.cpp`
  - `RenderLightingGlobal` (`:91`) — cadence counter + predicate established before the
    `PopulateLightingParameters` call at `:195`.
  - `PopulateLightingParameters` latch region (`:54-61`) — refresh-gated latch advance / skip-frame
    publication as specified above. No other part of this function.
  - `RenderLightingSpreadIndirect` (`:349-371`) — consume the cached predicate: zero spread instance
    counts on skip; add the combine + temporal `WriteIndirectComputeBuffer` writes (full groups on
    refresh, zero on skip).
- `Engine/Source/Graphics/Render/Render.h` — one new inline global for the cached predicate in the
  `// Lighting` block (`:90-101`). `TemporalAreaLatch` (`:55-80`) unchanged.
- `Engine/Source/Graphics/Managers/CommandBufferRecordMain.cpp` —
  `RecordLightingSpreadPipeline` (`:109`) only: combine dispatch at `:193` and temporal `RecordCompute`
  at `:219` converted to indirect as specified. The inline group-count locals those two dispatches feed
  may be removed with them. **Shared file with `WindowedLightingDispatch.md`** — see Coordination.
- `Engine/Source/Graphics/Managers/PipelineManager.cpp` — `CreateLightingPipelines` only: add
  `kIndirectHostVisible` to `mCombinePipeline` `.flags` (`:228`) and `mLightingTemporalPipeline` `.flags`
  (`:249`).
- `Engine/Source/Ui/LightingWrappersBase.h` / `.cpp` — the one `gLightingUpdateCadence` extern/definition
  pair in the `// Write - Temporal` group.
- `Engine/Source/Ui/Screens/TweaksScreen/TweaksScreenLighting.cpp` — the one `gLightingRegistrar` entry.

### Out of scope

- `Engine/Source/Graphics/Objects/Pipeline.cpp` / `PipelineCreator.cpp` — read-only precedent; the
  existing `RecordComputeIndirect` / `WriteIndirectComputeBuffer` / creator paths are not modified.
- `Engine/Source/Graphics/Render/MainUniforms.cpp` — read-only; the two
  `RenderLightingSpreadIndirect` call sites stay as they are.
- The shipped empty-deposit spread gate — reused unchanged; windowed `LOAD` passes later supersede it
  with bounded work on every refresh.
- The shadow chain — its windowed indirect dispatch already ships
  (`CommandBufferRecordGlobal.cpp:82-96`, `GlobalUniforms.cpp:476`) and is untouched, as are the shadow
  `TemporalAreaLatch` user, terrain shadows, and object shadows.
- Lighting footprint windowing, bounded `LOAD` deposit/spread, lighting min-texel offsets, and bounded
  history copy — owned by `Documents/Plans/Graphics/WindowedLightingDispatch.md`.
- Spread kernel quality/cost tuning (pass, ring, direction counts, texture multipliers) — already sliders.
- The deposit pass, and cadence for any pass outside the spread → combine → temporal chain.
- Shader edits and data repack — none (group counts and the gate are CPU-side).

## Risk tier and invariants

**Tier 2** — scoped client-render behavior in one subsystem (Graphics), default-off. No determinism/CRC,
wire, `kiVersion`, `.pack`, save/replay, threading, or server exposure; every touched file is
`BT_CLIENT`-only. Invariants:

- Record-once command buffers: no per-frame CB re-record is introduced; all per-frame variation flows
  through host-visible indirect buffers and uniforms.
- `LightingUniforms.cpp` is allocation-tracked main-loop code — the cadence path must stay heap-free
  (fixed loops, no containers).
- Per-framebuffer indirect slot indexing (write only slot `iCommandBuffer`) is what keeps the host-visible
  writes race-free — preserve it for the new compute writes exactly as the spread draw writes do
  (`LightingUniforms.cpp:359-370`).
- Zero group counts are valid Vulkan (`vkCmdDispatchIndirect` with a zero dimension launches no
  workgroups); confirm validation stays clean rather than assuming it.

## Acceptance criteria

- Cadence 1 (default): output bit-identical to the current build — same idle behavior, same empty-deposit
  spread skip, combine/temporal work every frame.
- Cadence 2 in a **lit combat scene**: `kGpuTimerLightingSpread` + `kGpuTimerLightingCombine` +
  `kGpuTimerLightingTemporal` average roughly halves.
- Cadence 2: no visible glow stutter panning over a static lit scene; capture the glow-lag and
  dropped-flash behavior at cadence 3–4 so the default can be revisited on evidence.
- Zoom with cadence > 1: no reprojection smear — `f4LightingAreaPrevious` advances only on refresh frames.
- Pipeline recreate with cadence > 1: the forced refresh blends pure-current (no stale-history fetch), as
  today.
- No command-buffer re-record introduced; Vulkan validation clean across pipeline recreate and resize
  (indirect-dispatch buffer usage, zero group counts).

## Coordination

- `Documents/Plans/Graphics/WindowedLightingDispatch.md`: **never interleave.** Both plans convert the
  same combine/temporal recording and edit the same `LightingUniforms.cpp` populate region (that plan
  already names this plan's cadence predicate). Co-schedule in one session, or land sequentially; the
  later plan refreshes shared-site citations and extends the first change instead of duplicating its
  indirect-dispatch conversion. Either sequential order is valid under the contracts above.

## Notes

- Deposits on skip frames are unconsumed, so a light flash shorter than the cadence is dropped — accepted
  behavior; the cadence 3–4 capture in acceptance criteria exists to bound it.
- This is the only visual-risk item from the original lighting-cost plan, which is why it ships
  default-off (cadence 1) until A/B'd. Half-rate refresh hidden behind an existing temporal-accumulation
  pass is standard practice; the EMA blend in `LightingTemporal.comp` is the compensator.
