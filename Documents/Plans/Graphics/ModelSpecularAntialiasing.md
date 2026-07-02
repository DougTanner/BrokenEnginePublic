# Model Specular Antialiasing

## Context

The session that landed analytic specular AA for the water shader (`Engine/Data/Shaders/Water/Water.frag` — the `WATER_SPEC_AA_MODE` compile-time select and its `FilteredPowerLobe` NDF-variance-widening helper) left `Model.frag` as the remaining specular-aliasing site. A review sweep identified its GGX Cook-Torrance specular as the offender:

- **EWNS cardinal-direction specular loop** (`Model.frag` ~`363-383`, gated by `ENABLE_SPECULAR_LIGHTING`): four fixed light directions each evaluate `cD = D_GGX(cNdotH, alphaRoughness)`.
- **Direct sun/moon BRDF** (~`301-312`, gated by `ENABLE_BRDF`): `D = D_GGX(NdotH, alphaRoughness)`.

Both feed off the per-fragment perturbed normal `n = GetNormal(material)` (screen-space-derivative tangent frame + BC5 normal map). At low `alphaRoughness` (metals — `material.fMetallicFactor` high, small `perceptualRoughness`) the GGX NDF is a sub-pixel-narrow lobe, so per-pixel normal-map variation makes the highlight flicker under camera/object motion. `alphaRoughness = perceptualRoughness * perceptualRoughness`, and `D_GGX` squares it again (`a2 = alphaRoughness * alphaRoughness`), so the effective microfacet distribution is `perceptualRoughness^4`-narrow — exactly the regime that aliases.

Today this is masked by `kSampleShading` on the model pipeline (`DynamicPipelines::CreateModelPipeline`, `DynamicPipelines.cpp:73` flags list). `kSampleShading` maps (in `PipelineCreator.cpp:448`) to `sampleShadingEnable = (flags & kSampleShading && gSampleShading.Get<bool>())` with `minSampleShading = gMinSampleShading` — i.e. models run the fragment shader **per MSAA sample** whenever the runtime `gSampleShading` toggle is on. That is a real per-frame cost. If analytic NDF-derivative AA holds up, the model pipeline could drop `kSampleShading` and recover per-sample shading cost with no visible regression.

The IBL split-sum ambient path (`GetIBLContribution`) is prefiltered by cubemap mips + the BRDF LUT and does **not** call `D_GGX`, so it is inherently far less aliasing-prone — it is out of primary scope (see Out of scope), addressed only as a conditional fallback if A/B testing reveals residual IBL shimmer.

Invariant exposure: **client/graphics-only.** The two new tuning uniforms are `MainLayout` fields (per-frame uniform buffer, populated CPU-side from Ui wrappers each frame — same class as the existing `fWaterSpecAA*` / `fPbr*` fields). `MainLayout` is not serialized to `.pack`, not part of sim frame state, not CRC'd. **No determinism / CRC / `kiVersion` / `.pack` / wire exposure.** Requires a DataPacker shader repack (the `#define` mode select + the shader edits are compile-time, edit-and-repack, exactly like `WATER_SPEC_AA_MODE`).

## Design

### 1. Geometric specular AA in `Model.frag` (GGX roughness-domain form)

Follow the water precedent: add a compile-time mode select at the top of `Model.frag`, defaulting to on.

```glsl
// Specular-AA variant select — compile-time, toggled by editing this define and re-running DataPacker
// (same in-source mechanism as WATER_SPEC_AA_MODE in Water.frag / DT_LIGHTING_ONLY in ShaderLayoutsBase.h).
// GGX geometric specular AA: normal-derivative slope variance added to the microfacet alpha, de-flickering
// the sub-pixel-narrow specular lobe on low-roughness (metal) surfaces so no MSAA sample-shading is needed.
// Runtime sliders: fPbrSpecAAVariance scales the kernel; fPbrSpecAAThreshold clamps the maximum widening.
// 0 = off — pointwise D_GGX (flickers; baseline reference)
// 1 = NDF variance widening — Kaplanyan/Tokuyoshi geometric specular AA via the Vlachos GDC15 production form
#define MODEL_SPEC_AA_MODE 1
```

