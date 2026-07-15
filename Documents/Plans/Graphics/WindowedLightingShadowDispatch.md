# Window the lighting/shadow passes to the cropped sub-region (skip full-texture clear/dispatch)

## Context

The shadow and lighting (deposit / spread / combine / temporal) passes process the **entire** headroom texture every frame, even though only the centered visible window — ~1/3–1/4 of the texture at *every* settled height, now that the texel ramp holds a steady-state target at every height — is live:

- **Combine / temporal**: `vkCmdDispatch(uiCombineWidth/tile, uiCombineHeight/tile, 1)` over the full combine extent (`CommandBufferRecordMain.cpp:414-417` and `:441-442`); the shaders early-out outside the window (write `0` / blend) but the workgroups still launch and the writes still touch every texel.
- **Shadow**: `Shadow.comp` is dispatched full-texture; outside the window (+`kiShadowWindowMargin`) it writes the no-shadow fill (`Shadow.comp:23-28`). `ShadowBlurH/V.comp` and `ShadowTemporal.comp` are likewise full-texture-dispatched with a window early-out.
- **Deposit**: `LOAD_OP_CLEAR` over the whole attachment every frame (`RenderTargetTexturesLighting.cpp` lighting render pass).
- **Spread**: the runtime pass count (default 40) MRT passes each rasterize the full texture; the `LightingSpread.frag` window early-out makes off-window texels a cheap passthrough copy, but it is still a full-texture write per pass.

The *expensive* math (spread radial gather, shadow ray-march) is already windowed via the in-shader early-out. What is **not** windowed is the clear + per-texel write bandwidth + workgroup-launch footprint, which is full-texture. With the texel ramp now holding the window at its steady-state target at all heights (`Camera.cpp` clamp removal), that wasted footprint is the dominant residual cost of the lighting/shadow pipeline.

**Enabler (in place):** the `CLAMP_TO_BORDER` edge samplers — `mVkSamplerBorderWhite` (shadow, opaque white = no-shadow) and the transparent-black `mVkSamplerBorder` (lighting = no-light), wired into the Terrain / Water consumer bindings in `PipelineManager.cpp`. They let the off-window region go stale/unwritten safely: the consumers (`Terrain.frag` / `Water.frag`) sample by world position and only ever read inside the window; the sole beyond-window read (a fast zoom-out that overflows the whole texture) lands on the constant border instead of stale data or a smeared edge.

## Design

Dispatch / render each pass over only the **window + that pass's margin**, not the full texture. The window bounds move every frame, so this must be done **without command-buffer re-record** (the CB re-record ban). Two mechanisms, both already used elsewhere in the engine:

1. **Compute passes** (`Shadow.comp`, `ShadowBlurH/V.comp`, `ShadowTemporal.comp`, `LightCombine.comp`, `LightingTemporal.comp`): replace the direct `vkCmdDispatch(...)` with **`vkCmdDispatchIndirect`** reading a host-visible `VkDispatchIndirectCommand` written each frame from the uniform-populate path. Offset `gl_GlobalInvocationID` by the window's min-texel so threads map to absolute texels (shadow already has `iShadowVisibleMinX/Y` in `GlobalLayout`; lighting derives its window in-shader from `f4VisibleArea` vs `f4LightingArea` and would gain an explicit min-texel offset uniform). The existing in-shader window early-out stays as a cheap defensive guard covering the margin band and any dispatch-size mismatch.

2. **Render passes** (`LightingDeposit`, `LightingSpread`): a **dynamic scissor** (`vkCmdSetScissor` — dynamic pipeline state, no re-record) restricts rasterization to the window + margin, so the 40 spread passes' passthrough writes only touch the window.

The indirect/scissor bounds for each pass = window **+ that pass's existing margin** (`kiShadowWindowMargin` for shadow/blur, the one-pass-gather margin for spread, the bilinear margin for combine/temporal) so the consumer's and the next pass's edge reads still land on freshly-written texels.

### Decision points to resolve in the grill

- **Deposit `LOAD_OP_CLEAR`** — the render *area* (and its attachment clear) is baked into `VkRenderPassBeginInfo.renderArea` at record time; a dynamic scissor limits rasterization but **not** the clear. Options:
  - (a) **Keep the full-texture clear, scissor only the deposit rasterization** — simplest, captures most of the win (the per-light quad raster is the variable cost; the clear is a single fast write). *Recommended default unless the clear measures non-trivial.*
  - (b) Switch deposit to `LOAD_OP_DONT_CARE` + an explicit scissored full-screen clear quad (clears only the window).
  - (c) Leave deposit untouched if measurement shows the clear is negligible.
- **History `vkCmdCopyImage` (combine → history, 4 textures)** — a `VkImageCopy` region is also baked at record time, so it can't be windowed per-frame, and there is no indirect copy. Options:
  - (a) **Fold the history write into `LightingTemporal.comp`**: make the four history textures `STORAGE` and have the temporal shader write the blended result to *both* the combine images (in place) *and* the history images, eliminating the 4 copies entirely. The windowed indirect dispatch then windows the history write for free. *Recommended.*
  - (b) Keep the full-texture copy (leaves a full-texture residual that would dominate once the dispatches are windowed — not recommended).
