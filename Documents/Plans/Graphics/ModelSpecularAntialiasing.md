<!-- broken-engine-plan/v1 {"createdUtc":"2026-07-02T02:29:26.000Z","dependsOn":[]} -->
# Model Specular Antialiasing

## Context

The session that landed analytic specular AA for the water shader (`Engine/Data/Shaders/Water/Water.frag` — the `WATER_SPEC_AA_MODE` compile-time select and its `FilteredPowerLobe` NDF-variance-widening helper) left `Model.frag` as the remaining specular-aliasing site. Its GGX Cook-Torrance specular is the offender:

- **Direct sun/moon BRDF** (`Model.frag` `#if ENABLE_BRDF` block, lines 273-284): `float D = D_GGX(NdotH, alphaRoughness);` (line 275).
- **EWNS cardinal-direction specular loop** (`#if ENABLE_SPECULAR_LIGHTING` block, lines 336-356): four fixed light directions each evaluate `float cD = D_GGX(cNdotH, alphaRoughness);` (line 348).

Both feed off the per-fragment perturbed normal `vec3 n = GetNormal(material);` (line 227 — screen-space-derivative tangent frame + BC5 normal map). At low `alphaRoughness` (metals — `material.fMetallicFactor` high, small `perceptualRoughness`) the GGX NDF is a sub-pixel-narrow lobe, so per-pixel normal-map variation makes the highlight flicker under camera/object motion. `alphaRoughness = perceptualRoughness * perceptualRoughness` (line 216), and `D_GGX` squares it again (`a2 = alphaRoughness * alphaRoughness`), so the effective microfacet distribution is `perceptualRoughness^4`-narrow — exactly the regime that aliases.

Today this is masked by `kSampleShading` on the model pipeline (flags list in `DynamicPipelines::CreateModelPipeline(common::crc_t, std::string_view, common::crc_t, Buffer*)`, `DynamicPipelines.cpp:80`). `kSampleShading` maps (in `ConfigureMultisampling`, `PipelineCreator.cpp:452`) to `sampleShadingEnable = (flags & kSampleShading && gSampleShading.Get<bool>())` with `minSampleShading = gMinSampleShading` — i.e. models run the fragment shader **per MSAA sample** whenever the runtime `gSampleShading` toggle is on. That is a real per-frame cost. If analytic NDF-derivative AA holds up, the model pipeline could drop `kSampleShading` and recover per-sample shading cost with no visible regression.

The IBL split-sum ambient path (`GetIBLContribution`) is prefiltered by cubemap mips + the BRDF LUT and does **not** call `D_GGX`, so it is inherently far less aliasing-prone — it is out of primary scope (see Scope contract), addressed only as a conditional fallback if A/B testing reveals residual IBL shimmer.

## Scope contract

The in-scope list below plus the `## Out of scope` section form the scope contract. The listed scope is both target and ceiling: make the smallest complete change satisfying the Design and Acceptance criteria below, and add no abstractions, configuration, refactors, or fixes to adjacent code encountered along the way. Naming a file grants permission only for the named regions plus the mechanical necessities (includes, declarations) those named edits require — nothing else in the file.

**In scope:**