Unlike Water (five modes for its Phong-power skybox lobes), GGX needs only the **roughness-domain** form — the Phong-power remap `p' = 2/(2/(p+2) + kernel) - 2` from `FilteredPowerLobe` does **not** apply here; GGX widens `alpha^2` directly.

Compute the kernel **once** in `main()` after `n` and `alphaRoughness` are established (the sun BRDF and all four EWNS directions share the same normal `n`, so one derivative pair covers every `D_GGX` site):

```glsl
float alphaRoughnessAA = alphaRoughness;
#if MODEL_SPEC_AA_MODE == 1
	// Tokuyoshi/Kaplanyan 2019 geometric specular AA, Vlachos GDC15 production form.
	vec3 dNdx = dFdx(n);
	vec3 dNdy = dFdy(n);
	float fVariance = mainLayout.fPbrSpecAAVariance * (dot(dNdx, dNdx) + dot(dNdy, dNdy));
	float fKernel = min(2.0 * fVariance, mainLayout.fPbrSpecAAThreshold);
	// alphaRoughness is the GGX alpha (perceptualRoughness^2); widen alpha^2 in slope space, then clamp.
	alphaRoughnessAA = sqrt(clamp(alphaRoughness * alphaRoughness + fKernel, 0.0, 1.0));
#endif
```

Then swap the aliasing `D_GGX` inputs:
- Sun BRDF site (~`303`): `float D = D_GGX(NdotH, alphaRoughnessAA);`
- EWNS loop site (~`376`): `float cD = D_GGX(cNdotH, alphaRoughnessAA);`

