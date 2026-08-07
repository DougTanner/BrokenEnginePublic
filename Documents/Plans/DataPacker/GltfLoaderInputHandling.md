<!-- broken-engine-plan/v1 {"createdUtc":"2026-08-06T22:03:22.466Z","dependsOn":[]} -->
# Fail fast on unsupported glTF input shapes in scene loaders

## Context

Three verified findings from `/external-deep-analysis` over `DataPacker/Source` (baseline `3cb5e9a6`), each an input shape the glTF 2.0 specification allows but the loaders silently mishandle. Source files are a trust boundary (root `AGENTS.md`), and the audio pipeline sets the repository precedent that unsupported-but-valid source shapes assert rather than silently degrade (`ExportJobs/AGENTS.md`, Audio Policy).

- Non-indexed primitives dropped: `AppendIndices` (`DataPacker/Source/ExportJobs/Scene/SceneVerticesLoader.cpp:210-215`) returns early when `rPrimitive.indices <= -1`. Spec-verified (glTF 2.0 §5.24.2, registry.khronos.org/glTF/specs/2.0/glTF-2.0.html#_mesh_primitive_indices): an omitted `indices` means legal non-indexed geometry in sequential vertex order, so such a primitive's topology is silently lost while its vertices are still appended.
- Sparse animation accessors read wrong data: `AccessorFloats` (`SceneAnimationLoader.cpp:6-13`) returns a raw pointer into the underlying buffer for `sampler.input`/`sampler.output` (`:111-112`). Spec-verified: sparse is legal on any accessor (§3.6.2.3, §5.1), and the vendored tinygltf (commit `81bd50c1`, `tiny_gltf.h:832/4708/7380`) parses sparse as metadata only — it never materializes sparse values into the buffer — so a legal sparse accessor yields silently wrong keyframes.
- Diagnostic throws instead of warning: `LogFilteredChannelDiagnostics` (`ExportScene.cpp:132`) calls `rGltfModel.nodes.at(rChannel.target_node)` whenever the target is merely nonnegative, while the loader filter (`SceneAnimationLoader.cpp:64-68`) also rejects upper-out-of-range targets; a channel targeting `nodes.size()` or greater makes the diagnostic itself throw `out_of_range` instead of reporting the rejected channel.

## Design

Decision: fail fast, do not add capability. Matching the audio-policy precedent, an unsupported-but-legal shape asserts/fails the owning job with a diagnostic naming the asset and primitive/channel:

- `AppendIndices` (or its caller) rejects a primitive with omitted `indices` instead of silently dropping it.
- `AccessorFloats` call sites reject accessors with `sparse` set (tinygltf `Accessor::sparse.isSparse`) before reading.
- `LogFilteredChannelDiagnostics` dereferences the node name only when `0 <= target_node < nodes.size()`, logging the raw index as invalid otherwise.

All current assets are indexed and dense, so exported bytes are unchanged for the existing asset set; export stays deterministic and version constants untouched.

## Critical files

- `DataPacker/Source/ExportJobs/Scene/SceneVerticesLoader.cpp`
- `DataPacker/Source/ExportJobs/Scene/SceneAnimationLoader.cpp`
- `DataPacker/Source/ExportJobs/ExportScene.cpp`

## In scope

- The three rejection/diagnostic changes above, at the named functions only.

## Out of scope

- Supporting non-indexed or sparse data (a capability addition; would be a Feature).
- `LoadAnimations` decomposition (owned by the export-job decomposition plan); any serialization change.

## Risk tier and invariants

Expected Change Workflow Tier 2. Trigger: scoped tool behavior at a file trust boundary; no layout or version exposure. Invariants: byte-identical export for the current asset set; a rejected asset fails its job through the existing failure disposition without publishing output.

## Acceptance criteria

- A test glTF with a non-indexed primitive, and one with a sparse animation sampler accessor, each fail their scene job with a diagnostic naming the offending asset, publishing nothing.
- A glTF whose only animation channel targets an out-of-range node logs the filter warning without throwing.
- A full export of the current asset set is byte-identical.
