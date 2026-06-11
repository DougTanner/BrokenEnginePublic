# Architecture: File Include Hygiene

## Context

Source: /external-architecture-review on `Engine/Source/File` (non-recursive). The directory is `ExternalHeaders.h`-compliant (zero direct `<...>` includes), but carries one unused include and three implicit dependencies held together only by `Engine.h` ordering comments — the same PCH-ordering debt class the Audio `Architecture_IncludeHygiene` plan addresses for `Engine/Source/Audio`.

## Design

### Engine/Source/File/FileManager.cpp
- Remove the unused `#include "Data/Data.h"` (line 7). Every `data::` symbol the TU references (`data::DataTypes`, `data::kDataTypeCount`, `data::kpcDataTypeNames`, the `kDataType*` enumerators) is declared in `Data/DataTypes.h`, which already arrives via `FileManager.h:3`; none of the 8 generated asset-CRC headers `Data.h` aggregates contributes a referenced symbol. [~5m]
- Add direct includes for the two Graphics dependencies currently riding `Engine.h` ordering (`Engine.h:52,55`): `Graphics/Managers/TextureUploadManager.h` (`gpTextureUploadManager->RequestUpload`, line 582) and `Graphics/AnimationData.h` (`gAnimationDataMap`/`AnimationData`, lines 371-373). Both usages sit inside `BT_CLIENT` blocks — wrap the includes in `#if defined(BT_CLIENT)` to match how `Engine.h` gates its client-only span. [~5m]

### Engine/Source/File/DifferenceStream.h
- Add `#include "File/FileManager.h"`. The header uses `FileFlags_t` (`DifferenceStreamWriter::Save` at line 66, the `DifferenceStreamReader` ctor at line 152) and `gpFileManager` (lines 71, 90, 112, 124, 155, 200, 232, 247) with zero includes of its own; the dependency is enforced solely by the hand-maintained ordering comment at `Engine.h:11` ("FileManager before DifferenceStream: DifferenceStream uses FileFlags_t"). After the direct include, simplify that `Engine.h` comment — the ordering is no longer load-bearing for this pair. [~5m]

## Critical files

- `Engine/Source/File/FileManager.cpp`
- `Engine/Source/File/DifferenceStream.h`
- `Engine/Source/Engine.h` (ordering comment at line 11 only)

## Out of scope

- The ~22 consumer TUs using `gpFileManager` via PCH only — `StaticVoice.cpp`/`StreamingVoice.cpp` are owned by `Audio/Architecture_IncludeHygiene.md`; a repo-wide consumer-side sweep is not File-directory work.
- Narrowing `FileManager.cpp`'s `#include "Game.h"` (used only for `game::kGameName`, lines 23, 31) — the include is used, the force-included PCH makes a narrower header buy nothing measurable, and relocating `kGameName` would touch game-layer code for no functional win (YAGNI).
- `FileManager.h`'s PCH-provided `common::`/Vulkan/std symbol reliance — repo convention (headers are PCH-reliant by design).

## Acceptance criteria

- Client and server build clean with no behavior change (include-only edit; compile-checked).

## Notes

- No determinism/CRC/network/`kiVersion` exposure.
- Re-confirm the `Data/Data.h` removal by grep at execution (no `data::` constant from the generated asset-CRC headers referenced in the TU).

## Verification Notes

All design items verified against source (2026-06-10):

- `Data/Data.h` removal confirmed: every `data::` symbol in `FileManager.cpp` (`DataTypes`, `kDataTypeCount`, `kpcDataTypeNames`, the eight `kDataType*` enumerators) is declared in the generated `DataTypes.h` (`Output/Data/DataTypes.h`), which arrives via `FileManager.h:3`. The eight asset-CRC headers `Data.h` aggregates contain only `k<Type><Asset>Crc` constants; grep finds zero such symbols in the TU (the `kFont`/`kChunkAudio` hits at lines 195/201 are `common::ChunkFlags`, not `data::`).
- Graphics usages confirmed: `gpTextureUploadManager->RequestUpload` at `FileManager.cpp:582` (inside the `BT_CLIENT` block at 577-585); `gAnimationDataMap`/`AnimationData` at 371-373 (inside `BT_CLIENT`). `Engine.h:52`/`Engine.h:55` include those headers inside the client span as cited.
- No include cycle from the `DifferenceStream.h` item: `FileManager.h` includes only `Data/DataTypes.h` — it does not reach `DifferenceStream.h` directly or transitively. All cited `FileFlags_t`/`gpFileManager` usage lines in `DifferenceStream.h` (66, 152; 71, 90, 112, 124, 155, 200, 232, 247) verified. The `Engine.h:11` ordering comment exists verbatim ("FileManager before DifferenceStream: DifferenceStream uses FileFlags_t"). Direct-include-replacing-ordering-comment precedent: `Documents/Plans/Audio/Architecture_IncludeHygiene.md` (verified, landed-pattern).
- `Game.h` retention rationale confirmed (`game::kGameName` at lines 23, 31). Directory is ExternalHeaders.h-compliant (zero `<...>` includes in all three File sources).
- Sequencing caveat: `File/Architecture_AnimationDataLoadPlacement.md` deletes the lines 353-375 block that is this plan's only `Graphics/AnimationData.h` user. If that plan lands first, drop the `AnimationData.h` include item (the `TextureUploadManager.h` include stays valid either way); if this plan lands first, that plan must also remove the then-unused include.
- No queue duplication: `Engine/DeadCodeAndUnusedIncludesSweep.md` covers only `Memory/MemoryManager.h` consumer includes (FileManager.cpp has none); consumer-TU includes stay with the Audio plan as stated.
