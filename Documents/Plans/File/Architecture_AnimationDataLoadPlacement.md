# Architecture: Animation Data Load Placement

## Context

Source: /external-architecture-review on `Engine/Source/File`. `FileManager::LoadPackFiles` parses GLTF scene-chunk byte layout and writes directly into the Graphics global `gAnimationDataMap` (`FileManager.cpp:353-375`): the File layer hand-computes DataPacker's scene-chunk layout offsets (`iSceneArraysSize`/`iMaterialDataSize`, lines 364-366 — duplicating export-side layout knowledge) and populates a Graphics-owned map (declared `Graphics/AnimationData.h:41`). This is the directory's one real cross-subsystem leak: chunk loading should be format-agnostic; scene parsing belongs to a Graphics consumer of the eager map.

## Design

### Engine/Source/Graphics/AnimationData.h / .cpp
- Add a Graphics-side population function (e.g. free function `LoadAnimationDataFromEagerChunks()` beside `gAnimationDataMap`) that iterates `gpFileManager->GetEagerChunkMap()`, filters `kScene` chunks with `sceneHeader.bHasAnimation`, computes the animation-data offset (relocate the `iSceneArraysSize`/`iMaterialDataSize` math here), and calls `AnimationData::Load`. [~30m]

### Engine/Source/File/FileManager.cpp
- Delete the scene-parsing block from the eager-load async lambda (lines 353-375: the GLTF debug LOGs, offset computation, and `gAnimationDataMap` write; the generic eager-map population stays). [~10m]

### Call site
- Invoke the new Graphics function once on the client boot path after eager data is available. The first `GetEagerChunkMap()` caller blocks on the future, so any Graphics/Models boot point before first animated-model use works — pick during grill (candidate: where `gAnimationDataMap` consumers are first set up, e.g. model pipeline setup in the `Graphics` boot path). [~15m]

## Critical files

- `Engine/Source/File/FileManager.cpp`
- `Engine/Source/Graphics/AnimationData.h` / `.cpp`
- Client boot call site (chosen at grill)

## Out of scope

- Decomposing the rest of `LoadPackFiles` — `File/Refactor_FunctionDecomposition.md` (sequence this plan first; it shrinks the function by ~25 lines).
- Changing the scene-chunk on-disk layout or DataPacker export — the layout knowledge moves, it does not change.
- The redundant nested `#if defined(BT_CLIENT)` at `FileManager.cpp:370-374` — `File/Refactor_ClientGuardAndWideApi.md` owns it (it evaporates with the moved block if this plan lands first).

## Acceptance criteria

- `FileManager.cpp` contains no `gAnimationDataMap`/`AnimationData`/`sceneHeader`-layout references; animations still load (client boots, an animated model renders).

## Notes

- Client-only code path; no determinism/CRC/network/`kiVersion` exposure (the `.pack` layout is untouched).
- Grill decision: call-site placement. Today the parse runs on the eager-load async thread; moving it to a post-drain boot call serializes it behind the future. The parse is cheap (offset math + header reads), but if boot measurement disagrees, the Graphics function can be invoked from an async continuation instead — decide at grill.

## Verification Notes

Verified against source (2026-06-10):

- `gAnimationDataMap` confirmed at `Graphics/AnimationData.h:41` (inline global beside the class); `AnimationData::Load(const std::byte*, common::crc_t)` confirmed at `AnimationData.h:10`.
- The `FileManager.cpp:353-375` block matches the description exactly: GLTF debug LOG (353-357), `bHasAnimation` gate (360), `iSceneArraysSize`/`iMaterialDataSize` offset math duplicating export layout (364-366), `gAnimationDataMap.try_emplace` + `Load` inside the redundant nested `BT_CLIENT` (370-374).
- Post-drain feasibility confirmed: `EagerChunk` (`pHeader` + `pData`) provides everything the relocated math needs (`flags & kScene`, `sceneHeader.bHasAnimation`/counts, chunk-data base pointer), and the backing `mPackFileData` buffers live for FileManager's lifetime, so zero-copy `Load` pointers stay valid. The first `GetEagerChunkMap()` caller blocks on `mLoadingFuture` (FileManager.cpp:387-390), so any post-drain client boot point works.
- Client-only wiring caveat (per `Graphics/CLAUDE.md`): `AnimationData.*` is unwrapped and client-only purely via vcxproj membership + `Engine.h`'s `BT_CLIENT` span — the new function stays in those files (no server vcxproj entry), and the chosen boot call site must already be client-only or carry its own `BT_CLIENT` guard.
- Cross-references verified both ways: `Refactor_ClientGuardAndWideApi.md` owns the nested-guard removal and notes it evaporates if this lands first; `Refactor_FunctionDecomposition.md` sequences after this plan. `Architecture_IncludeHygiene.md`'s `Graphics/AnimationData.h` direct-include item also evaporates if this plan lands first (noted there).
