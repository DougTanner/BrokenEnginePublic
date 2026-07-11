# Extract Shader Dependency Cache

## Context

`DataPacker/Source/ExportJobs/ExportShader.cpp` is 657 lines and now contains two cohesive responsibilities. The compilation/reflection pipeline runs Vulkan SDK tools and emits shader chunks; the dependency-cache path parses shaderc depfiles, serializes root-relative input fingerprints, checks cache dirtiness, rechecks input stability, and atomically commits `.deps.meta`. Review identified that second responsibility as a natural file boundary.

## Design

- Add a sibling `ExportShaderDependencies.cpp` and move the dependency-only implementation there: `IsDependencyInInputRoot`, `ReadDependencyMetadata`, `ParseDependencyFile`, `ExportShader::CheckDirty`, `CaptureDependencies`, `AreCachedInputsStable`, and `UpdateCacheMetadata`, plus their metadata constants/private record type.
- Keep the existing `ExportShader` class and private `DependencyFingerprint` state in `ExportShader.h`; do not introduce a second cache abstraction unless the move exposes a compile-time need.
- Leave `GetVulkanSdkBinariesDirectory`, preprocessing, compilation, optimization, SPIR-V reflection, chunk writing, and failure cleanup in `ExportShader.cpp`.
- Preserve the dependency metadata magic/version and binary layout, root-index + normalized-relative-path identity, Git/content fingerprint behavior, shaderc's unescaped-space disambiguation, and the post-read stability check.
- Add the new TU to `DataPacker.vcxproj` and its filters. Compile DataPacker and exercise clean-cache, changed-include, and include-path-with-spaces cases.

## Critical files

- `DataPacker/Source/ExportJobs/ExportShader.cpp` — current dependency helpers and `ExportShader` dependency-cache member definitions.
- `DataPacker/Source/ExportJobs/ExportShader.h` — `DependencyFingerprint` and dependency-cache state shared by the split definitions.
- `DataPacker/Source/InputFingerprint.{h,cpp}` — existing fingerprint provider; unchanged and reused.
- `DataPacker/Platforms/VisualStudio2026/DataPacker.vcxproj` and `.filters` — new TU membership.

## Out of scope

- Changing shaderc/glslang/spirv-opt command lines, optimization policy, SPIR-V reflection, or shader chunk contents.
- Changing dependency metadata format/version, input fingerprint algorithms, cache invalidation semantics, or ambiguous depfile handling.
- Generalizing dependency caching to non-shader export jobs.

## Acceptance criteria

- `ExportShader.cpp` is centered on compile/optimize/reflect/export flow; dependency parsing and metadata persistence live in the focused sibling TU.
- Existing dependency metadata remains readable and clean shaders reuse their cached chunks.
- Editing an included shader file dirties the shader, and paths containing spaces are still delimited or rejected exactly as before the move.
- DataPacker builds with the new TU registered in the project and filters.

## Notes

- This is a move-only refactor. Preserve error text and trust-boundary validation because depfiles and cache metadata are opaque file input.
- Invariant exposure: offline DataPacker only. Shader source edits still require repacking, but this plan does not change emitted SPIR-V, `.pack` layout, `kiVersion`, runtime determinism/CRC, replay, client/server guards, or allocation-tracked paths.