- `Engine/Data/Shaders/Model/Model.frag` — add the `MODEL_SPEC_AA_MODE` define near the existing debug toggles (lines 14-21); add the `alphaRoughnessAA` kernel computation in `main()` after `n` (line 227) is established; change the `D_GGX` argument at exactly two call sites: `D = D_GGX(NdotH, ...)` (line 275) and `cD = D_GGX(cNdotH, ...)` (line 348). No other statement in the shader changes; both `V_SmithGGXCorrelated` calls (lines 276, 349) keep the material `alphaRoughness`.
- `Engine/Data/Shaders/ShaderLayoutsBase.h` — append `fPbrSpecAAVariance` / `fPbrSpecAAThreshold` to the `MainLayout` `fPbr*` grouping (lines 629-658, after `fPbrCubemapLodOffset` at line 658). No other layout field moves or changes.
- `Engine/Source/Ui/PbrWrappersBase.h` — add two `extern Wrapper` declarations in the `// Pbr - BRDF` group (after `gPbrBrdfSpecularPower`, line 16).
- `Engine/Source/Ui/PbrWrappersBase.cpp` — add the two matching `Wrapper` definitions in the `// Pbr - BRDF` group (after line 14), values per Design §2.
- `Engine/Source/Ui/Screens/TweaksScreen/TweaksScreenPbr.cpp` — add two entries to the `gPbrRegistrar` slider map (BRDF group, after line 23) and two `WrapperSlider` calls in `TweaksScreenBase::RenderPbrSection()` (BRDF section, after line 72), keeping registrar/slider/wrapper-declaration order aligned per the TweaksScreen contract.
- `Engine/Source/Graphics/Render/LightingUniforms.cpp` — add two assignments in `RenderLightingMain`'s `rMainLayout.fPbr*` block (lines 314-343, next to `fPbrBrdfSpecularPower` at line 328).
- `Engine/Source/Graphics/Managers/DynamicPipelines.cpp` — **final, user-gated step only** (Design §3): remove `kSampleShading` from the flags list at line 80 inside `DynamicPipelines::CreateModelPipeline(common::crc_t, std::string_view, common::crc_t, Buffer*)`. One token; nothing else in the function or file.
- `Engine/Data/Shaders/Model/AGENTS.md` — add one "specular antialiasing" architecture-note bullet mirroring the Water AGENTS.md bullet (analytic NDF-derivative AA, compile-time mode select, replaces sample-shading masking).

## Out of scope

