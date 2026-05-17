# Plan: Collapse Height-Lerped Wrapper Quartets Into `HeightLerpWrapperQuartet` + Free `LerpAtHeight`

## Context

The "Camera-Height-Conditional Uniforms" pattern documented at `Engine/Source/Graphics/Render/CLAUDE.md` says every author-facing height-lerped scalar is exposed as four `Wrapper` globals (`*StartHeight`, `*EndHeight`, `*Low`, `*High`). Every quartet repeats the same boilerplate four times across declaration, definition, slider-map registrar, slider row, and consumer-side `Get()` reads.

### Current quartets in the engine (5 total → 20 `Wrapper` globals)

Audio side — `Engine/Source/Ui/SoundSettingsWrappersBase.{h,cpp}`, consumed by `StaticVoices::UpdateListenerPosition` (`Engine/Source/Audio/StaticVoices.cpp`):

1. `gListenerDistanceStart{StartHeight,EndHeight,Low,High}` → `mfEffectiveFadeStart`
2. `gListenerDistanceEnd{StartHeight,EndHeight,Low,High}` → `mfEffectiveFadeEnd`
3. `gListenerCurve{StartHeight,EndHeight,Low,High}` → `mfCurveDistanceScaler`
4. `gListenerAudibleFloor{StartHeight,EndHeight,Low,High}` → `mfManualFadeVolume`

Lighting side — `Engine/Source/Ui/LightingWrappersBase.{h,cpp}`, consumed by `RenderLightingGlobal` (`Engine/Source/Graphics/Render/LightingUniforms.cpp:71-77`):

5. `gSpreadDistanceEnd{StartHeight,EndHeight,Low,High}` → `rGlobalLayout.fSpreadDistanceEnd`

### Duplication footprint per quartet

| Site | Lines per quartet |
|------|-------------------|
| `*WrappersBase.h` declarations | 4 `extern Wrapper` |
| `*WrappersBase.cpp` definitions | 4 `Wrapper(…)` constructors |
| `TweaksScreen<Tab>.cpp` `gRegistrar` map entries | 4 `{"…", &gFoo}` rows |
| `TweaksScreen<Tab>.cpp` slider rendering | 4 `WrapperSlider("…", kiSection, 2.0f, "…")` calls |
| Consumer site `.Get()` reads inside the lerp | 4 `.Get()` reads passed to the lerp function |

Five quartets × 4 sites × 4 rows = **80 mechanical rows** dedicated to the quartet pattern. Consumer side adds duplicated computation: `StaticVoices::UpdateListenerPosition` (`StaticVoices.cpp:450-455`) defines a local `LerpAtHeight` lambda that is byte-identical in algebra to the inline blocks in `LightingUniforms.cpp:71-77` and to the helper proposed by the existing orphan plan `Documents/Plans/Graphics/CameraHeightLerpHelper.md`.

### Relationship to existing plans

