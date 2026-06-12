# Architecture: Scene Loader Vestigial Paths

## Context
Source: /external-architecture-review on `DataPacker/Source/ExportJobs` (recursive). Understanding scene export requires bouncing across `ExportScene.cpp` plus three loader TUs, and two of the threads running through them are vestigial — they advertise structure that no longer exists. A third finding is a same-file duplication that can silently dangle texture CRCs.

## Design

### Remove the identity `nodeToJointMap`
- `LoadSkeletonData` builds `nodeToJointMap` as an identity map (`i → i` for every node, `SceneSkeletonLoader.cpp:84-88`) which is then threaded through three TUs and used only as: a non-emptiness boolean (`SceneVerticesLoader.cpp:244`), a membership test always true for valid nodes (`ExportScene.cpp:400-405` — the ancestor walk therefore always terminates at its first iteration when the map is non-empty), and a membership-filter + identity passthrough (`SceneAnimationLoader.cpp:76-87`). Replace with a `bool` (e.g. `bHasSkeleton`) and direct node indices; update `SkeletonData`/`LoadVerticesContext`/loader signatures accordingly. Two behavior-preserving requirements: (a) `LoadAnimations`' map lookup also filters channels with `target_node` of -1 / out-of-range — the replacement must keep an explicit `target_node >= 0 && target_node < nodes.size()` check; (b) joint indices written to output must be value-identical (the map is identity, so removing it is a no-op on bytes — verify) [~45m]

### Collapse the converged `DetermineAnimationPath` branches
- Both branches of `SetupSkeletonAndMaterials` call `LoadSkeletonData` identically (`ExportScene.cpp:298-312`), and `WriteAnimationSection` recomputes the path kind (:706-710) then also calls `LoadSkeletonData` unconditionally — the skeletal-vs-node-based distinction now affects only log text. Reduce `DetermineAnimationPath` to a log label (or fold away), removing the dead branch structure. The fold must preserve the *outer* gating: `LoadSkeletonData` runs iff `!skins.empty() || !animations.empty()` (skeletal branch fires whenever skins exist even with zero animations; node-based requires animations) — when neither holds, the map stays empty / `bHasSkeleton` stays false, which is load-bearing for the static-model vertex transform at `SceneVerticesLoader.cpp:244-252` [~20m]

### Merge the duplicated VkFormat→suffix mapping
- `GetTextureIntermediatePath` (`ExportScene.cpp:163-184`) and the inline relative-path build in `MainExport` (:545-563) each map VkFormat→filename-suffix; they must produce matching strings or the texture CRCs referenced from the scene chunk dangle (silent missing texture at runtime). Route both through one mapping — `MainExport` needs the *relative* path, so either build it as `mRelativeDirectory / GetTextureIntermediatePath(source, format).filename()` or extract a `TextureIntermediateSuffix(VkFormat)` helper both sites call [~20m]

## Critical files
- `DataPacker/Source/ExportJobs/ExportScene.cpp`
- `DataPacker/Source/ExportJobs/Scene/SceneSkeletonLoader.{h,cpp}`, `SceneAnimationLoader.{h,cpp}`, `SceneVerticesLoader.{h,cpp}`

## Acceptance criteria
- `.MODEL` and scene-chunk bytes identical before/after (vestigial-path removal is a no-op on output).
- No `nodeToJointMap` symbol remains; skeleton presence is a single boolean concept.
- Exactly one VkFormat→suffix mapping exists in `ExportScene.cpp`.

## Out of scope
- The `ComputeTextureFormats` double-computation (PreExport + MainExport, :266/:540) — deterministic and benign; leave.
- `LoadVertices` decomposition — `Refactor_SceneFunctionDecomposition.md`.
- The meshoptimizer vertex-dedup replacement — `Architecture_LibraryReplacement.md`.
- The two-phase PreExport/.MODEL design itself — documented intentional.

## Notes
- Pack/`.MODEL` bytes must not change; no version bump expected. If the identity-map removal turns out to alter written joint indices (it should not), stop and re-evaluate — that would mean the map was not identity in some path.
- File-group overlap with `Refactor_SceneFunctionDecomposition.md` and `Architecture_LibraryReplacement.md` (same TUs) — co-schedule or order explicitly.

## Verification Notes
- The identity-map claim was independently re-derived from source: `SceneSkeletonLoader.cpp:84-88` is the *only* writer (`insert_or_assign(i, i)` over all nodes, unconditional) and the three cited uses are the *only* reads across all four TUs. The header comment at `SceneSkeletonLoader.h:16` corroborates ("Builds identity nodeToJointMap (node index → node index)"). Deletion is sound.
- `DetermineAnimationPath` (`SceneAnimationLoader.cpp:28-49`) verified: its result selects between two branches that both call `LoadSkeletonData` identically and otherwise differ only in LOG text (`ExportScene.cpp:305/:310`, `:708`).
- The two VkFormat→suffix switches verified to currently emit identical strings (".BC4_UNORM_BLOCK"/".BC5_UNORM_BLOCK"/".BC7_UNORM_BLOCK") fed from the same `ComputeTextureFormats` result; the CRC-match coupling (intermediate file path → ExportTexture raw chunk path → `pTextureCrcs` at `ExportScene.cpp:564`) is real.
- Design bullets were tightened during verification with the `target_node` bounds-check requirement, the `!skins.empty() || !animations.empty()` gating condition, and the relative-vs-absolute path detail for the suffix merge — all behavior-preservation constraints, no scope change.
