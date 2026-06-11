# Architecture: Frame Include Hygiene

## Context

Source: /external-architecture-review on `Engine/Source/Frame` (non-recursive). The directory's internal include graph is acyclic and std-header hygiene is perfect (zero direct `<...>` includes), but several headers compile only because the game's `Pch.h` includes `Frame/Frame.h` before `Engine.h` — making `Engine.h` non-self-contained — and four includes are dead weight. Fixing the missing direct includes removes the load-bearing-PCH-ordering hazard; removing the unused ones trims the worst weight-to-need include trees in the directory.

## Design

### Engine/Source/Frame/IslandTerrain.cpp
- Remove unused `#include "Graphics/Managers/PipelineManager.h"` (line 7) — `gpPipelineManager`/`kPipeline*`/`PipelineManager::` appear only in comments (lines 568, 752); `TextureDescriptors` comes from `TextureManager.h` [~5m]
- Remove unused `#include "Data/Data.h"` (line 13, generated asset-CRC header) — zero `data::` references; `LazyChunk`/`ChunkState`/`LoadPriority` come from `File/FileManager.h` via `Engine.h`, `common::ChunkFlags`/`common::IslandHeader` from `Common/DataFile.h` [~5m]

### Engine/Source/Frame/TimeStep.cpp
- Replace `#include "Game.h"` (line 5) with the game's `Frame/Frame.h` — the only game symbol used is `game::kTickNs` (lines 39, 48, 56–57, 64, 74), declared at game `Frame.h:36`; `Game.h` drags the sessions/fleet/save-load tree for one `constexpr` [~5m]

### Engine/Source/Frame/Alignments.cpp, AreaDamage.cpp, Collision.cpp
- Remove redundant `#include "Pch.h"` (line 1 of each) — the PCH is force-included via vcxproj `<ForcedIncludeFiles>`; 7 of the 10 Frame .cpp files already omit it [~5m]

### Engine/Source/Frame/Collision.h
- Remove the dead `game::Frame` forward declaration (lines 3–8) — the name is never referenced in the header or `Collision.cpp` [~5m]
- Add `#include "Frame/Alignments.h"` — the header uses `engine::alignment_t` (line 47) and `engine::Alignments` (lines 123, 138) but receives them only via PCH ordering. This is the single missing edge that makes `Engine.h` non-self-contained (`Engine.h:8` pulls `Collision.h` with `Alignments.h` provided nowhere before it) [~5m]

### Engine/Source/Frame/FrameStaticData.h, IslandChainPlacement.h, IslandTerrain.h, FrameUtils.h
- Add `#include "Frame/GridCoord.h"` to each — `GridCoord` is used at `FrameStaticData.h:12` (member), `IslandChainPlacement.h:24` (`Generate` signature), `IslandTerrain.h:133` (`BuildElevationGrid` signature), and `FrameUtils.h:73` (`ForEachBeginRender`'s `std::unordered_map<GridCoord, ...>` parameter — this one also needs the `std::hash<GridCoord>` specialization GridCoord.h provides), all supplied only by PCH ordering today [~10m]

### Engine/Source/Frame/Collision.cpp, FrameBase.cpp
- Add a direct include of the game's `Frame/Frame.h`: `Collision.cpp` uses `game::kfDeltaTime` (lines 75, 137, 172) and `FrameBase.cpp` uses the complete `game::Frame`/`FrameInterpolate`/`FramePostRender` types (lines 165–222), both via PCH only. Precedent: `IslandChainPlacement.cpp:4` already includes it directly [~5m]

## Critical files
- `Engine/Source/Frame/IslandTerrain.cpp`, `TimeStep.cpp`, `Alignments.cpp`, `AreaDamage.cpp`, `Collision.cpp`, `FrameBase.cpp`
- `Engine/Source/Frame/Collision.h`, `FrameStaticData.h`, `IslandChainPlacement.h`, `IslandTerrain.h`, `FrameUtils.h`

## Out of scope
- `Memory/MemoryManager.h` include staleness in `Alignments.cpp`/`AreaDamage.cpp`/`Collision.cpp` — owned by `Engine/DeadCodeAndUnusedIncludesSweep.md` (~12-TU sweep); verify overlap at execution and skip here if covered there
- `NavQuery.cpp`'s `Ui/WrapperBase.h` include — flagged unused by the review **in error**; it provides `gBaseHeight` (`WrapperBase.h:214`, used at `NavQuery.cpp:503/583/603`). Only removable if `Frame/Architecture_BaseHeightWrapperDeterminism.md` lands first
- `Engine.h`'s broader self-containment (the `std::formatter` specializations relying on game-aggregation arrival) — engine-wide concern, not Frame
- Any behavior change; this is includes-only and compile-checked

## Notes
- No determinism/CRC/serialization exposure. Both client and server builds must compile after each removal (the PCH currently masks everything).

## Verification Notes (2026-06-10)
- Every unused-include claim re-verified by symbol grep (the report class that produced the `NavQuery.cpp`/`gBaseHeight` false positive):
  - `IslandTerrain.cpp` `PipelineManager.h` (line 7): `PipelineManager`/`gpPipelineManager`/`kPipeline*` appear only in comments at lines 568-569 and 752. `TextureDescriptors` (used at 578/585/587/718/745-749/757/760/775/827) is provided by `TextureManager.h`, which includes `TextureDescriptors.h` at its line 7 — claim holds transitively.
  - `IslandTerrain.cpp` `Data/Data.h` (line 13): zero `data::` references in the TU.
  - `TimeStep.cpp` `Game.h` (line 5): only game symbol is `game::kTickNs` (lines 39, 48, 56-57, 64, 74), declared at game `Frame.h:36` — all cites exact.
  - `Pch.h` at line 1 of `Alignments.cpp`/`AreaDamage.cpp`/`Collision.cpp` confirmed; `<ForcedIncludeFiles>Pch.h</ForcedIncludeFiles>` confirmed in all configs of both vcxprojs; the other 7 of 10 Frame TUs omit it.
  - `Collision.h`: `game::Frame` forward decl (lines 3-8) referenced nowhere in `Collision.h`/`Collision.cpp`; `alignment_t` at :47 and `Alignments` at :123/:138 confirmed; `Engine.h:8` pulls `Collision.h` with only `ProfileManagerBase.h` and `AreaDamage.h` (which has no includes) before it — Alignments.h genuinely provided nowhere first. Today it arrives only because game `Pch.h:96` includes `Frame/Frame.h` (→ `FrameBase.h:3` → `Alignments.h`) before `Engine.h` at `Pch.h:97`.
  - `FrameBase.cpp` complete-type uses: lines 165–222 (cite corrected from 175–222; `AllocateAndCopy` at 165 already needs the complete derived types).
- **Addition**: `FrameUtils.h` uses `GridCoord` at line 73 with zero includes of its own — the same missing-direct-include class the plan targets; added to the GridCoord item and Critical files.
- Overlap with `Engine/DeadCodeAndUnusedIncludesSweep.md` re-checked: that sweep's item 1 owns the `Memory/MemoryManager.h` staleness in these same three TUs; correctly excluded here.