- `Documents/Plans/Graphics/CameraHeightLerpHelper.md` (orphan — exists on disk but not registered in `Order.md`) proposes a `CameraHeightLerp` free function for three render-uniform call sites. That plan explicitly leaves audio "out of scope" and addresses only the lambda duplication, not the wrapper-quartet duplication.
- `Documents/Plans/WrapperArrayCollapse.md` (Order.md #3) is the precedent for "collapse parallel wrapper sets into a single aggregate" refactors. The shape here is the same idea applied to height-lerp quartets rather than per-render-target arrays.

This plan supersedes the orphan `CameraHeightLerpHelper.md` (the free-function part) and extends it with the wrapper-aggregate struct.

## Design

### `HeightLerpWrapperQuartet` aggregate

New header `Engine/Source/Ui/HeightLerpWrapperQuartet.h` (sits next to `WrapperBase.h` because the type composes four `Wrapper` instances and is only meaningful in Ui-level declaration contexts):

```cpp
struct HeightLerpWrapperQuartet
{
    Wrapper StartHeight;
    Wrapper EndHeight;
    Wrapper Low;
    Wrapper High;

    HeightLerpWrapperQuartet(
        float fStartHeightDefault, float fStartHeightMin, float fStartHeightMax,
        float fEndHeightDefault,   float fEndHeightMin,   float fEndHeightMax,
        float fLowDefault,         float fLowMin,         float fLowMax,
        float fHighDefault,        float fHighMin,        float fHighMax);

    float Resolve(float fEyeHeight) const;
};
```

`Resolve` calls into the free `LerpAtHeight` (below); the struct itself is dumb storage plus a forwarding convenience. No virtuals, no inheritance — aggregation only.

### `LerpAtHeight` free function

The orphan graphics plan places its helper in `Engine/Source/Graphics/Render/Render.h` and bakes `game::gpCamera->mfCameraEyeHeight` into the body. That coupling blocks audio reuse (the audio TU already reads `mfCameraEyeHeight` into a local before lerping, and `Common/` would not include game globals). This plan instead puts the helper next to the struct and takes `fEyeHeight` as an explicit argument so both audio and graphics sites can call it with the value they already have:

In `Engine/Source/Ui/HeightLerpWrapperQuartet.h` (declaration) and `Engine/Source/Ui/HeightLerpWrapperQuartet.cpp` (definition):

```cpp
float LerpAtHeight(float fEyeHeight, float fStartHeight, float fEndHeight, float fLow, float fHigh);
```

Implementation mirrors the existing audio lambda and graphics block byte-for-byte — `std::max(fEndHeight - fStartHeight, 0.001f)` floor, `std::clamp` of `fT`, `std::lerp` of low/high. Behavior identical to today.

`Quartet::Resolve(float fEyeHeight)` is `return LerpAtHeight(fEyeHeight, StartHeight.Get(), EndHeight.Get(), Low.Get(), High.Get());`.

### Migration sequence

1. Add `HeightLerpWrapperQuartet.{h,cpp}` with the struct, the free function, and an INIT-style constructor that takes the 12 floats in declaration order.
2. Replace each audio quartet block in `SoundSettingsWrappersBase.{h,cpp}` with one `extern HeightLerpWrapperQuartet gListener{DistanceStart,DistanceEnd,Curve,AudibleFloor};` declaration + one twelve-float definition.
3. Replace the lighting quartet block in `LightingWrappersBase.{h,cpp}` with `extern HeightLerpWrapperQuartet gSpreadDistanceEnd;` + the matching definition.
4. Update slider-map registrar rows in `TweaksScreenSound.cpp` and `TweaksScreenLighting.cpp`: each quartet expands to four rows that reach into the struct members (e.g., `{"Listener Distance Start Start Height", &gListenerDistanceStart.StartHeight}`) — map keys unchanged so persistence and `WrapperSlider` lookups stay identical.
5. Replace the `LerpAtHeight` lambda in `StaticVoices::UpdateListenerPosition` with four `quartet.Resolve(fEyeHeight)` calls. The local `const float fEyeHeight = game::gpCamera->mfCameraEyeHeight;` line stays.
6. Replace the inline lerp block in `RenderLightingGlobal` (`LightingUniforms.cpp:71-77`) with `rGlobalLayout.fSpreadDistanceEnd = gSpreadDistanceEnd.Resolve(game::gpCamera->mfCameraEyeHeight);`.
7. Update `Engine/Source/Graphics/Render/CLAUDE.md` "Camera-Height-Conditional Uniforms" section to point at `HeightLerpWrapperQuartet::Resolve` as the canonical implementation.
8. Update `Engine/Source/Audio/CLAUDE.md` "Volume & 3D" paragraph that names the four quartets in `SoundSettingsWrappersBase.cpp` to describe the new struct shape.
9. Delete the orphan `Documents/Plans/Graphics/CameraHeightLerpHelper.md` (its graphics call site #1 is absorbed here; sites #2 and #3 — the inverted `1 - fWaveAmplitudeScale` pair with hard-coded endpoints — are unrelated to the wrapper-quartet pattern and stay in their files unchanged, since collapsing two hard-coded-endpoint sites into a helper is a separate KISS/YAGNI call). Note the deletion in the commit message so the orphan plan does not get re-picked up.

### Consumer-side wins

- `StaticVoices::UpdateListenerPosition`: 4 lambda definitions deleted, 4 calls with 4 `.Get()`s each (16 reads) collapse to 4 `Resolve` calls (each internally does 4 `.Get()`s — same total, but the call sites are 16 fewer tokens).
- `RenderLightingGlobal`: 7-line inline block collapses to one line.
- Net code shrinks ~25 lines in consumer code; ~12 wrapper declarations + 12 wrapper definitions still exist but bound into 5 struct instances instead of 20 loose globals.

### Compatibility & invariants preserved

- **Wrapper declaration order**: `Engine/Source/Ui/Screens/TweaksScreen/CLAUDE.md` requires wrapper declaration order to match UI render order. The quartet struct keeps StartHeight/EndHeight/Low/High in their canonical UI order; declaration order across quartets in `*WrappersBase.{h,cpp}` matches the slider render order in the matching `TweaksScreen<Tab>.cpp`. No reordering required.
- **Slider map keys**: Every existing `"…"` map key is preserved verbatim so the on-disk settings persistence file (which keys by label string) is untouched. Renaming any key would require a settings-migration pass — explicitly avoided.
- **Defaults / min / max**: Each quartet's twelve floats land in the constructor argument list in the same order they appear today, so a side-by-side diff of the migrated `SoundSettingsWrappersBase.cpp` against the current file shows zero numeric drift.
- **`Wrapper(float, float, float, float)` snap-step constructor**: None of the existing 20 quartet wrappers use the optional `fStep` parameter, so the 12-float constructor signature is sufficient. If a future quartet needs snap steps, add a second constructor — out of scope here.

## Critical files

- **New**: `Engine/Source/Ui/HeightLerpWrapperQuartet.h` — struct declaration, `LerpAtHeight` free function declaration.
- **New**: `Engine/Source/Ui/HeightLerpWrapperQuartet.cpp` — constructor body, `Resolve`, `LerpAtHeight` definition.
- `Engine/Source/Ui/SoundSettingsWrappersBase.h` — replace 16 `extern Wrapper` lines (the 4 audio quartets) with 4 `extern HeightLerpWrapperQuartet`.
- `Engine/Source/Ui/SoundSettingsWrappersBase.cpp` — replace 16 `Wrapper(…)` definitions with 4 `HeightLerpWrapperQuartet(…)` definitions.
- `Engine/Source/Ui/LightingWrappersBase.h` — replace 4 `extern Wrapper` lines (the `gSpreadDistanceEnd*` quartet) with 1 `extern HeightLerpWrapperQuartet gSpreadDistanceEnd;`.
- `Engine/Source/Ui/LightingWrappersBase.cpp` — replace 4 `Wrapper(…)` definitions with 1 `HeightLerpWrapperQuartet(…)` definition.
- `Engine/Source/Ui/Screens/TweaksScreen/TweaksScreenSound.cpp` — update 16 `gSoundRegistrar` entries to reach through the struct (`&gListenerDistanceStart.StartHeight` etc.); slider rendering rows unchanged (they use the map key).
- `Engine/Source/Ui/Screens/TweaksScreen/TweaksScreenLighting.cpp` — update 4 `gLightingRegistrar` entries for the `gSpreadDistanceEnd` quartet.
- `Engine/Source/Audio/StaticVoices.cpp` — `UpdateListenerPosition`: delete the local `LerpAtHeight` lambda; rewrite the 4 assignments to call `g…Quartet.Resolve(fEyeHeight)`.
- `Engine/Source/Graphics/Render/LightingUniforms.cpp` — collapse the inline 7-line lerp block in `RenderLightingGlobal` to `rGlobalLayout.fSpreadDistanceEnd = gSpreadDistanceEnd.Resolve(game::gpCamera->mfCameraEyeHeight);`.
- `Engine/Source/Graphics/Render/CLAUDE.md` — update "Camera-Height-Conditional Uniforms" section to point at `HeightLerpWrapperQuartet::Resolve` (acknowledge canonical implementation now exists).
- `Engine/Source/Audio/CLAUDE.md` — update "Volume & 3D" paragraph naming the four audio quartets to reflect the struct shape.
- `Projects/BrokenEngineSandbox/Platforms/VisualStudio2026/*.vcxproj{,.filters}` — register `HeightLerpWrapperQuartet.{h,cpp}` in the engine-shared filter (both client and server: `Wrapper` is shared, and the struct must compile in both builds since `LightingWrappersBase` and `SoundSettingsWrappersBase` are shared TUs).
- **Delete**: `Documents/Plans/Graphics/CameraHeightLerpHelper.md` — superseded by this plan; the wrapper-quartet site it cited (#1) is absorbed here, and the two sites it cited with hard-coded endpoints (#2, #3) do not benefit from a wrapper-aggregate refactor.

## Out of scope

- The two non-quartet camera-height sites in `LightingUniforms.cpp` (`RenderLightingMain` zoom factor) and `GlobalUniforms.cpp` (water-speed zoom factor) that use hard-coded `kfCameraEyeHeightDefault` endpoints instead of author-facing `Wrapper` quartets — they do not share the duplication this plan targets. If a future audit decides to extract a separate hard-coded-endpoint helper, that is its own plan.
- Promoting `LerpAtHeight` to `Common/` — `HeightLerpWrapperQuartet` belongs in `Engine/Source/Ui/` because it owns four `Wrapper` instances; the free function is co-located there to keep the header coherent. Nothing in `Common/` includes `Wrapper`.
- Renaming existing quartet members (e.g., `gListenerDistanceStart` could become `gListenerFadeStart`) — every grep-able rename is its own diff and risks settings-key drift. Identifier names stay as they are; only the layout changes.
- Changing the slider label format in `TweaksScreenSound.cpp` / `TweaksScreenLighting.cpp` ("Listener Distance Start Start Height" reads awkwardly) — label renames would invalidate persisted settings.
- Adding new height-lerp quartets, new author-facing controls, or new consumer sites.
- Migrating the `Wrapper` class itself (constructors, snap-step semantics, `Changed<T>()` self-advancing contract).
- Replacing the `*WrappersBase.{h,cpp}` declare/define split pattern with anything else.
- Touching the GLSL shaders that consume `fSpreadDistanceEnd` — the GPU still sees a single resolved float.
- Auditing other UI screens for quartet-shaped clusters (`Pbr`, `Terrain`, `Water`, `Shadow`, `Smoke`, `Wind`, `Misc` — none appear to have height-lerp quartets today, but a full audit is its own scope).
- Coordinating with `Documents/Plans/WrapperArrayCollapse.md` (Order.md #3) — that plan addresses the per-render-target Sun/Moon quartet pattern (orthogonal axis: render target index, not eye height). The two plans can land in either order.

## Acceptance criteria

- `Engine/Source/Ui/HeightLerpWrapperQuartet.h` exists and declares both the struct and the free `LerpAtHeight` function.
- Grep `extern Wrapper g(Listener|SpreadDistanceEnd).*(StartHeight|EndHeight|Low|High)` returns zero matches across `Engine/Source/Ui/`.
- Grep `LerpAtHeight` returns one declaration (in the new header), one definition (in the new `.cpp`), and four consumer-side `Resolve` calls in `StaticVoices.cpp` + one in `LightingUniforms.cpp`. No remaining lambda named `LerpAtHeight` in any TU.
- The wrapper-quartet `Wrapper` global count drops from 20 to 0; replaced by 5 `HeightLerpWrapperQuartet` instances.
- Slider-map registrar entries are unchanged in count (20 entries, same label strings) — each entry now points to a member of a struct instead of a free `Wrapper`. Persisted settings load without migration.
- `RenderLightingGlobal` spread-distance-end block is one line (`gSpreadDistanceEnd.Resolve(…)`); no `std::max` / `std::clamp` / `std::lerp` calls remain in that block.
- `StaticVoices::UpdateListenerPosition` contains zero lambdas; the four assignments call `Resolve(fEyeHeight)`.
- `Documents/Plans/Graphics/CameraHeightLerpHelper.md` is deleted; no reference to it remains in `Documents/Plans/`.
- Engine client and engine server both compile (the struct lives in shared Ui code; the only client-only consumer is `StaticVoices.cpp`, gated by `BT_CLIENT`).
- Tweaks > Sound > Tweaks sub-tab and Tweaks > Lighting Effects > Visible/Lighting tabs render the same 20 sliders with the same labels in the same order; dragging each slider produces the same audio/lighting response as before.

## Notes

- Effort 3, Impact 2, Risks 1, Score 2. Tier Medium.
- This plan supersedes `Documents/Plans/Graphics/CameraHeightLerpHelper.md` (orphan — not in `Order.md`). The two non-quartet sites that orphan plan cites (`RenderLightingMain` and `GlobalUniforms.cpp` water-speed zoom factor, both using hard-coded `kfCameraEyeHeightDefault` endpoints) are intentionally left untouched because they do not share the wrapper-aggregate refactor target.
- Discovery confirms 5 quartets total (4 audio + 1 lighting). A future-second-quartet scenario would amortize the abstraction immediately; with 5 already, the break-even is already past.
