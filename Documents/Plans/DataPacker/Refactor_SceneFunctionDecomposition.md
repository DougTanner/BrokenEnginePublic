# Refactor: Scene & Texture Function Decomposition

## Context
Source: /external-refactor-clean on `DataPacker/Source/ExportJobs` (recursive). The scene/texture pipeline's three largest functions exceed ~100 lines (largest: 260, with 4+ nesting levels); each has clean extraction seams, and one contains a near-verbatim intra-file duplication.

## Design

### `LoadVertices` (`Scene/SceneVerticesLoader.cpp:88-347`, 260 lines)
- Extract: node-local-matrix construction (:96-130), effective-material resolution (:167-207), per-vertex assembly (:237-296), index-append switch (:301-345) [~1h]
- DRY: the node-local-matrix block (:96-130) duplicates `ComputeNodeWorldTransform`'s TRS/matrix branch (:49-71, same file) almost verbatim — extract one `NodeLocalMatrix(rNode)` used by both [~15m]

### File-loading `Texture::Texture` ctor (`Texture/Texture.cpp:58-217`, 160 lines)
- Four independent format branches (kImage :62-92, kFloat32 :93-115, kUint16Raw :116-140, EXR :141-216) — extract one private loader per `FileType` [~45m]

### `WriteAnimationSection` (`ExportScene.cpp:687-829`, 143 lines)
- Extract the 32-line warning-only "all channels filtered out" diagnostic dump (:723-755) as `LogFilteredChannelDiagnostics` [~15m]

## Critical files
- `DataPacker/Source/ExportJobs/Scene/SceneVerticesLoader.cpp`
- `DataPacker/Source/ExportJobs/Texture/Texture.{h,cpp}`
- `DataPacker/Source/ExportJobs/ExportScene.cpp`

## Out of scope
- The vestigial `nodeToJointMap`/`DetermineAnimationPath` removal — `Architecture_SceneLoaderVestigialPaths.md`.
- The meshoptimizer vertex-remap replacement — `Architecture_LibraryReplacement.md` (execute together with or after this decomposition; it restructures the same dedup block).
- `EncodeWithRdo`'s 9-parameter list — borderline; production callers go through `ToBc4/5/7`, leave unless touched.

## Notes
- No determinism/pack-byte exposure — pure intra-file extraction; `.MODEL`/chunk bytes identical, no version bumps.
- File-group overlap: `SceneVerticesLoader.cpp`/`ExportScene.cpp` shared with the Scene architecture plans; `Texture.cpp` with `Refactor_TextureUtilDedup.md` — co-schedule.

## Verification Notes
- All spans verified exact: `LoadVertices` :88-347 (260 lines) with seams :96-130 / :167-207 / :237-296 / :301-345; the node-local-matrix block does duplicate `ComputeNodeWorldTransform`'s TRS/matrix branch (:49-71) — same TRS composition (`S * R * T`) and the same column-major-as-row-major matrix load, differing only in local-variable structure, so the shared `NodeLocalMatrix(rNode)` is sound; `Texture::Texture` file ctor :58-217 (160 lines) with branches kImage :62-92, kFloat32 :93-115, kUint16Raw :116-140, EXR :141-216; `WriteAnimationSection` :687-829 (143 lines) with the diagnostic dump at :723-755.
- Note for the per-`FileType` loader extraction: the EXR branch is the `else` arm (no explicit `kExr` test) and the kFloat32/kUint16Raw branches require caller-supplied `miWidth`/`miHeight` (asserted at :95/:120) — keep those preconditions on the extracted loaders.
- The `LogFilteredChannelDiagnostics` extraction (:723-755) reads `nodeToJointMap` — if `Architecture_SceneLoaderVestigialPaths.md` lands first, the dump's map-contents section reduces to the node list/bool; co-schedule or extract after.
