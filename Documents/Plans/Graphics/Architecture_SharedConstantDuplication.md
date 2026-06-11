# Architecture: Shared-Constant Duplication

## Context

Source: /external-architecture-review on `Engine/Source/Graphics` (non-recursive). Four constants whose
correctness depends on agreement between two or more sites are currently synchronized by comment (or not at
all). Each item names the constant once and points every consumer at the single definition so the pairing is
compiler-enforced.

## Design

### Magic 8 shadow-block multiplier — Engine/Source/Graphics/Graphics.cpp + Engine/Data/Shaders/ShaderLayoutsBase.h
- `FullDetail` (`Graphics.cpp:18`) and `WaterFullDetail` (`Graphics.cpp:51`) both compute
  `iBlockSize = 8 * shaders::kiShadowTextureExecutionSize` — a duplicated magic product that must stay
  identical between the two functions (WaterFullDetail's block snap is documented as "matches FullDetail()",
  `Graphics.cpp:47-48`). Hoist the product into one named constant (e.g. `kiDetailBlockSize`, file-local in
  `Graphics.cpp` next to `FullDetail`, or beside `shaders::kiShadowTextureExecutionSize` at
  `ShaderLayoutsBase.h:146`) and use it at both sites. Also fix the stale comment at `Graphics.cpp:17`
  ("must match max iShadowTextureY divisor"): `kiShadowTextureExecutionSize` is referenced nowhere else in
  the repo, no code site divides `iShadowTextureY` by 8 (`RenderTargetTextures.cpp:80-92` is
  multiplier-and-clamp only), and `RenderTargetTextures.cpp:72-74` documents that shadow dispatch
  ceil-divides — no block-multiple requirement survives. Replace the comment with the real invariant (the
  FullDetail/WaterFullDetail snap pairing). [~15m]

### Reference-resolution literals — Engine/Source/Graphics/Graphics.cpp
- `3840` appears twice under different names (`kiReferenceWidth` `:49`, `kfReferencePixels` `:70`) and
  `8192.0f` (`:72`, the smoke-sim pixel scale at reference width) has no name at all. Define the reference
  render width once (shared by `WaterFullDetail` and `SmokeSimulationPixels`; `kiReferenceHeight = 2160`
  `:50` can stay local) and name the `8192.0f` for what it is. The `Graphics/CLAUDE.md` "3840-px reference
  width" prose then has one symbol to cite. [~15m]

### Eye-height minimum duplicated across layers — Engine/Source/Graphics/CameraBase.{h,cpp} + Projects/.../Graphics/Camera.cpp
- `CameraBase.cpp:128` hard-codes `constexpr float kfMinEyeHeight = 150.0f`; the game's zoom clamp source of
  truth is `kfEyeHeightMin = 150.0f` (`Projects/BrokenEngineSandbox/Source/Graphics/Camera.cpp:18`), and the
  engine comment at `CameraBase.h:69` cites the game name. The pairing is comment-enforced only: if the game
  ever lowers its minimum zoom, the engine LOD pivot silently keeps anchoring at 150 (all closer eye heights
  clamp into LOD bucket 0 via the `max(eyeDist, kfMinEyeHeight)` term at `CameraBase.cpp:129`, so the LOD
  table never gains density below the stale pivot), and the zoom-bucket rationale comment at
  `CameraBase.h:69-72` goes stale. Define the constant once as a `protected static constexpr` on
  `CameraBase` and have the game's clamp reference it (game derives from `CameraBase`, so engine→game
  direction is clean). Update the `CameraBase.h:69` comment. [~15m]

### glTF channel enums — Common + Engine/Source/Graphics/AnimationData.cpp + DataPacker
- `AnimationData.cpp` branches on raw glTF enum values shared with the DataPacker exporter:
  `uiInterpolation == 2` (CUBICSPLINE, `:129`) / `== 0` (STEP, `:223`), and `uiTargetPath == 0/1/2`
  (translation/rotation/scale, `:185`, `:236`, `:284-297`). Add named enumerators on
  `common::AnimationChannel` (where the fields live) and use them at both the runtime read sites and the
  DataPacker write sites (locate via grep for `uiInterpolation`/`uiTargetPath` assignments in
  `DataPacker/Source/`). Values unchanged — names only, so no `.pack` rebuild is required. [~30m]

