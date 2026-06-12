# Architecture: Render Shared-Constant Duplication

## Context

Source: /external-architecture-review on `Engine/Source/Graphics/Render` (non-recursive). Two constants whose
correctness depends on cross-site agreement are synchronized by comment (or by repeating the arithmetic), exactly
the defect class `Graphics/Architecture_SharedConstantDuplication.md` fixes for the parent directory. Single-source
each so the pairing is compiler-enforced.

## Design

### kfWaveFadeEnd — Engine/Source/Graphics/Render/GlobalUniforms.cpp + LightingUniforms.cpp + game Camera.h
- `static constexpr float kfWaveFadeEnd = 2.0f * game::Camera::kfCameraEyeHeightDefault;` exists byte-identically
  as two independent function-local constants (`GlobalUniforms.cpp:546` in `PopulateWaterParameters`,
  `LightingUniforms.cpp:117` in `RenderLightingMain`), each feeding an identical `engine::LerpAtHeight(...)` call
  (`:547` / `:118`). `Render/CLAUDE.md:45` documents "the `kfWaveFadeEnd` factor … must stay matched" — i.e. the
  invariant is currently enforced only by a doc sentence. Hoist to one shared definition and reference it at both
  sites; recommended home: `game::Camera` beside `kfCameraEyeHeightDefault`
  (`Projects/BrokenEngineSandbox/Source/Graphics/Camera.h:23`), e.g. `kfWaveFadeEndHeight = 2.0f * kfCameraEyeHeightDefault` —
  both TUs already read `game::Camera` constants (sanctioned engine→game direction), and `Render.h` cannot host it
  (`game::Camera` is incomplete at `Engine.h:61`, before `GameBase.h` at `:96`). Update `Render/CLAUDE.md:45` to
  state the constant is single-sourced instead of "must stay matched". [~10m]

### Elevation-texture 1.5x width — GlobalUniforms.cpp + Managers/RenderTargetTextures.cpp
- The shadow-elevation texture is allocated 1.5x the shadow texture's width
  (`RenderTargetTextures.cpp:92` area: `iShadowTextureX + iShadowTextureX / 2`, width forced even at `:83` so the
  division is exact), but `PopulateShadowParameters` re-derives the factor as independent arithmetic:
  `fShadowElevationTextureSizeWidth = fShadowTextureSizeWidth + 0.5f * fShadowTextureSizeWidth`
  (`GlobalUniforms.cpp:175`). Replace the recompute by reading the created extent —
  `gpTextureManager->mRenderTargetTextures.mShadowElevationTexture.mInfo.extent.width` — exactly how the same
  function already reads the shadow texture's actual extent at `:173-174`, whose own comment (`:242-243`) gives the
  rationale: reading the actual (clamped) extent keeps coverage device-clamp-invariant. The 1.5x factor then has a
  single owner (the allocation site). [~10m]
- While in the block: `iShadowStartOffset = static_cast<int>(fShadowTextureSizeWidth / 2.0f)` (`:337`) is the same
  extension scheme's half-width; once the elevation extent is read directly, the offset can be expressed as
  `elevationWidth - shadowWidth` so all three quantities derive from the two real extents instead of repeated
  ratios. Optional consistency step — keep only if it reads cleaner. [~5m]

## Critical files
- `Engine/Source/Graphics/Render/GlobalUniforms.cpp`
- `Engine/Source/Graphics/Render/LightingUniforms.cpp`
- `Projects/BrokenEngineSandbox/Source/Graphics/Camera.h`
- `Engine/Source/Graphics/Managers/RenderTargetTextures.cpp` (read-only reference — allocation stays as is)
- `Engine/Source/Graphics/Render/CLAUDE.md` (the `:45` "must stay matched" sentence)

## Out of scope
- Changing any constant's value — both items are single-sourcing with bit-identical results on every device: the
  width cap and even-forcing (`RenderTargetTextures.cpp:82-83`) run before *both* allocations, so the created
  elevation extent always equals exactly 1.5x the created shadow extent that `GlobalUniforms.cpp:173` already
  reads back.
- The parent directory's constant dedup (`Graphics/Architecture_SharedConstantDuplication.md`) — separate files,
  no overlap.
- The magic moduli (`10.0` family) in the water precision reduction — deliberate, documented
  (`Render/CLAUDE.md:30-33`).

## Acceptance criteria
- `kfWaveFadeEnd` has exactly one definition; both former sites reference it; the elevation width used by
  population is read from the created texture extent. Client builds clean; rendering identical at default settings.

## Notes
- No determinism/CRC/network/`kiVersion` exposure — client render parameters only.
- No `.pack`/shader recompile: no shader-layout member changes.

## Verification Notes

Verified 2026-06-11 against current source; both items confirmed.
- `kfWaveFadeEnd`: declarations at `GlobalUniforms.cpp:546` and `LightingUniforms.cpp:117` are byte-identical
  (`static constexpr float kfWaveFadeEnd = 2.0f * game::Camera::kfCameraEyeHeightDefault;`), each feeding
  `engine::LerpAtHeight` at `:547`/`:118`; `Render/CLAUDE.md:45` "must stay matched" sentence confirmed;
  `kfCameraEyeHeightDefault` at game `Camera.h:23`; `Render.h` host rejection confirmed (`Engine.h` includes
  `Render.h` at `:61`, `GameBase.h` — which pulls the game `Graphics/Camera.h` — at `:96`).
- Elevation 1.5x: allocation at `RenderTargetTextures.cpp:92` (`iShadowTextureX + iShadowTextureX / 2`, width
  even-forced at `:83`, both inside `CreateShadowTextures`, created before the render loop); recompute at
  `GlobalUniforms.cpp:175`; the same function already reads the shadow texture's created extent at `:173-174`
  with the device-clamp rationale comment at `:242-243`. `mShadowElevationTexture.mInfo.extent.width` is readable
  at the population point. Out-of-scope prose corrected: the swap is bit-identical on *all* devices (clamp runs
  before both allocations), so the benefit is single ownership of the factor, not behavior.
- Optional `iShadowStartOffset` step: `:337` confirmed; `elevationWidth − shadowWidth == shadowWidth / 2` exactly
  (width even), so the rewrite is value-preserving.
- No items dropped; no score adjustment suggested.