- **Temporal reprojection coverage** — `ShadowTemporal`/`LightingTemporal` reproject each window texel into the *previous* frame's area (`f4*AreaPrevious`) to fetch history. Under windowing, the history's written region must cover the reprojection source: the previous window covers the previous visible area, the current window reprojects mostly into it, and the newly-revealed edge falls back to current via the existing disocclusion path (reprojected UV outside [0,1]). Verify the disocclusion fallback fully covers the panned/zoomed gap (it should — it is the same mechanism already relied on), and size the temporal window/margin so the overlap holds.

### Stale-data invariant (the correctness contract)

Off-window texels are no longer written and hold stale data from prior frames / camera positions. This is safe **only** because: (i) the consumers sample inside the window; (ii) the sole beyond-window read (fast zoom-out past the whole texture) hits the `CLAMP_TO_BORDER` constant; (iii) the temporal pass reads history through world-position reprojection with disocclusion fallback. Any future consumer that samples the lighting/shadow textures outside the live window would break this and must be routed through the border or the window.

## Critical files

- `Engine/Source/Graphics/Objects/PipelineCreator.cpp` — add the `kIndirectHostVisible | kCompute` branch (currently `ASSERT(false)`); **shared with `WaterDisplacementIndirectCompute.md` Item 1**.
- `Engine/Source/Graphics/Objects/Pipeline.h/.cpp` — `WriteIndirectComputeBuffer(...)` + `RecordComputeIndirect(...)` (shared with the same plan).
- `Engine/Source/Graphics/Managers/PipelineManager.cpp` — add `kIndirectHostVisible` to the shadow / combine / temporal compute pipeline flags; the deposit/spread graphics pipelines gain `kDynamicScissor` (or equivalent dynamic-state flag).
- `Engine/Source/Graphics/Managers/CommandBufferRecordGlobal.cpp` — shadow + blur + shadow-temporal dispatch → indirect.
- `Engine/Source/Graphics/Managers/CommandBufferRecordMain.cpp` — combine/temporal dispatch (`:414-417`, `:441-442`) → indirect; deposit + spread `vkCmdSetScissor`; remove the 4 history `RecordCopyImageFrom` calls if the temporal-writes-history option lands.
- `Engine/Data/Shaders/Shadow/Shadow.comp`, `ShadowBlurH/V.comp`, `ShadowTemporal.comp` — invocation min-texel offset; temporal optionally writes history.
- `Engine/Data/Shaders/Lighting/LightCombine.comp`, `LightingTemporal.comp` — invocation min-texel offset; temporal optionally writes the four history images.
- `Engine/Source/Graphics/Render/GlobalUniforms.cpp` (shadow + `PopulateLightingParameters`) — write the per-pass indirect dispatch group counts (window+margin / tile) and any min-texel offset uniform.
- `Engine/Source/Graphics/Managers/RenderTargetTexturesLighting.cpp` — add `STORAGE` usage to the four lighting history textures if the temporal pass writes them directly.
- `Engine/Data/Shaders/ShaderLayoutsBase.h` — any new window-offset uniform fields.

## Out of scope

- **Object shadows** (`mObjectShadowsTexture` / blur) — a separate, non-world-sized-texel texture not part of this window mechanism.
- The texel-ramp steady-state-target behavior and the `CLAMP_TO_BORDER` edge samplers — both already exist and are *prerequisites*, not part of this plan.
- The texture **allocation** — stays at full headroom size (`LightingDetailTextureSize` / shadow pre-size); only the per-frame *processed footprint* shrinks.
- The deposit per-light quad rasterization itself — already drawn only where lights are; the scissor merely bounds it to the window.
- Profile-overlay readouts — the active-pixel `NxM` display reports the window size and is sufficient to measure this change.

## Acceptance criteria

- GPU profile: the `Lighting Spread` / `Lighting Combine` / `Lighting Temporal` and `Shadow` timers drop and scale with the window size (small when settled, growing only during a fast zoom-out), instead of being flat at full-texture cost across all heights.
- No visual change at a settled height (identical lighting/shadow inside the window).
- Fast zoom-out: screen edges beyond coverage fade to no-light / no-shadow via the border, with no smear and no stale data.
- No command-buffer re-record introduced — all per-frame variation flows through indirect-dispatch buffers, dynamic scissor, and uniforms.
- Vulkan validation clean (indirect-dispatch buffer usage, dynamic scissor, any new combine/history storage-image layout transitions).

## Coordination

- `Documents/Plans/Graphics/Managers/Architecture_BindlessSlotLifecycle.md`: mandatory reciprocal pipeline-cluster exclusion; never interleave because BindlessSlotLifecycle executes alone.
- `Documents/Plans/Graphics/Architecture_ShadowLightingUniformDedup.md` and `Documents/Plans/Graphics/LightingSpreadEmptySkipCadence.md`: never interleave their shared GlobalUniforms/LightingUniforms/recording-region edits; land sequentially with citation refresh.

## Notes

- **Dependency / shared helper:** the `WriteIndirectComputeBuffer` + `RecordComputeIndirect` plumbing and the `PipelineCreator.cpp` `kIndirectHostVisible | kCompute` branch are shared with `Graphics/WaterDisplacementIndirectCompute.md` (Item 1). Land one first or co-implement the helper; the second plan then only wires its pipelines.
- The in-shader window early-outs in every pass stay as cheap defensive guards — they cost a branch on out-of-window threads (now rarely launched) and protect against any dispatch-size / margin mismatch.
- Sequence within this plan: land the compute-pass indirect dispatch first (biggest, cleanest win — combine/temporal/shadow), then the spread scissor, then resolve the deposit-clear and history-copy decisions last (they are the fiddly, lower-value tail).