**Derivative placement (helper-invocation safety):** `dFdx(n)` / `dFdy(n)` must be evaluated at uniform (non-divergent) control-flow depth so helper invocations produce valid derivatives — compute them at `main()` top level, not inside the `ENABLE_SPECULAR_LIGHTING` block or the EWNS `for` loop (matches Water's mode-2 placement of `dFdx(f3SkyboxWaveNormal)` before any branch). `n` is already computed unconditionally at `main()` scope.

**Design decision (pre-staged for grill): does the widened alpha feed the `V` (visibility) term too?** `V_SmithGGXCorrelated` also takes `alphaRoughness`. Options:
- **A (recommend): widen `D` only** — mirrors Water's NDF-only widening; the visibility term is a smooth, non-aliasing shadowing factor, and widening it slightly over-darkens the (already dimmed) highlight. Minimal, matches the precedent.
- **B: widen both `D` and `V`** — physically, sub-pixel normal variance raises effective roughness for both terms; a rougher surface's `V` broadens too. Marginally more energy-consistent but no visible aliasing benefit.
Recommend A (feed `alphaRoughnessAA` to the two `D_GGX` calls; leave `V_SmithGGXCorrelated` on the material `alphaRoughness`).

### 2. Tuning uniforms — model-specific (recommended) vs. reuse Water's

**Design decision (pre-staged for grill):** reuse the existing `fWaterSpecAAVariance` / `fWaterSpecAAThreshold` `MainLayout` fields, or add model-specific `fPbrSpecAAVariance` / `fPbrSpecAAThreshold`?

- **Recommend model-specific fields.** The tuning domains differ fundamentally: Water's sliders scale a Phong-power (`p ~ 200`) skybox-lobe variance; Model's scale a GGX `alpha^2` (metal roughness `~ 0.04-0.1^2`) slope-space kernel with a different unit basis and a different sensible default range. Sharing one slider would couple two unrelated visual tunings. The cost of separate fields is trivial (two floats in a uniform already dense with per-target lighting scalars).

Add to `MainLayout` in `ShaderLayoutsBase.h` (next to the existing `fWaterSpecAA*` pair or in the Pbr grouping):
```glsl
	float fPbrSpecAAVariance INIT;
	float fPbrSpecAAThreshold INIT;
```

Wire them:
- **`Engine/Source/Ui/PbrWrappersBase.h` / `.cpp`** — declare/define `Wrapper gPbrSpecAAVariance(1.0f, 0.0f, 4.0f);` and `Wrapper gPbrSpecAAThreshold(0.18f, 0.0f, 1.0f);` (final defaults tuned during A/B; variance default `1.0` so the kernel ≈ raw slope variance, threshold `0.18` mirrors Water's clamp). Group with the other Pbr specular wrappers (`gPbrLightingSpecular` etc.).
- **`Engine/Source/Ui/Screens/TweaksScreen/TweaksScreenPbr.cpp`** — expose both sliders in the Pbr tweaks screen alongside the other specular controls.
- **`Engine/Source/Graphics/Render/LightingUniforms.cpp`** — copy into `MainLayout`: `rMainLayout.fPbrSpecAAVariance = gPbrSpecAAVariance.Get();` / `rMainLayout.fPbrSpecAAThreshold = gPbrSpecAAThreshold.Get();` (next to the existing `rMainLayout.fPbr*` assignments).

`MainLayout` is dual-language (`BT_ENGINE`) so the server also compiles the two new fields; they are client-only lighting values the server never reads — no `BT_CLIENT` guard needed (matches every existing `fPbr*` / `fWaterSpecAA*` field, per the Water/Model CLAUDE notes).

### 3. `kSampleShading` investigation + optional drop (gate behind user judgement)

End state: with analytic AA landed, A/B test in-game (`MODEL_SPEC_AA_MODE 1` vs `0`, and vs the current `kSampleShading` masking) and — **only if the user confirms the analytic result holds** — drop `kSampleShading` from `CreateModelPipeline`'s flags list (`DynamicPipelines.cpp:73`), recovering per-sample fragment shading cost.

Investigation to perform and record in-plan **before** recommending the drop:
- **Confirm `Model.frag` has no alpha test / `discard`.** Verified at authoring: the shader outputs `baseColor.a` but never `discard`s and has no alpha-cutoff branch, so `kSampleShading` on models is purely interior specular/shading-rate AA — **not** alpha-tested-edge coverage. `alphaToCoverageEnable` is unconditionally `VK_FALSE` in `PipelineCreator.cpp`, confirming no alpha-to-coverage dependency either.
- **Confirm geometric edge AA survives the drop.** `rasterizationSamples` (MSAA coverage) is independent of `sampleShadingEnable` — dropping `kSampleShading` keeps polygon-edge MSAA and removes only per-sample *interior* shading (the specular aliasing the analytic filter now handles). Silhouette AA is unaffected.
- **Scope guard — do NOT touch other `kSampleShading` consumers.** `CreatePipelineVisibleLights` (`DynamicPipelines.cpp:191`) and `CreatePipelineBillboards` (`:252`) also carry `kSampleShading`, but for their own alpha/edge-coverage reasons on translucent quads — **out of scope**, leave untouched.

The drop is a one-line flag removal; keep it as the final, separately-confirmed step so the shader AA can land and be validated independently of the perf change.

## Critical files

- **`Engine/Data/Shaders/Model/Model.frag`** — add `MODEL_SPEC_AA_MODE` define; compute `alphaRoughnessAA` kernel from `dFdx(n)`/`dFdy(n)` at `main()` scope; feed it to the `D_GGX` call in the `ENABLE_BRDF` sun term (~`303`) and the `ENABLE_SPECULAR_LIGHTING` EWNS loop (~`376`).
- **`Engine/Data/Shaders/ShaderLayoutsBase.h`** — add `fPbrSpecAAVariance` / `fPbrSpecAAThreshold` to `MainLayout` (dual-language `INIT` fields).
- **`Engine/Source/Ui/PbrWrappersBase.h` / `.cpp`** — declare/define the two `Wrapper` sliders.
- **`Engine/Source/Ui/Screens/TweaksScreen/TweaksScreenPbr.cpp`** — expose the sliders.
- **`Engine/Source/Graphics/Render/LightingUniforms.cpp`** — copy wrapper values into `MainLayout` (the `rMainLayout.fPbr*` block).
- **`Engine/Source/Graphics/Managers/DynamicPipelines.cpp`** — (optional, final step, user-gated) remove `kSampleShading` from `CreateModelPipeline`'s flags list at `:73`.
- **`Engine/Data/Shaders/Model/CLAUDE.md`** — add a "Specular antialiasing" architecture note mirroring the Water CLAUDE.md bullet (analytic NDF-derivative AA, compile-time mode, replaces sample-shading masking).

## Out of scope

- **IBL specular (`GetIBLContribution`) AA.** Cubemap-mip-prefiltered + BRDF-LUT split-sum — inherently stable; no `D_GGX` to widen. Address only as a fallback (widen the `perceptualRoughness` passed to `GetIBLContribution`) **if** A/B reveals residual IBL shimmer; not part of the primary change.
- **Other `kSampleShading` pipelines** — `CreatePipelineVisibleLights`, `CreatePipelineBillboards` (alpha/edge coverage on translucent quads).
- **Water shader** — already done; this plan does not touch `Water.frag` or the `fWaterSpecAA*` uniforms.
- **A multi-mode ladder** (box filter / Toksvig / supersample reference like Water's modes 1/3/4). GGX only needs the single roughness-domain form; adding validation-only modes is YAGNI unless the grill specifically wants a supersample reference for A/B.
- **Global `gSampleShading` / `gMinSampleShading` runtime settings** — the model pipeline flag drop is the only sample-shading change; the global toggle stays for the other consumers.
- **Removing `V`-term widening or reworking the BRDF** — the change is additive on the `D` (NDF) input only.

## Acceptance criteria

- With `MODEL_SPEC_AA_MODE 1` and `kSampleShading` still present, low-roughness metal models show no specular flicker under camera/object motion at the RTS zoom range; `MODEL_SPEC_AA_MODE 0` reproduces the flicker (baseline).
- `fPbrSpecAAVariance` / `fPbrSpecAAThreshold` sliders visibly broaden/dim vs. clamp the highlight and appear in the Pbr tweaks screen.
- Client and server both build (dual-language `MainLayout` fields compile server-side unused).
- (Final, user-confirmed) with `kSampleShading` dropped, no visible specular or edge regression vs. the sample-shaded build; per-sample shading no longer runs on models when `gSampleShading` is on.

## Notes

- **Grill decisions (2):** (1) tuning uniforms — model-specific `fPbrSpecAA*` (recommended) vs. reuse Water's `fWaterSpecAA*`; (2) widen `D` only (recommended) vs. `D` and `V`. Both pre-staged in Design §1/§2.
- **Compile-time mode + repack:** `MODEL_SPEC_AA_MODE` is edit-and-repack like `WATER_SPEC_AA_MODE` — DataPacker shader rebuild required after any shader edit.
- **Invariant exposure declared:** client/graphics-only; two new `MainLayout` uniform fields (per-frame, CPU-populated, not serialized/CRC'd); no determinism / CRC / `kiVersion` / `.pack` / wire exposure.
- **Shared-file overlaps (see `Order.md` Dependencies):** the optional `kSampleShading` drop edits `CreateModelPipeline` in `DynamicPipelines.cpp`, the same function `Meta/ReviewSweepQuickWins.md` item 13 folds the try/catch into — refresh line cites if co-scheduled. The new `MainLayout` fields append to `ShaderLayoutsBase.h`, which `Graphics/WindowedLightingShadowDispatch.md` may also grow — additive, refresh cites. Both overlaps are line-drift only, no logic conflict.
- Expected effort likely Small-Medium: the shader edit is a localized additive change following a landed precedent; the bulk is the uniform/Ui wiring, the sample-shading investigation, and A/B tuning.
