# Right-size AnimationData's oversized fixed arrays

## Context

`engine::AnimationData` (`Engine/Source/Graphics/AnimationData.{h,cpp}`) carries four by-value member arrays dimensioned to compile-time maxima but filled only up to the runtime header counts in `mHeader` (`common::AnimationHeader`). One `AnimationData` instance lives per animated-scene CRC in the global `gAnimationDataMap` (`std::unordered_map<common::crc_t, AnimationData>`, declared in `AnimationData.h`), so the unused inline tail is paid per scene CRC. The four members and their current dimensions (`AnimationData.h:30-33`):

- `XMMATRIX mBindPoseLocalMatrices[common::Skeleton::kiMaxNodes]` — `kiMaxNodes = 256` (`Common/DataFile.h:164`); 256 × 64 B = 16 KB. Filled to `mHeader.skeleton.uiNodeCount` (`AnimationData.cpp:48-56`); read at `:318` (`EvaluateWorldMatrices`).
- `bool mbAnimatedNodes[common::AnimationHeader::kiMaxAnimations][common::Skeleton::kiMaxNodes]` — `kiMaxAnimations = 64` (`Common/DataFile.h:211`) × `kiMaxNodes = 256` = 16 KB (2D array). Filled to `uiAnimationCount × uiNodeCount` (`AnimationData.cpp:58-66`); read as a row pointer `const bool* pbAnimated = mbAnimatedNodes[iAnimationIndex]` at `:266`, then indexed `[i]` at `:269` and `:306`.
- `XMMATRIX mAlignedInverseBindMatrices[common::Skeleton::kiMaxSkinJoints]` — `kiMaxSkinJoints = 256` (`Common/DataFile.h:165`); 16 KB. Filled to `mHeader.skeleton.uiSkinJointCount` (`AnimationData.cpp:68-72`); read at `:365` (`EvaluateMaterial`).
- `XMMATRIX mAlignedRelativeTransforms[common::SceneHeader::kiMaxMaterials]` — `kiMaxMaterials = 128` (`Common/DataFile.h:86`); 128 × 64 B = 8 KB. Filled to `mHeader.uiMaterialCount` (`AnimationData.cpp:74-78`); read at `:339` (`EvaluateMaterial`).

Total inline footprint ≈ 56 KB per instance, mostly empty for typical glTF skeletons (a handful of nodes/joints/animations/materials). This is the AnimationData analog of the just-landed `Graphics/Refactor_ModelPipelineMembers` `ModelPipeline::mpPipelines` right-size — same intent (trim worst-case inline arrays down to runtime fill), different file.

All four members are accessed **only** inside `AnimationData.cpp` (verified by a repo-wide symbol search — no external readers); `mHeader`/`mpNodes`/etc. are the only members that callers reach into. That keeps the change confined to the one TU plus the header declarations.

## Design

Replace the four fixed arrays with runtime-sized containers populated in `Load` from the already-known header counts. Containers are allocated once at load time and never resized afterward, so the indexing in `EvaluateWorldMatrices`/`EvaluateMaterial` stays identical apart from container syntax.

Alignment requirement: `XMMATRIX` is 16-byte aligned. The three `XMMATRIX` arrays must keep that alignment, and `Load` uses `XMLoadFloat4x4`/`operator*` writes that assume aligned storage. Two viable container choices — pick the simplest that preserves alignment:

- **`std::vector<XMMATRIX>`** — in C++17+ the default allocator is over-aligned-aware (`operator new(size, align_val_t)`), so `std::vector<XMMATRIX>` storage is 16-byte aligned. Simplest; integrates with range/`.data()` access; one heap block per array. Recommended default.
- **`common::AlignedUniquePtr<XMMATRIX>` via `common::MakeAligned<XMMATRIX>(count)`** (`Common/AlignedMemory.h`) — 64-byte aligned, RAII, no size/capacity overhead. Use only if the project prefers the engine's explicit aligned helper over trusting the standard allocator's over-alignment.

`mbAnimatedNodes` is a 2D `bool` array indexed `[iAnimation][node]`. Flatten to a 1D `std::vector<uint8_t>` of size `uiAnimationCount * uiNodeCount` with stride `uiNodeCount`:

- Fill (`AnimationData.cpp:64`): `mbAnimatedNodes[iAnim * uiNodeCount + mpChannels[iCh].uiNodeIndex] = 1;`
- Row pointer (`:266`): `const uint8_t* pbAnimated = mbAnimatedNodes.data() + iAnimationIndex * uiNodeCount;` then `[i]` indexing at `:269`/`:306` is unchanged (truthiness of `uint8_t` works as the `bool` did).
- Store `uiNodeCount` as the stride at fill time — it is `mHeader.skeleton.uiNodeCount`, already available. (`std::vector<bool>` is rejected: bit-packed proxy, no `bool*`/`uint8_t*` row pointer; `vector<vector<uint8_t>>` is rejected: extra indirection and N small allocations for no benefit.)

`Load` already reads every count from `mHeader` before the fill loops, so size the four containers immediately after the header `memcpy` (`AnimationData.cpp:11`) and before their respective fill loops. The `EvaluateWorldMatrices` clamp `iAnimationIndex < common::AnimationHeader::kiMaxAnimations` and the `EvaluateMaterial` joint cap `i < common::kiMaxJointsPerMesh` are guards against the old fixed dimensions; once the arrays are runtime-sized, the live bound is the header count (`uiAnimationCount`, `uiSkinJointCount`) — keep the existing `iAnimationIndex < mHeader.uiAnimationCount` assert/clamp and the `i < mHeader.skeleton.uiSkinJointCount` loop bound, which already dominate. Drop only the now-redundant `kiMax*` terms in those bounds if they become dead; do not otherwise touch the evaluation math.

