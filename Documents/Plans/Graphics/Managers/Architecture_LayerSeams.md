# Architecture: Layer Seams (Graphics/Managers → game)

## Context

Source: /external-architecture-review on `Engine/Source/Graphics/Managers` (non-recursive). Two engine→game
references that go beyond the sanctioned `game::gp*` reads: a direct call to a game static function from a
manager constructor, and two engine cpps including the game's `Game.h` solely for `game::Camera` static
constants whose natural home is the engine's `CameraBase`.

## Design

### Engine/Source/Graphics/Managers/PipelineManager.cpp
- Replace the direct game call `game::FrameInterpolate::GraphicsResources()` at `PipelineManager.cpp:141`
  (end of the `PipelineManager` constructor; the function is defined at
  `Projects/BrokenEngineSandbox/Source/Frame/Frame.cpp:61`). This is engine code invoking a game symbol by
  name — neither a `GameBase` virtual nor a `gp*` read. **Boot-order verified (see Verification Notes):
  `game::gpGame` is NOT alive at boot-time PipelineManager construction** (`Main.cpp` constructs `Graphics`
  before `game::Game`), though it IS alive on pipeline-tier recreates. A `GameBase` virtual hook through
  `gpGame` therefore cannot fire at boot — use a registered-callback / static-registration mechanism instead
  (e.g. a function pointer the game installs before `Graphics` construction, or keep the engine-side
  declaration pattern); shape is the grill decision. [~30m]

### game::Camera headroom constants → engine CameraBase
- Move `game::Camera::kfLightingHeadroomMultiplier` (read at `TextureManager.cpp:43,45`) and
  `game::Camera::kfShadowHeadroomMultiplier` (read at `RenderTargetTextures.cpp:82,84`) onto the engine's
  `CameraBase` (`Engine/Source/Graphics/CameraBase.h`) as `static constexpr`, since the consumers are engine
  render-target sizing code; update any game-side uses to reference the base. Then drop the now-unneeded
  `#include "Game.h"` at `TextureManager.cpp:10` and `RenderTargetTextures.cpp:10` (coordinates with
  `Architecture_IncludeHygiene.md`, same files). Precedent: `Graphics/Architecture_SharedConstantDuplication.md`
  item 3 does the same single-sourcing for `kfMinEyeHeight` on `CameraBase`. [~30m]

## Critical files
- `Engine/Source/Graphics/Managers/PipelineManager.cpp`
- `Engine/Source/GameBase.h` (+ its cpp if the hook needs a default body)
- `Projects/BrokenEngineSandbox/Source/Game.h`/`Game.cpp` (override), `Projects/BrokenEngineSandbox/Source/Graphics/Camera.h` (constants move out)
- `Engine/Source/Graphics/CameraBase.h` (constants move in)
- `Engine/Source/Graphics/Managers/TextureManager.cpp`, `RenderTargetTextures.cpp` (include drops)

## Out of scope
- `ImGuiManager` owning eight `game::*Screen` members — was the subject of
  `Graphics/ParentRuleFramingReconciliation.md` Decision 2 (**landed** — sanctioned-exception carve-out in
  `Engine/Source/CLAUDE.md` + `ImGuiManager.CLAUDE.md`; code change de-recommended there). Not re-filed.
- `InstanceManager.cpp` reads of `game::kGameName`/`game::kiGameVersion` (`:128-129,313,449-469`) — constant
  reads in boot/error-dialog paths, same spirit as the sanctioned `game::gp*` reads; left as-is.
- Engine reading `game::gp*` globals anywhere — by design per root CLAUDE.md.

## Acceptance criteria
- No `game::` symbol reference remains in `PipelineManager.cpp` / `TextureManager.cpp` /
  `RenderTargetTextures.cpp` beyond sanctioned `gp*` reads; client + server build clean; pipeline-recreate
  behavior unchanged (hook fires exactly where the old call sat).

## Notes
- No determinism/CRC exposure — client render boot path only. `GraphicsResources()` timing must not move
  relative to pipeline creation (it re-acquires pipeline pointers); the hook replaces the call in place.
- One grill decision staged: the shape of the deferred-call mechanism (gpGame-hook-only is ruled out by the
  verified boot order — see Verification Notes).
- Shares `TextureManager.cpp`/`RenderTargetTextures.cpp` with `Architecture_IncludeHygiene.md` — co-schedule.

## Verification Notes

Verified against source 2026-06-11 (verification pass for the /external-deep-analysis run):

- `game::FrameInterpolate::GraphicsResources()` call confirmed at `PipelineManager.cpp:141` (last statement of
  the ctor); definition at `Projects/BrokenEngineSandbox/Source/Frame/Frame.cpp:60-72` (`BT_CLIENT`-wrapped).
- **Boot order resolved**: `Engine/Source/Main.cpp` constructs `Graphics` (which constructs `PipelineManager`)
  at ~`:189` and `game::Game` at ~`:193` — `gpGame` is nullptr during the boot-time call. The current direct
  static call works only because `GraphicsResources()` touches no game-object state. On pipeline-tier destroy
  the whole `PipelineManager` is reconstructed with `gpGame` alive, so any replacement must work in both
  states. The item text was updated accordingly; the original "GameBase virtual hook, default empty" proposal
  is not viable as the boot-time mechanism.
- Headroom-constant reads confirmed at `TextureManager.cpp:43,45` (`kfLightingHeadroomMultiplier`) and
  `RenderTargetTextures.cpp:82,84` (`kfShadowHeadroomMultiplier`); both declared `static constexpr` on
  `game::Camera` (`Projects/.../Graphics/Camera.h:32-33`). Other readers: `game::Camera`'s own cpp and the
  engine's `GlobalUniforms.cpp` — update those references when the constants move to `CameraBase`
  (`Engine/Source/Graphics/CameraBase.h` exists; `game::Camera : public engine::CameraBase` confirmed).
- `InstanceManager.cpp` `game::kGameName`/`kiGameVersion` reads confirmed at `:128-129, 313, 449-469` —
  correctly left out of scope.
- Cross-references verified: `Graphics/ParentRuleFramingReconciliation.md` Decision 2 (since **landed**) covers the ImGuiManager
  screen ownership (no overlap); `Graphics/Architecture_SharedConstantDuplication.md` item 3 is the
  `kfMinEyeHeight` precedent as cited.
