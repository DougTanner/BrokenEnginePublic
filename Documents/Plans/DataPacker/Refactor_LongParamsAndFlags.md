# Refactor: Long Parameter Lists and Bool Proliferation

Source: /external-refactor-clean on DataPacker/Source/

Goal: three functions violate the 5-6 parameter guideline; one class scatters related `bool` knobs across multiple call sites that should be self-documenting at call time.

## Changes

### DataPacker/Source/Texture.h, Texture.cpp — replace unlabeled bools with `common::Flags`
Three distinct `bool` parameters appear across the `Texture` API:
- `bool bFromGamma` — ctor at `Texture.h:17`
- `bool bVerifyNoAlpha` — `ToBc7` at `Texture.h:34`, `Export` at 38/40, `Save` at 47
- `bool bUseBoxFilter` — `MakeMipmaps` at `Texture.h:22,24`

No single function takes all three — they are distributed across the API. Call sites still suffer from unlabeled booleans (`texture.Save(path, format, false);` — what does `false` mean?). Introduce `enum class TextureOptions` with `kFromGamma`, `kVerifyNoAlpha`, `kUseBoxFilter` and replace each `bool` parameter with `common::Flags<TextureOptions>`. Call sites become `texture.Save(path, format, TextureOptions::kVerifyNoAlpha)` or `{}` for default [~30m]

- Update Texture call sites in `ExportIsland.cpp`, `ExportTexture.cpp`, and internal `Texture.cpp` uses [~15m]

### DataPacker/Source/ExportJobs/ExportSceneVertices.cpp
- Line 52 `LoadVertices` — 9 parameters. Wrap `rVertices, rMaterials, rMaterialNodeInfos, rMaterialNodeMap, rNodeToJointMap` into a `LoadVerticesContext` struct; keep `pParent, iCurrentNodeIndex, rNode, rModel` explicit [~15m]

### DataPacker/Source/ExportJobs/ExportSceneAnimation.cpp
- Line 26 `LoadAnimations` — 6 parameters, 4 of which are output vectors. Wrap the 4 outputs in an `AnimationOutput` struct [~15m]

### DataPacker/Source/ExportJobs/ExportShader.cpp
- Line 101 `WriteBinding` — 7 parameters. Wrap into a `BindingEntry` designated-init struct; callers use `WriteBinding({.pBindings = ..., .pSetIndices = ..., .iBinding = ..., .uiSet = ..., .vkDescriptorType = ..., .iDescriptorCount = ..., .chunkFlags = ...})` which is strictly more readable at the 6 call sites [~15m]

## Expected Outcome

- Every `Texture` call site names its options at construction/save time instead of carrying an unlabeled bool
- No function in DataPacker exceeds 6 parameters
- SPIRV-Cross binding-emit code becomes diff-friendly (adding a new field requires one struct member, not touching every call site)

## Verification Notes

- Corrected the overstatement that "each function accepts all three bools." The three bools (`bFromGamma`, `bVerifyNoAlpha`, `bUseBoxFilter`) are distributed — no single signature takes all of them. Flags refactor still has value for documentation at call sites; rewording clarifies the real benefit.
- REMOVED the bullet citing `Texture.cpp:228,232` as call sites needing updates. Those lines are `stbir_resize` / `stbir_resize_float_linear` calls inside `MakeMipmaps` — they are NOT call sites for the `Texture` ctor or encoder methods. Kept generic "ExportIsland.cpp, ExportTexture.cpp, and internal Texture.cpp uses" as the update target.
- REMOVED the subsumed `MakeMipmaps` 6-param entry. After the flags refactor it becomes 4 params, which is below threshold. No separate action needed.
- Corrected `WriteBinding` call-site count from 7 to 6 (verified: `ExportShader.cpp:307,327,347,367,389,403`).