Allocation context: the containers allocate at load time. `Load` is called from `LoadAnimationDataFromEagerChunks` (`AnimationData.cpp:381`), which the `Graphics` ctor invokes once at startup (per `Graphics/CLAUDE.md` "AnimationData": "Map populated once in the `Graphics` ctor … idempotent early-out so device-loss recreation skips it"). Startup is **before** the main loop, where allocation tracking is not armed (per `Engine/Source/CLAUDE.md` "Allocation discipline": tracking is enabled only around the main loop). So **no** `ScopedSuppressAllocationTracking` / `// Heap:` annotation is required — confirm at execution that no animated-scene `Load` path is reachable from a main-loop-armed context (it is not today; the map is build-once and never re-`Load`ed per frame).

## Critical files

- `Engine/Source/Graphics/AnimationData.h` — the four member declarations (`:30-33`); replace with the chosen containers. No public-interface change (the four members have no external readers).
- `Engine/Source/Graphics/AnimationData.cpp` — `Load` sizing + fill loops (`:48-78`), the `mbAnimatedNodes` row-pointer read in `EvaluateWorldMatrices` (`:266`), the `mBindPoseLocalMatrices` read (`:318`), the `mAlignedRelativeTransforms` read (`:339`), the `mAlignedInverseBindMatrices` read (`:365`).
- `Common/DataFile.h` — read-only reference for the `kiMax*` constants (`SceneHeader::kiMaxMaterials:86`, `Skeleton::kiMaxNodes:164` / `kiMaxSkinJoints:165`, `AnimationHeader::kiMaxAnimations:211`) and the runtime count fields on `AnimationHeader`/`Skeleton`. **Do not edit** — the `kiMax*` maxima still bound the DataPacker export and the pack-format layout; only AnimationData's inline mirror is being right-sized.

## Out of scope

- The `kiMax*` constants in `Common/DataFile.h` and any `.pack`/DataPacker export sizing — unchanged; this only trims the runtime render-side cache, not the on-disk format.
- The zero-copy pack-memory pointers (`mpNodes`/`mpAnimations`/`mpChannels`/…) — they already index pack memory directly; not arrays, not in scope.
- The workbuffer temp arrays in `EvaluateWorldMatrices`/`EvaluateAnimation` (`gpThreadLocal->mWorkbuffer.PushBuffer`, sized to `kiMaxNodes`) — those are per-frame scratch, not per-instance storage; leave as-is.
- Any other AnimationData cleanup (the matrix-convention comments, the keyframe interpolation paths, the topological-order assert, enum dedup) — not folded in here.
- The `EvaluateWorldMatrices` clamp / `EvaluateMaterial` joint-cap math beyond removing terms that become provably dead once arrays are runtime-sized.

## Acceptance criteria

- The four members are runtime-sized containers; `sizeof(AnimationData)` no longer carries the ~56 KB inline tail.
- `XMMATRIX` containers preserve 16-byte alignment (verified by the chosen container's guarantee).
- `mbAnimatedNodes` flattened to a 1D stride-`uiNodeCount` container; all three access sites (`:64` fill, `:266` row pointer, `:269`/`:306` element reads) updated and indexing-equivalent.
- Client builds; no new server-vcxproj exposure (file membership unchanged).
- No behavior change to evaluated matrices for any existing animated scene.

## Notes

- **Invariant exposure: none.** These four members are derived load-time render-side caches with no CRC / determinism / `kiVersion` / `.pack`-layout / replay / network / wire exposure. The animation clock that consumes `mpAnimations[].fDuration` is deliberately excluded from the shared CRC (see the `AnimationData.h:40-43` comment and `Players/CLAUDE.md` "Shared-CRC exclusions"); right-sizing these arrays does not touch that path. Client/graphics-only (`AnimationData.*` is unwrapped but client-only via client-vcxproj membership + `Engine.h`'s `BT_CLIENT` span, per `Graphics/CLAUDE.md` guard-scope notes).
- **Allocation-tracking:** load runs at startup (Graphics ctor, before the main loop), so no suppression guard/`// Heap:` needed — confirm at execution.
- **Sibling:** this is the AnimationData analog of the landed `Graphics/Refactor_ModelPipelineMembers` (`ModelPipeline::mpPipelines` right-size) — same intent, different file.
- **One open decision for `/external-grill-plan`:** `std::vector<XMMATRIX>` (rely on C++17 over-aligned default allocator) vs `common::AlignedUniquePtr<XMMATRIX>` + `MakeAligned` (explicit engine aligned helper) for the three matrix arrays. Recommendation: `std::vector` for simplicity (KISS) unless the project standard is the explicit aligned helper.
- **Co-schedule:** `AnimationData.{h,cpp}` is in the `Engine/Source/Graphics/` top-level File Group (Order.md `## File Groups`) alongside `Architecture_SharedConstantDuplication` (touches `AnimationData.cpp` enums) and `Architecture_InvariantHardening` (touches the `AnimationData.h:41` comment) — disjoint from this plan's member/array edits; refresh line citations if interleaved.