## Critical files
- `Engine/Source/Graphics/Graphics.cpp`
- `Engine/Data/Shaders/ShaderLayoutsBase.h` (only if the block constant lands there)
- `Engine/Source/Graphics/CameraBase.h`, `CameraBase.cpp`
- `Projects/BrokenEngineSandbox/Source/Graphics/Camera.cpp`
- The `common::AnimationChannel` definition (Common data layouts) + DataPacker exporter write sites
- `Engine/Source/Graphics/AnimationData.cpp`

## Out of scope
- Changing any constant's *value* — every item is a naming/single-sourcing change with bit-identical
  behavior.
- The `ScaleEngineToMeters` literal rescaling pass (`Frame/ScaleEngineToMeters.md`) — see Notes for the
  ordering constraint on the eye-height item.
- `AnimationChannel` layout or `kiVersion` changes — enumerators only, no struct edits.

## Acceptance criteria
- Each constant has exactly one definition; all former duplicate sites reference it; client + DataPacker
  build clean; no behavioral change (identical values).

## Notes
- No determinism/CRC exposure: items 1–3 are client render sizing/camera; item 4 renames values on a
  load-time parse path without changing them. No `.pack`/`kiVersion` impact.
- Ordering: the eye-height item shares the 150.0f literals with `Frame/ScaleEngineToMeters.md`'s catalog
  (`Camera.{h,cpp}`, `CameraBase.cpp`) — co-schedule or land after it so the single-sourced constant carries
  the post-rescale value.

## Verification Notes
Verified: all cited paths/lines/symbols re-checked against source.
- Item 1 (rewritten): `8 *` confirmed at `Graphics.cpp:18` and `:51`;
  `shaders::kiShadowTextureExecutionSize = 64` at `ShaderLayoutsBase.h:146`. The original item claimed "the
  divisor logic lives in `Managers/RenderTargetTextures.cpp:84-92`" — false: those lines are
  headroom-multiply-and-clamp with no `/8`, `kiShadowTextureExecutionSize` appears nowhere outside
  `Graphics.cpp:18/:51` and its declaration, and `RenderTargetTextures.cpp:72-74` explicitly states the
  dispatch ceil-divides (no block-multiple requirement). Item rewritten to dedup the two real sites and
  correct the stale `Graphics.cpp:17` comment instead of threading the constant into a third site that does
  not exist; `RenderTargetTextures.cpp` dropped from Critical files.
- Item 2: `kiReferenceWidth = 3840` `:49`, `kiReferenceHeight = 2160` `:50`, `kfReferencePixels = 3840.0f`
  `:70`, unnamed `8192.0f` `:72` — all exact.
- Item 3: `kfMinEyeHeight = 150.0f` at `CameraBase.cpp:128`; game `kfEyeHeightMin = 150.0f` at
  `Camera.cpp:18`; `CameraBase.h:69` comment cites the game name. The original "LOD-bucket silently
  degrades / bucket 0 becomes reachable" rationale was overstated (the equation clamps via
  `max(eyeDist, kfMinEyeHeight)`; zoom bucket 0 needs eye distance < 1) — corrected to the real exposure
  (stale pivot anchoring + stale comment). `Frame/ScaleEngineToMeters.md` exists and catalogs both literals
  (`CameraBase.cpp:128`, game `Camera.cpp` — its own `:23-24` citation has drifted; current line is `:18`),
  so the ordering note stands.
- Item 4: `common::AnimationChannel` fields at `Common/DataFile.h:122-123` (with `sizeof == 12`
  static_assert — enumerators don't change layout, so no `.pack`/version impact, as claimed);
  `AnimationData.cpp` reads at `:129` (`==2`), `:223` (`==0`), `:185`/`:236` (`==1`), `:284-297` (switch
  0/1/2 + default ASSERT); DataPacker write sites `SceneAnimationLoader.cpp:92,96,100` (`uiTargetPath`) and
  `:110,114,118` (`uiInterpolation`), plus exporter-side reads `:128,130,142,168,173` to convert.
