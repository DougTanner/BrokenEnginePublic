# Architecture: ExportJobs Include Hygiene

## Context
Source: /external-architecture-review on `DataPacker/Source/ExportJobs` (recursive). Dead includes and unused using-declarations create false dependency edges; one header-level include forces `SceneSkeletonLoader.h` onto every includer of `ExportScene.h`.

## Design

### DataPacker/Source/ExportJobs/ExportAudio.cpp
- Remove unused `#include "FileManager.h"` (line 15) — no `FileManager`/`gpFileManager` reference anywhere in the TU [~2m]

### DataPacker/Source/ExportJobs/ExportRaw.cpp
- Remove unused `#include "FileManager.h"` (line 3) — no reference in the TU [~2m]

### DataPacker/Source/ExportJobs/ExportScene.h / ExportScene.cpp
- Move `#include "Scene/SceneSkeletonLoader.h"` from `ExportScene.h:6` into `ExportScene.cpp` — no symbol from it (`SkeletonData`, `LoadSkeletonData`, `BuildNodeParentMap`) appears in the header's declarations; the .cpp uses them at lines 304, 309, 348, 703, 710 via the transitive include [~5m]

### DataPacker/Source/ExportJobs/Texture/Texture.cpp
- Remove `#include <codeanalysis/warnings.h>` (line 3) — the `ALL_CODE_ANALYSIS_WARNINGS` macro it provides already arrives via the forced PCH (`Common/ExternalHeaders.h:9`); every other TU in the area uses the macro with no direct include [~2m]

### Unused `using enum common::ChunkFlags;` declarations
- Remove from `ExportAudio.cpp:17`, `ExportFont.cpp:3`, `ExportModel.cpp:3`, `ExportRaw.cpp:5`, `ExportScene.cpp:30`, `ExportIsland.cpp:8` — all six TUs reference enumerators fully qualified (e.g. `common::ChunkFlags::kFont` at `ExportFont.cpp:42`). Keep the declarations in `ExportTexture.cpp` and `ExportShader.cpp`, where unqualified enumerators are actually used [~5m]

### DataPacker vcxproj
- `PersistentWorker.cpp` is compiled into DataPacker (`DataPacker.vcxproj:23`) with zero references anywhere in DataPacker source (parallelism is `std::async`-based by documented design). Remove it from the project — both `DataPacker.vcxproj:23` and the matching filter entry at `DataPacker.vcxproj.filters:129` — **after verifying at execution time that no linked Common code references its symbols** (build + link check) [~10m]

## Critical files
- `DataPacker/Source/ExportJobs/ExportAudio.cpp`, `ExportRaw.cpp`, `ExportScene.h`, `ExportScene.cpp`, `ExportFont.cpp`, `ExportModel.cpp`, `ExportIsland.cpp`, `Texture/Texture.cpp`
- `DataPacker/Platforms/.../DataPacker.vcxproj` (PersistentWorker entry)

## Out of scope
- Include-path style normalization (`"ExportJobs/ExportIsland.h"` rooted form vs bare-relative) — cosmetic only.
- Direct includes for pieces of the `bc7enc_rdo` umbrella header (`rdo_bc_encoder.h`) — the umbrella is the library's intended interface.
- Engine-side include hygiene — owned by the Engine dead-code/unused-includes sweep plan.

## Notes
- No determinism/CRC/pack-layout/`kiVersion` exposure. Pure removals; compile + link is the verification.
- Quick Win — every item is mechanical; the only conditional item is the `PersistentWorker.cpp` vcxproj removal (gated on a link check).

## Verification Notes
- Every claim independently re-verified against source: `FileManager.h` includes at `ExportAudio.cpp:15` / `ExportRaw.cpp:3` confirmed unreferenced in their TUs; all six `using enum common::ChunkFlags;` TUs (`ExportAudio.cpp:17`, `ExportFont.cpp:3`, `ExportModel.cpp:3`, `ExportRaw.cpp:5`, `ExportScene.cpp:30`, `ExportIsland.cpp:8`) checked against the full `ChunkFlags` enumerator list (`Common/DataFile.h:46-68`) — zero unqualified uses; the two keepers confirmed (unqualified `kCubemap`/`kZlibCompressed`/`kTexture` in `ExportTexture.cpp:80/95/115/...`, unqualified `kCompute`/`kFragment` in `ExportShader.cpp:152`).
- `ExportScene.h` declares nothing from `SceneSkeletonLoader.h` (`SkeletonData`/`LoadSkeletonData`/`BuildNodeParentMap` are its only exports; the header's `Material`/`MaterialNodeInfo` come from `SceneVerticesLoader.h`); the .cpp use sites at :304/:309/:348/:703/:710 verified exact. `Main.cpp` (the only other includer of `ExportScene.h`) uses no skeleton-loader symbol.
- `Texture.cpp:3` `<codeanalysis/warnings.h>` confirmed redundant: `Pch.h:4` includes `ExternalHeaders.h`, which includes it at line 9.
- `PersistentWorker.cpp` appears in `DataPacker.vcxproj:23` (+ `.filters:129`, now added to the Design item) with zero `PersistentWorker` references anywhere under `DataPacker/`.
- No overlap with `Documents/Plans/Engine/DeadCodeAndUnusedIncludesSweep.md` (Engine/Game/Shaders only) or `DataPacker/Refactor_MainQuickWinMechanics.md` (owns `Main.cpp:29`'s unused `using enum` — a different TU).
