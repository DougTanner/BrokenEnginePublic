# Object shadows: bilinear-paired blur taps + render-resolution default

## Context

GPU profile (Profile build, 120 fps, idle scene, ~6.6 ms total GPU/frame): `kGpuTimerObjectShadows` **226 µs** (avg 243, max 294) + `kGpuTimerObjectShadowsBlur` **168 µs** — ~6% of the frame for a content-light scene. Both passes are full-texture every frame:

- **Render**: the `kDynamicModelPipelineModelShadow` pipelines re-rasterize every visible model into `mObjectShadowsTexture`, sized `TextureManager::DetailTextureSize(gObjectShadowsRenderMultiplier)` with **default multiplier 1.0** — a full-render-resolution R16 coverage raster whose detail is then destroyed by a σ=8 Gaussian.
- **Blur**: `ObjectShadowsBlurH/V.comp` each fetch `2 * iObjectShadowsBlurRadius + 1` = **17 discrete taps** per texel (default radius 8, runtime uniform) over the blur texture (`gObjectShadowsBlurMultiplier` default 0.5) — ~34 fetches/texel/frame, content-independent.

**Relationship to the live plans**: `Graphics/WindowedLightingShadowDispatch.md` explicitly excludes object shadows (its Out of scope: "a separate, non-world-sized-texel texture not part of this window mechanism" — the object chain is screen-space, objects cover the whole screen, so there is no live sub-window to crop to). `Graphics/DisabledPassGatingPerfAudit.md` covers the *different* question of running these passes while the feature is effectively off (and records that object shadows have no clean on/off edge — `fObjectShadowsIntensity` is day-cycle continuous); this plan reduces the cost **while enabled** and does not touch gating. Both this plan and the windowing plan edit `CommandBufferRecordMain.cpp` (different pass blocks) — co-schedule or refresh citations.

## Design

### Item 1 — bilinear-paired Gaussian taps in `ObjectShadowsBlurH/V.comp`

Replace the discrete per-texel tap loop with the standard linear-sampling Gaussian (Rástočný/RasterGrid): step the loop by 2, merge each adjacent weight pair `(w1, w2)` into one hardware-bilinear fetch at offset `(i + w2 / (w1 + w2))` with weight `w1 + w2`; center tap fetched alone. `2R + 1` → `R + 1` fetches (17 → 9 at default radius 8, ~47% fewer). The radius/sigma stay runtime uniforms — weights/offsets are computed in-loop exactly as today, just pairwise. Preserve the passes' documented asymmetry (H inverts the sampled coverage, V applies `fObjectShadowsIntensity`) and the existing bounds guard.

Requires the input samplers to be LINEAR-filtered (they sample between texels): verify the combined-sampler bindings for `mObjectShadowsTexture` / `mObjectShadowsBlurIntermediateTexture` in `PipelineManager::CreatePipelines` use a linear sampler (R16 UNORM linear filtering is universally supported; the H pass already relies on implicit downsample filtering from the 1.0× render texture to the 0.5× blur grid).

Expected saving: ~**65–80 µs** of the 168 µs blur.

**Visual impact: (a) no visual change** — mathematically equivalent Gaussian to within the GPU's fixed-point bilinear-fraction precision (sub-1/256 weight quantization on an already-smooth R16 field). No slider.

### Item 2 (optional rider) — same transformation for the terrain `ShadowBlurH/V.comp`

Identical mechanical change to the terrain shadow blur (inside the 113 µs `kGpuTimerShadow` chain; radius there is compile-time, which makes the pairing even simpler — weights could be constant-folded). Those two shaders are also edited by `WindowedLightingShadowDispatch.md` (min-texel invocation offset) — **co-schedule or land after it**; the two edits are orthogonal (loop body vs. index mapping).

**Visual impact: (a) no visual change** (same equivalence).

### Item 3 (decision item) — `gObjectShadowsRenderMultiplier` default 1.0 → 0.5

Render the object coverage raster at the blur resolution instead of full render resolution: one default change in `ShadowWrappersBase.cpp`. The σ=8 blur at the 0.5× grid low-passes away everything the full-res raster adds; rendering at 1.0× only to average it down is paying ~4× raster area for detail the pipeline discards. Expected saving: large fraction of the 226 µs render (raster-area-bound part) plus cheaper H-pass source fetches — estimate **~100–150 µs**, to be confirmed by the existing timer after flipping the slider.

**Visual impact: (b) minor** — shadow silhouettes originate from a 2× coarser coverage grid before blurring; at gameplay camera heights (small units, σ=8 blur) the difference is at the edge-of-perception level, but thin geometry (masts, missile trails' casters) may soften slightly. **Already a runtime slider** ("Render Multiplier", Shadow tab, range 0.25–4.0) — no new UI; this item is only the default value, decided by user A/B (the change detector in `Graphics::Refresh` already handles the destroy/recreate on slider move).

## Critical files

- `Engine/Data/Shaders/Shadow/ObjectShadowsBlurH.comp` / `ObjectShadowsBlurV.comp` — the tap loops (item 1).
- `Engine/Data/Shaders/Shadow/ShadowBlurH.comp` / `ShadowBlurV.comp` — item 2 (shared with `WindowedLightingShadowDispatch.md`).
- `Engine/Source/Graphics/Managers/PipelineManager.cpp` — the `kPipelineObjectShadowsBlurH/V` create entries (verify linear samplers on the input bindings; no structural change expected).
- `Engine/Source/Ui/ShadowWrappersBase.cpp` — `gObjectShadowsRenderMultiplier` default (item 3).
- `Engine/Source/Graphics/Managers/CommandBufferRecordMain.cpp` — object-shadows dispatch sites (unchanged logic; cited for the co-scheduling constraint).

## Out of scope

- Gating the object-shadows render/blur when shadows are visually off — `Graphics/DisabledPassGatingPerfAudit.md`.
- Windowing/indirect dispatch of the world-sized-texel terrain shadow chain — `Graphics/WindowedLightingShadowDispatch.md`.
- Shared-memory tiled blur, mip-pyramid / dual-Kawase restructuring, or merging the blur into the render pass (needs the completed coverage raster).
- The model-shadow render pipelines themselves (vertex/fragment work per model).
- Any change to blur radius/sigma defaults (artistic).

## Notes

- **Invariant exposure**: client/graphics-only. Items 1–2 are shader edits → **shader repack required**; no descriptor-layout change (same bindings, same push constants), no `ShaderLayoutsBase.h` change. Item 3 is a one-line default. No determinism/CRC/wire/`kiVersion`/replay exposure.
- **Grill decisions to pre-stage**: (1) include item 2 (terrain blur) here or leave it to ride with `WindowedLightingShadowDispatch` (same files); (2) item 3 accept/reject after A/B at 0.5 (and whether 0.75 is the compromise); (3) confirm the object-blur input samplers are LINEAR (if any is NEAREST today, pairing changes output and the sampler must be switched deliberately).
- Technique reference: hardware linear-sampling Gaussian (halved taps, exact result) — RasterGrid "Efficient Gaussian blur with linear sampling"; standard production practice.
