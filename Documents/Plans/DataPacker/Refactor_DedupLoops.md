# Refactor: Deduplicate Copy-Pasted Loops

Source: /external-refactor-clean on DataPacker/Source/ExportJobs/

Goal: two hot spots contain near-identical copy-pasted loop bodies that can be consolidated with a single helper each.

## Changes

### DataPacker/Source/ExportJobs/ExportShader.cpp
- Lines 298-405: SIX near-identical `if (shaderResources.X.size() > 0) { LOG... for (rResource) { get_decoration... WriteBinding... iBindingCount = std::max(...); } }` blocks where X is `uniform_buffers`, `storage_buffers`, `sampled_images`, `storage_images`, `separate_images`, `separate_samplers`. Each block differs in: the descriptor type passed to `WriteBinding`, the descriptor-count computation (some `1`, some `rSpirvType.array[0]`, `separate_images` uses `UINT32_MAX` sentinel for runtime-sized arrays). Extract a helper:
  ```
  template <typename CountFn>
  void CollectBindings(const std::vector<spirv_cross::Resource>& resources,
                       const char* pcLabel,
                       VkDescriptorType descriptorType,
                       spirv_cross::Compiler& compiler,
                       VkDescriptorSetLayoutBinding* pBindings,
                       uint32_t* pSetIndices,
                       int64_t& riBindingCount,
                       common::ChunkFlags_t chunkFlags,
                       CountFn&& countFn);
  ```
  Call 6x with the appropriate descriptor-type + count-functor. Removes ~100 lines, eliminates drift risk when SPIRV-Cross changes an API [~30m]
- Lines 260-285: the stage_inputs reindex is O(n²) — outer `for i` with inner `for rResource` searching for matching location. Replace with a first pass that builds `std::vector<std::pair<uint32_t, const spirv_cross::Resource*>>` sorted by location, then iterates once. Typical stage_input count is ≤16, so O(n²) is not a real performance problem — the payoff is readability [~15m]

### DataPacker/Source/ExportJobs/ExportSceneVertices.cpp
- Lines 192-245 in `LoadVertices`: five copy-pasted blocks for `TEXCOORD_0` through `TEXCOORD_4`. Factor into `LoadAttribute(rPrimitive, rModel, attributeName, ...)` returning `{ptr, stride}`; call five times in a small loop or array-indexed [~30m]

### DataPacker/Source/ExportJobs/ExportShader.cpp, ExportFont.cpp
- Read-whole-file-into-vector pattern appears at: `ExportFont.cpp:54-57`, `ExportShader.cpp:243-247`. Add `common::ReadFile(path) -> std::vector<std::byte>` in `Common/` and replace these two sites. (Two sites is a borderline YAGNI case; accept if the helper goes into `Common/Utils.h` alongside existing `Read`/`Write` binary I/O helpers for consistency with the engine-wide pattern.) [~15m]

## Expected Outcome

- ExportShader.cpp: ~100 lines removed, explicit SPIRV-Cross descriptor-table coupling
- ExportSceneVertices.cpp: ~60 lines removed
- `ExportShader.cpp:260-285` no longer O(n²) (minor — data set is tiny)

## Verification Notes

- Corrected the ExportShader binding-loop count: SIX blocks, not seven. `subpass_inputs` is NOT present in this file. Removed that entry from the list. Line range adjusted from 298-406 to 298-405.
- Clarified that the six blocks differ in more than the descriptor type — the descriptor-count computation varies too (constant 1 / `array[0]` / `UINT32_MAX` sentinel). Helper signature updated to accept a `CountFn` callable.
- REMOVED the `ExportIsland::Export` decomposition bullet. Investigation: `ExportIsland.cpp` is 184 lines total, not "~170 lines of one function". `Export()` is well below the `/reduce-file` threshold and the five sections (AO, Color, Elevation, Normals, Beach-Elevation) are short, already-separated blocks. Decomposing into 5 named helpers trades readability for ceremony. KISS/YAGNI reject.
- Corrected the `common::ReadFile` helper plan: the investigated sites at `ExportRaw.cpp:33-36`, `ExportModel.cpp:22-42`, and `ExportTexture.cpp:367-373` do NOT fit the "read whole file into a fresh std::vector" pattern. They interleave field reads or stream directly into a pre-allocated span. Only `ExportFont.cpp:54-57` and `ExportShader.cpp:243-247` are true candidates (2 sites). Downgraded the helper recommendation to borderline-YAGNI, accepted only because the engine-wide `common::Read/Write` pattern argues for consistency.