- **IBL specular (`GetIBLContribution`) AA.** Cubemap-mip-prefiltered + BRDF-LUT split-sum — inherently stable; no `D_GGX` to widen. Address only as a fallback (widen the `perceptualRoughness` passed to `GetIBLContribution`) **if** A/B reveals residual IBL shimmer; not part of the primary change.
- **Other `kSampleShading` pipelines** — `CreatePipelineVisibleLights` (`DynamicPipelines.cpp:167`, flags at :179) and `CreatePipelineBillboards` (:224, flags at :239) carry `kSampleShading` for their own alpha/edge-coverage reasons on translucent quads. Leave untouched.
- **Water shader** — already done; do not touch `Water.frag` or the `fWaterSpecAA*` uniforms.
- **A multi-mode ladder** (box filter / Toksvig / supersample reference like Water's modes). GGX needs only the single roughness-domain form; validation-only modes are YAGNI.
- **Global `gSampleShading` / `gMinSampleShading` runtime settings** — the model pipeline flag drop is the only sample-shading change; the global toggle stays for the other consumers.
- **`V`-term widening or BRDF rework** — the change is additive on the `D` (NDF) input only.
- **Mip-variance handoff for zoom/minification** — known residual, deliberately deferred; see Notes.

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

Compute the kernel **once** in `main()` after `n` (line 227) and `alphaRoughness` (line 216) are established (the sun BRDF and all four EWNS directions share the same normal `n`, so one derivative pair covers every `D_GGX` site):

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
- Sun BRDF site (line 275): `float D = D_GGX(NdotH, alphaRoughnessAA);`
- EWNS loop site (line 348): `float cD = D_GGX(cNdotH, alphaRoughnessAA);`

**Derivative placement (helper-invocation safety):** `dFdx(n)` / `dFdy(n)` must be evaluated at uniform (non-divergent) control-flow depth so helper invocations produce valid derivatives — compute them at `main()` top level, not inside the `ENABLE_SPECULAR_LIGHTING` block or the EWNS `for` loop (matches Water's placement of its normal derivatives before any branch). `n` is already computed unconditionally at `main()` scope.

**Decision (2026-07-03): widen `D` only (option A).** Feed `alphaRoughnessAA` to the two `D_GGX` calls (lines 275, 348); leave both `V_SmithGGXCorrelated` calls (lines 276, 349) on the material `alphaRoughness`. The visibility term is a smooth shadowing factor with no aliasing to fix — widening it only over-darkens the already-dimmed highlight — and D-only mirrors the landed Water precedent (NDF-only widening).

### 2. Tuning uniforms — model-specific

**Decision (2026-07-03): add model-specific `fPbrSpecAAVariance` / `fPbrSpecAAThreshold` fields** (do not reuse Water's `fWaterSpecAA*`, `ShaderLayoutsBase.h:580-581`). The tuning domains differ fundamentally: Water's sliders scale a Phong-power (`p ~ 200`) skybox-lobe variance; Model's scale a GGX `alpha^2` (metal roughness `~ 0.04-0.1^2`) slope-space kernel with a different unit basis and a different sensible default range. Sharing one slider would couple two unrelated visual tunings. The cost of separate fields is trivial (two floats in a uniform already dense with per-target lighting scalars).

Add to `MainLayout` in `ShaderLayoutsBase.h`, at the end of the `fPbr*` grouping (after `fPbrCubemapLodOffset`, line 658):
```glsl
	float fPbrSpecAAVariance INIT;
	float fPbrSpecAAThreshold INIT;
```

Wire them:
- **`Engine/Source/Ui/PbrWrappersBase.h` / `.cpp`** — declare/define `Wrapper gPbrSpecAAVariance(1.0f, 0.0f, 4.0f);` and `Wrapper gPbrSpecAAThreshold(0.18f, 0.0f, 1.0f);` in the `// Pbr - BRDF` group (final defaults tuned during A/B; variance default `1.0` so the kernel is roughly the raw slope variance, threshold `0.18` mirrors Water's clamp).
- **`Engine/Source/Ui/Screens/TweaksScreen/TweaksScreenPbr.cpp`** — add `{"Spec AA Variance", &gPbrSpecAAVariance}` / `{"Spec AA Threshold", &gPbrSpecAAThreshold}` to `gPbrRegistrar`'s BRDF group and matching `WrapperSlider` calls in `RenderPbrSection()`'s BRDF section, preserving declaration/slider order alignment (exact label text is trivial local detail; keys must be globally unique).
- **`Engine/Source/Graphics/Render/LightingUniforms.cpp`** — in `RenderLightingMain`, copy into the layout: `rMainLayout.fPbrSpecAAVariance = gPbrSpecAAVariance.Get();` / `rMainLayout.fPbrSpecAAThreshold = gPbrSpecAAThreshold.Get();` next to the existing `rMainLayout.fPbrBrdf*` assignments (lines 325-328).

`MainLayout` is dual-language (`BT_ENGINE` selects C++ vs GLSL declarations) so the server also compiles the two new fields; they are client-only lighting values the server never reads — no `BT_CLIENT` guard, matching every existing `fPbr*` / `fWaterSpecAA*` field.

### 3. `kSampleShading` investigation + optional drop (user-gated)

End state: with analytic AA landed, A/B test in-game (`MODEL_SPEC_AA_MODE 1` vs `0`, and vs the current `kSampleShading` masking) and — **only if the user confirms the analytic result holds** — remove `kSampleShading` from the flags list in `DynamicPipelines::CreateModelPipeline(common::crc_t, std::string_view, common::crc_t, Buffer*)` (`DynamicPipelines.cpp:80`), recovering per-sample fragment shading cost.

Investigation already performed and recorded (re-confirm against current code before the drop):
- **`Model.frag` has no alpha test / `discard`.** Verified at authoring: the shader outputs `baseColor.a` but never `discard`s and has no alpha-cutoff branch, so `kSampleShading` on models is purely interior specular/shading-rate AA — **not** alpha-tested-edge coverage. `alphaToCoverageEnable` is unconditionally `VK_FALSE` (`PipelineCreator.cpp:455`), confirming no alpha-to-coverage dependency either.
- **Geometric edge AA survives the drop.** `rasterizationSamples` (MSAA coverage, `PipelineCreator.cpp:451`) is independent of `sampleShadingEnable` — dropping `kSampleShading` keeps polygon-edge MSAA and removes only per-sample *interior* shading (the specular aliasing the analytic filter now handles). Silhouette AA is unaffected.
- **Scope guard — do NOT touch other `kSampleShading` consumers** (see Scope contract: `CreatePipelineVisibleLights`, `CreatePipelineBillboards`).

The drop is a one-token flag removal; keep it as the final, separately-confirmed step so the shader AA can land and be validated independently of the perf change.

## Critical files

- **`Engine/Data/Shaders/Model/Model.frag`** — `MODEL_SPEC_AA_MODE` define; `alphaRoughnessAA` kernel at `main()` scope; two `D_GGX` argument swaps (lines 275, 348).
- **`Engine/Data/Shaders/ShaderLayoutsBase.h`** — two new `MainLayout` `fPbr*` fields (dual-language `INIT` fields, end of `fPbr*` block).
- **`Engine/Source/Ui/PbrWrappersBase.h` / `.cpp`** — two `Wrapper` sliders, `// Pbr - BRDF` group.
- **`Engine/Source/Ui/Screens/TweaksScreen/TweaksScreenPbr.cpp`** — `gPbrRegistrar` entries + `RenderPbrSection()` sliders, BRDF group.
- **`Engine/Source/Graphics/Render/LightingUniforms.cpp`** — two assignments in `RenderLightingMain`'s `rMainLayout.fPbr*` block.
- **`Engine/Source/Graphics/Managers/DynamicPipelines.cpp`** — (optional, final step, user-gated) `kSampleShading` removal at line 80.
- **`Engine/Data/Shaders/Model/AGENTS.md`** — one specular-antialiasing architecture-note bullet.

## Risk tier and invariants

**Tier 2 — scoped behavior** (client graphics/tool behavior in one subsystem; no excluded surface touched). Invariant exposure: **client/graphics-only.** The two new tuning uniforms are `MainLayout` fields (per-frame uniform buffer, populated CPU-side from Ui wrappers each frame — same class as the existing `fWaterSpecAA*` / `fPbr*` fields). `MainLayout` is not serialized to `.pack`, not part of sim frame state, not CRC'd. **No determinism / CRC / `kiVersion` / `.pack` / wire exposure.** Requires a DataPacker shader repack (the `#define` mode select and shader edits are compile-time, edit-and-repack, exactly like `WATER_SPEC_AA_MODE`).

## Acceptance criteria

- With `MODEL_SPEC_AA_MODE 1` and `kSampleShading` still present, low-roughness metal models show no specular flicker under camera/object motion at the RTS zoom range; `MODEL_SPEC_AA_MODE 0` reproduces the flicker (baseline).
- `fPbrSpecAAVariance` / `fPbrSpecAAThreshold` sliders visibly broaden/dim vs. clamp the highlight and appear in the Pbr tweaks screen (BRDF section) with no missing/orphaned-registration audit report.
- Client and server both build (dual-language `MainLayout` fields compile server-side unused); DataPacker shader repack succeeds with no new GLSL warnings.
- (Final, user-confirmed) with `kSampleShading` dropped, no visible specular or edge regression vs. the sample-shaded build; per-sample shading no longer runs on models when `gSampleShading` is on.

## Notes

- **Zoom/minification residual — deferred; reuse the Water mip-variance bake if it surfaces.** Screen-space-derivative AA (Design §1) cannot see variance the mip chain already averaged away, so low-roughness models may still flicker under camera zoom after this lands — the same failure the water shader fixed with its `WATER_SPEC_AA_MIP_HANDOFF` term. Model normal maps are regular-path BC5, so DataPacker already bakes their per-mip Toksvig variance into `TextureHeader::pfMipVariance` (`Common/DataFile.h:369`). The remaining work, only if the residual is visible in A/B: read the model normal map's table (bindless CRC-to-header lookup), compute an analytic LOD (or `textureQueryLod`), and add the variance into `alphaRoughnessAA`'s kernel in GGX `alpha^2` domain — no pack-format change needed. This is follow-up work, not part of this plan's scope ceiling.
- **Decisions (2026-07-03, both resolved in Design §1/§2):** (1) tuning uniforms — model-specific `fPbrSpecAA*` (separate GGX-alpha^2 tuning domain from Water's Phong-power domain); (2) widen `D` only (V is a smooth non-aliasing term; matches Water's NDF-only precedent). Re-verified 2026-07-24 against current code: D/V call pairs at `Model.frag:275-276` and `:348-349`.
- **Compile-time mode + repack:** `MODEL_SPEC_AA_MODE` is edit-and-repack like `WATER_SPEC_AA_MODE` — DataPacker shader rebuild required after any shader edit.
- Expected effort Small-Medium: the shader edit is a localized additive change following a landed precedent; the bulk is the uniform/Ui wiring, the sample-shading investigation re-confirmation, and A/B tuning.
