# Refactor: BufferManager Growth & Bounds Bugs

## Context

Source: /external-refactor-clean on `Engine/Source/Graphics/Managers` (non-recursive). Two verified bugs in the
skinning bump-allocator grow path plus one release-silent bound. The grow bugs compound: when both fire, the
memcpy overruns the **source** and the caller then overruns the **destination**. Reachable in normal play — a
camera jump / save-load that brings a large fleet visible in one frame makes `SpaceshipsRender.cpp:120`
allocate the entire visible set (`iVisibleCount * uiMaterialCount`) in a single call.

## Design

### Engine/Source/Graphics/Managers/BufferManager.cpp — grow memcpy uses post-bump offset (REAL BUG)
- `AllocateMeshData` (`BufferManager.cpp:454-463`) saves `iOffset`, **bumps**
  `miMeshDataOffset[iCommandBuffer] += iCount` (`:457`), then calls `GrowMeshDataBuffer` (`:460`), which
  memcpys `miMeshDataOffset[iCommandBuffer] * sizeof(common::MeshData)` from the old mapping (`:498`) — the
  post-bump count. The grow condition is `newOffset > oldCapacity` and the old buffer is exactly
  `oldCapacity` elements, so **every** grow reads `newOffset − oldCapacity` elements past the old allocation's
  end (tens of KB on a burst; OOB read of host-visible VMA memory, AV-capable on a page boundary; the tail is
  semantically garbage anyway — the caller hasn't written `[iOffset, newOffset)` yet). Identical logic in
  `GrowJointMatrixBuffer` (`:519`). Fix: pass the pre-bump valid count into the grow helpers and memcpy only
  that. [~15m]

### Engine/Source/Graphics/Managers/BufferManager.cpp — single doubling can under-size (REAL BUG)
- `miMeshDataCapacity[iCommandBuffer] *= 2` runs exactly once (`:486`; joints `:507`) and `AllocateMeshData`
  (`:458`) never re-checks. One call with `iOffset + iCount > 2 × oldCapacity` leaves the new buffer too
  small and the caller silently writes past its end. Reachable: initial MeshData capacity is
  `common::MeshData::kiMaxMeshes = 512` (`Common/DataFile.h:180`), and one Spaceships bulk call can exceed
  1,024 elements (~260+ visible ships × 4 materials); joints analogous (initial 8,192,
  `Common/DataFile.h:197`, vs `iVisibleCount × iSkinnedMaterialCount × uiSkinJointCount`,
  `SpaceshipsRender.cpp:124-127`). Fix: `while (capacity < offset) capacity *= 2;` (or compute the required
  capacity directly) in both grow helpers. [~5m on top of the first item; fix jointly]

### Engine/Source/Graphics/Managers/BufferManager.h — magic `4` framebuffer-count arrays
- The per-command-buffer arrays are hard-sized `[4]` (`BufferManager.h:135-140`), guarded only by ASSERTs at
  `BufferManager.cpp:191` (+ the reset loop at `:257,269`); a 5-image swapchain would silently overflow
  `miMeshDataOffset[]` etc. Add `static constexpr int64_t kiMaxFramebufferCount = 4;` and use it for the
  arrays, the ASSERTs, and the reset loop. [~5m]

## Critical files
- `Engine/Source/Graphics/Managers/BufferManager.cpp` (`AllocateMeshData`, `AllocateJointMatrices`,
  `GrowMeshDataBuffer`, `GrowJointMatrixBuffer`)
- `Engine/Source/Graphics/Managers/BufferManager.h`

## Out of scope
- The `ResizeDynamicBuffer` single-slot stash — investigated and verdict "safe, undocumented";
  `Architecture_ThreadAndLifetimeGuards.md` carries the documentation/hardening item.
- Changing the bump-allocator design or initial capacities — only the grow-correctness is fixed.
- Caller-side changes — verified both game call sites already re-fetch `mpMappedMemory` after Allocate
  (`SpaceshipsRender.cpp:121,129`, `PlayersRender.cpp:139,148`); no stale-pointer issue exists.

## Acceptance criteria
- Grow memcpy length is the pre-bump valid count; capacity loop covers any single `iCount`; the framebuffer
  arrays and their guards share one named constant. Client builds clean; skinned rendering correct across a
  forced grow (large fleet snap into view).

## Notes
- No determinism/CRC exposure — client render buffers only. The bug class is silent memory corruption /
  AV, found by close inspection; per Diagnosis Discipline the fix is unambiguous from the code (offset bumped
  at `:457` before the grow call reads it at `:498`).
