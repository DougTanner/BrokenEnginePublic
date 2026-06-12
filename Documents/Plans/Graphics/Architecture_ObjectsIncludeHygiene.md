# Architecture: Include Hygiene (Engine/Source/Graphics/Objects)

## Context

Source: /external-architecture-review on `Engine/Source/Graphics/Objects` (non-recursive). The directory is
fully ExternalHeaders.h-compliant (zero `#include <...>` in all 16 files) and carries only 13 local include
directives, all used. The gaps are the inverse problem: symbols arriving only through load-bearing
`Engine.h` ordering or a sibling's transitive include, in the two places the codebase convention says they
should be declared — headers (the aggregation span must stay order-independent) and non-aggregated wrapper
headers.

## Design

### Engine/Source/Graphics/Objects/Pipeline.h
- Add `#include "Buffer.h"` — `Pipeline.h` forward-declares `Shader`/`Texture` (`:6-7`) but uses `Buffer`
  with no declaration of any kind: `Buffer* pBuffers` (`:50`), `Buffer* pVertexBuffer` (`:93`), parameter
  (`:131`), and the by-value member `Buffer mModelMaterialsStorageBuffer` (`:159`) which requires the
  complete type. Compiles today only because `Engine.h:32` (Buffer.h) precedes `Engine.h:34` (Pipeline.h).
  Precedent: `ModelPipeline.h:3` includes `Pipeline.h` for its by-value members; same fix shape as the
  Frame hygiene plan's `Collision.h`→`Alignments.h` item. The by-value member rules out a forward
  declaration. [~5m]

### Engine/Source/Graphics/Objects/ModelPipeline.h
- After the `Pipeline.h` fix, `Buffer` (used as `Buffer* pBuffer` param, `:27`) arrives through the
  file's own `Pipeline.h` include — verify at execution; if preferring declare-what-you-use, add
  `class Buffer;` beside the namespace top, mirroring `PipelineDescriptorWriter.h:7`. [~2m]

### Engine/Source/Graphics/Objects/PipelineCreator.cpp
- Add `#include "Ui/WrapperBase.h"` — `gWireframe` (`:525`) is declared in `WrapperBase.h:213` and arrives
  only transitively via `GraphicsSettingsWrappersBase.h:3` (the file's `:5` include, whose own wrappers
  `gMultisampling`/`gSampleCount`/`gSampleShading`/`gMinSampleShading` `:566-568` it legitimately uses).
  The Ui wrapper headers are deliberately non-aggregated, so the convention is to include the specific
  header you consume — same item shape as `Graphics.cpp` in `Graphics/Architecture_IncludeHygiene.md`. [~5m]

## Critical files
- `Engine/Source/Graphics/Objects/Pipeline.h`
- `Engine/Source/Graphics/Objects/ModelPipeline.h`
- `Engine/Source/Graphics/Objects/PipelineCreator.cpp`

## Out of scope
- `.cpp` files using symbols from `Engine.h`-aggregated headers via the PCH (`Buffer::CreateBuffer` in
  `PipelineCreator.cpp`/`Texture.cpp`, `Shader` uses, `OneShotCommandBuffer` in `Buffer.cpp`/`Texture.cpp`,
  `kfMinDepth`/`kfMaxDepth` from `Texture.h` in `PipelineCreator.cpp:317-318`) — accepted aggregation
  convention; policy wording is `Common/AggregationAndScopeRuleQualifiers.md`'s decision.
- Relocating `kfMinDepth`/`kfMaxDepth` out of `Texture.h` (odd ownership for viewport depth constants used
  by pipeline creation) — cosmetic placement, not filed.
- The neighbor Managers headers riding the same `Engine.h` ordering (`RenderTargetTextures.h`,
  `CommandBufferManager.h`, `DynamicPipelines.h`) — outside this run's target directory.

## Acceptance criteria
- Client builds clean; `Pipeline.h` no longer depends on `Engine.h` ordering for `Buffer`;
  `PipelineCreator.cpp` compiles with `GraphicsSettingsWrappersBase.h` hypothetically not chaining
  `WrapperBase.h`.

## Notes
- Includes-only, compile-checked; no determinism/CRC, `kiVersion`, replay, guard-scope, or allocation
  exposure. No grill decisions.

## Verification Notes
Verified: all cited paths/lines/symbols re-checked against source.
- `Pipeline.h` — forward-declares only `Shader`/`Texture` (`:6-7`); `Buffer` used with zero declaration at
  `:50` (`Buffer* pBuffers`), `:93` (`Buffer* pVertexBuffer`), `:131` (parameter), and the by-value
  `Buffer mModelMaterialsStorageBuffer` (`:159`), which requires the complete type. `Engine.h:32`
  (`Buffer.h`) precedes `Engine.h:34` (`Pipeline.h`) — the ordering dependency is exactly as stated.
- `ModelPipeline.h:3` includes `Pipeline.h`; its own `Buffer*` use is at `:27`
  (`UpdateStorageBufferDescriptors` parameter). `PipelineDescriptorWriter.h:7` carries the cited
  `class Buffer;` precedent.
- `PipelineCreator.cpp` — `gWireframe` read at `:525`, declared `WrapperBase.h:213`; the file's `:5` include
  is `Ui/GraphicsSettingsWrappersBase.h`, whose `:3` chains `WrapperBase.h`; its own wrappers
  `gMultisampling`/`gSampleCount`/`gSampleShading`/`gMinSampleShading` legitimately used at `:566-568`.
  `Engine/Source/Ui/CLAUDE.md` confirms wrapper headers are deliberately non-aggregated — include the
  specific header consumed. Caveat: `gWireframe.Get<bool>()` sits inside `if constexpr (kbVulkanWireframe)`
  and `kbVulkanWireframe = false` (`Projects/BrokenEngineSandbox/Source/Pch.h:17`) — the discarded branch of
  a non-template `if constexpr` is still name-checked, so the declaration (and therefore the include) is
  load-bearing regardless of the toggle.
- Out-of-scope `kfMinDepth`/`kfMaxDepth` citation confirmed (`Texture.h:6-7`, used `PipelineCreator.cpp:317-318`).
- No overlap with `Graphics/Architecture_IncludeHygiene.md` (top-level files only: CameraBase / GraphicsUtils /
  Graphics / Screenshot / Islands); its Graphics.cpp item is the cited precedent, not a duplicate.
