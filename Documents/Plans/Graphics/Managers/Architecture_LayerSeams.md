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
  `Projects/BrokenEngineSandbox/Source/Frame/Frame.cpp:61`) with a `GameBase` virtual hook (e.g.
  `GameBase::OnGraphicsPipelinesCreated()`, default empty, game override calls
  `FrameInterpolate::GraphicsResources()`). This is engine code invoking a game symbol by name — neither a
  `GameBase` virtual nor a `gp*` read. Verify first that `game::gpGame` is alive when `PipelineManager` is
  constructed (boot order); if it is not, use a deferred-call mechanism instead (grill decision). [~30m]

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
- `ImGuiManager` owning eight `game::*Screen` members — already the subject of
  `Graphics/ParentRuleFramingReconciliation.md` Decision 2 (doc-framing decision; code change de-recommended
  there). Not re-filed.
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
- One grill decision staged: hook vs deferred-call if `gpGame` turns out not to be alive at PipelineManager
  construction.
- Shares `TextureManager.cpp`/`RenderTargetTextures.cpp` with `Architecture_IncludeHygiene.md` — co-schedule.
