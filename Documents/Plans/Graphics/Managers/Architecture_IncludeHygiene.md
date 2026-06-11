# Architecture: Include Hygiene (Graphics/Managers)

## Context

Source: /external-architecture-review on `Engine/Source/Graphics/Managers` (non-recursive). The directory is
fully ExternalHeaders.h-compliant (zero `#include <...>` lines), but 18 local includes are unused (verified by
whole-file symbol matching against each header's complete declared-symbol set, all preprocessor branches), and
one file uses symbols it only receives transitively. All removals are compile-checked and PCH-safe (every cpp
inherits `Engine.h` + `Common.h` via `Pch.h`).

## Design

### Unused `Profile/ProfileManager.h` (game header) — six cpps
These files use only `ScopedBootTimer`/`kBootTimer*`/`ScopedCpuProfile`, which live in the engine's
`ProfileManagerBase.h` (in `Engine.h` → PCH), not in the game's `ProfileManager.h` (whose symbols are
`game::ProfileManager`, `gpProfileManager`, the `kCpuCounter*`/`kCpuTimer*` game enums). Remove the include:
- `Engine/Source/Graphics/Managers/DeviceManager.cpp:3` [~5m total for all six]
- `Engine/Source/Graphics/Managers/BufferManager.cpp:3`
- `Engine/Source/Graphics/Managers/InstanceManager.cpp:3`
- `Engine/Source/Graphics/Managers/ParticleManager.cpp:5`
- `Engine/Source/Graphics/Managers/PipelineManager.cpp:5`
- `Engine/Source/Graphics/Managers/TextManager.cpp:3`

### Other unused includes in cpps
- `PipelineManager.cpp:6` — `Ui/LightingWrappersBase.h`: zero of its 85 wrapper globals referenced. [~5m]
- `PipelineManager.cpp:9` — `Data/Texture.h`: zero `data::` texture tokens in the file (its `data::kShaders*`
  come from `Data/Shader.h` at line 8). [~5m]
- `TextManager.cpp:6` — `Data/Raw.h`: only `data::kFonts*` used (`TextManager.cpp:29,33`), which is
  `Data/Font.h` (line 5). [~5m]
- `TextureManager.cpp:9` — `Data/Data.h`: zero `data::` tokens in the cpp (the `kIrradianceCrc` etc. at
  `TextureManager.cpp:341,343` are `TextureManager`'s own static members). [~5m]
- `CommandBufferRecordMain.cpp:9` — `Ui/ShadowWrappersBase.h`: zero of its 22 globals referenced (the file's
  wrappers belong to GraphicsSettings/Lighting wrapper headers). [~5m]
- `RenderTargetTextures.cpp:8` — `Ui/WaterWrappersBase.h`: zero of its 96 globals (`gWaterShapeDetail` is
  GraphicsSettingsWrappersBase). [~5m]
- `TextureCache.cpp:5` — `TextureManager.h`: zero `TextureManager` symbols used; the file's `data::kShaders*`
  (`TextureCache.cpp:186`) come via its own `Data/Data.h` (line 7). [~5m]
- `RenderTargetTextures.cpp:10` and `TextureManager.cpp:10` — `Game.h`: each file's only game symbol is a
  `game::Camera` static constant (`kfShadowHeadroomMultiplier` at `RenderTargetTextures.cpp:82,84`,
  `kfLightingHeadroomMultiplier` at `TextureManager.cpp:43,45`), and `game::Camera` is not reachable through
  `Game.h` — it arrives via `Engine/Source/GameBase.h:6` through the PCH. Removing the include is compile-safe
  today; `Architecture_LayerSeams.md` item 2 (same files) moves the constants engine-side, which is the real
  fix. Co-schedule. [~5m]

### Unused `Data/Texture.h` in directory-private headers
The generated `Data/Texture.h` is used only by `TextureManager.h` itself; the three sub-object headers include
it without using any `data::` symbol (their `Texture` type is `engine::Texture` from `Graphics/Objects/Texture.h`
via PCH/`Engine.h` ordering):
- `RenderTargetTextures.h:3` [~5m total for all three]
- `TextureCache.h:3`
- `TextureDescriptors.h:3`
Keep `TextureManager.h:3`'s own copy — after this change it is the only (and a genuinely used) include of that
header in the directory.

### Transitive include made direct
- `DynamicPipelines.cpp` uses `data::kTextures*` at lines 298, 316, 325, 334, 343, 372 but includes only
  `Data/Model.h`/`Data/Shader.h` (lines 5-6); the symbols arrive via PCH → `Engine.h` → `TextureManager.h` →
  `Data/Texture.h`. Add a direct `#include "Data/Texture.h"`. [~5m]

## Critical files
- `Engine/Source/Graphics/Managers/`: DeviceManager.cpp, BufferManager.cpp, InstanceManager.cpp,
  ParticleManager.cpp, PipelineManager.cpp, TextManager.cpp, TextureManager.cpp, CommandBufferRecordMain.cpp,
  RenderTargetTextures.cpp, TextureCache.cpp, RenderTargetTextures.h, TextureCache.h, TextureDescriptors.h,
  DynamicPipelines.cpp

## Out of scope
- `TextureUploadManager.cpp:3` (`Memory/MemoryManager.h` unused) — already item 1 of
  `Engine/DeadCodeAndUnusedIncludesSweep.md` (the ~12-TU MemoryManager include sweep); not duplicated here.
- Moving `InstanceManager.h:3`'s `renderdoc_app.h` include into `Common/ExternalHeaders.h` — judgment call
  (lone quoted ThirdParty include, single consumer); left as-is.
- Header self-sufficiency (managers' zero-include headers relying on `Engine.h` ordering) — sanctioned project
  convention, not addressed.
- Moving the `game::Camera` headroom constants engine-side — `Architecture_LayerSeams.md` item 2.

## Acceptance criteria
- Each removed include has no remaining symbol use in its file (grep-clean); client builds clean.

## Notes
- No determinism/CRC/network/`kiVersion` exposure — include-only edits, no behavior change.
- Unused-include verdicts are textual whole-file checks; the client build is the backstop. No macro-synthesized
  symbol names were observed in the directory.
- Shares `TextureManager.cpp`/`RenderTargetTextures.cpp` with `Architecture_LayerSeams.md` — co-schedule
  (File Groups).
