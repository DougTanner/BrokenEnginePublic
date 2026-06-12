# Shader Reflection Scalar Input & Unsized-Array Descriptor Edges

## Context

A final audit of the just-landed `ShaderReflectionGuards` changes surfaced two pre-existing quirks in `ReflectAndWriteShader` (`DataPacker/Source/ExportJobs/ExportShader.cpp`), both verified against current source:

1. **Scalar stage-input falls into the vec4 format default.** The stage-input format mapping (`ExportShader.cpp:380`) is `vecsize == 2 ? R32G32 : (vecsize == 3 ? R32G32B32 : R32G32B32A32)`. A scalar float input (`vecsize == 1`) lands in the `R32G32B32A32_SFLOAT` (vec4) default arm, while the stride accumulator advances only `vecsize * sizeof(float)` = 4 bytes (`ExportShader.cpp:386`). `Engine/Data/Shaders/Objects/HexShield.vert` location 7 `in float fJoint` hits this today. It is benign per Vulkan attribute-fetch rules (extra components beyond the buffer-supplied data are discarded; the 16-byte fetch stays inside HexShield's 100-byte stride), but it becomes a buffer over-read if a scalar is ever the LAST attribute in a tightly-sized vertex buffer.

2. **`arrayCount` lambda emits 0 for an unsized array.** The `arrayCount` lambda (`ExportShader.cpp:401`) returns `rType.array[0]`, which SPIRV-Cross reports as `0` for a runtime-sized (unsized) array. It is used for the storage-buffer, sampled-image, and storage-image categories (`ExportShader.cpp:407-409`). An unsized array in any of those categories would emit a `descriptorCount` of 0, which both (a) writes a useless 0-count binding and (b) disarms the just-landed `WriteBinding` double-write guard (`ExportShader.cpp:149`, `ASSERT(descriptorCount == 0)`) for that binding slot — a second write to the same slot would then silently overwrite the first. No current shader does this (bindless arrays all route through `runtimeArrayCount` via `separate_images`, `ExportShader.cpp:410`), so this is latent.

The sibling `runtimeArrayCount` lambda (`ExportShader.cpp:403`) already handles the `array[0] == 0` case by substituting the `UINT32_MAX` sentinel that `PipelineManager` rewrites to the actual count at layout creation.

## Design

### Item 1 — explicit scalar format arm

Add the `vecsize == 1 → VK_FORMAT_R32_SFLOAT` arm to the format ternary in `ReflectAndWriteShader` (`ExportShader.cpp:380`):

```
format = rSpirvType.vecsize == 1 ? VK_FORMAT_R32_SFLOAT
       : rSpirvType.vecsize == 2 ? VK_FORMAT_R32G32_SFLOAT
       : rSpirvType.vecsize == 3 ? VK_FORMAT_R32G32B32_SFLOAT
       : VK_FORMAT_R32G32B32A32_SFLOAT;
```

The stride accumulator (`iVertexInputStride += rSpirvType.vecsize * sizeof(float)`) already advances 4 bytes for a scalar, so the **stride is unchanged** — only the format enum byte stored in the chunk's `VkVertexInputAttributeDescription` payload changes. HexShield's 100-byte stride and the `PipelineCreator.cpp:462` stride ASSERT against the bound vertex buffer are unaffected.

Rendering of HexShield is identical by reasoning: the engine consumes the format only as a `VkVertexInputAttributeDescription` passthrough — `PipelineCreator.cpp:465`/`:474` hands `pVertexShader->mInfo.pVertexAttributes` straight to `vkPipelineVertexInputStateCreateInfo.pVertexAttributeDescriptions` with no transformation. At the same offset, the GLSL `in float fJoint` reads only the `.x` component (a scalar input variable consumes only the first component regardless of the attribute's declared format), so R32_SFLOAT vs R32G32B32A32_SFLOAT produce the identical value in the shader; the only difference is the GPU no longer fetches three unused components.

**Baked-output / version concern:** this changes the format enum in the chunk payload for any shader with a scalar vertex input (HexShield today). `ExportShader::GetVersion` (`ExportShader.h:32`) is `Version(14 + VK_HEADER_VERSION)`. Bumping the raw literal `14` invalidates cached shader chunks so they re-export with the corrected format. **Open decision (grill):** whether to bump the literal now. If not bumped, a clean rebuild still re-exports HexShield's chunk (its `.vert`/`.d` deps drive `CheckDirty`), but a machine with a warm cache and an unmodified `HexShield.vert` would keep serving the old vec4 format. Recommended: bump the literal to force the one-time re-export deterministically.

### Item 2 — unsized-array sentinel handling in `arrayCount`

Make `arrayCount` (`ExportShader.cpp:401`) mirror `runtimeArrayCount`'s 0-to-sentinel handling, OR add `ASSERT(rType.array.empty() || rType.array[0] != 0)`. Since no current shader exercises the unsized-array-in-these-categories path, the ASSERT is the simpler fail-loud choice (KISS) and matches the file's existing fail-loud posture; mirroring `runtimeArrayCount` would silently accept a configuration the engine's bindless-array consumer map is not currently wired to handle for these descriptor types. **Pre-stage for grill:** ASSERT (recommended, fail-loud) vs mirror `runtimeArrayCount` (permissive). Note that an ASSERT here also protects the new `WriteBinding` double-write guard from being disarmed by a 0 count.

## Critical files

- `DataPacker/Source/ExportJobs/ExportShader.cpp` — the format ternary in `ReflectAndWriteShader` (`~:380`) and the `arrayCount` lambda (`~:401`).
- `DataPacker/Source/ExportJobs/ExportShader.h` — `GetVersion()` raw-version literal (`:32`), if the version bump is taken.
- `Engine/Data/Shaders/Objects/HexShield.vert` — the only current consumer of a scalar vertex input (location 7 `in float fJoint`); reference only, no edit.
- `Engine/Source/Graphics/Objects/PipelineCreator.cpp` — confirms the format is a `VkVertexInputAttributeDescription` passthrough (`:465`/`:474`) and the stride ASSERT (`:462`); reference only, no edit.

## Out of scope

- Non-float scalar inputs (int/uint vertex attributes) — the existing `ASSERT(rSpirvType.basetype == Float || !(mChunkFlags & kVertex))` at `ExportShader.cpp:376` already rejects non-float vertex inputs; this plan does not add int/uint format support.
- The `ShaderReflectionGuards` guards themselves (`WriteBinding`'s `descriptorCount == 0` ASSERT, the basetype ASSERT) — those landed and are not modified; this plan only ensures item 2 does not disarm them.
- glslang / SPIRV-Cross behavior — we treat SPIRV-Cross's `array[0] == 0` unsized-array convention and scalar-input-reads-first-component GLSL semantics as fixed contracts, not things to change.
- Any half/double (`SPIRType::Half`/`Double`) format handling — those are distinct base types already noted in the existing comment and out of this plan's scope.

## Notes

- **Offline DataPacker only.** No engine runtime code changes; no determinism / CRC / replay / network exposure. The engine side is a pure `VkVertexInputAttributeDescription` passthrough.
- **Invariant exposure: `.pack`/chunk layout.** Item 1 changes the baked format enum byte in the shader chunk payload for shaders with a scalar vertex input (HexShield today). This is not a struct-size change, so `Version()`'s `sizeof(ChunkHeader)` fold does not auto-invalidate it; it needs the raw `14` literal bumped in `GetVersion` for a deterministic one-time re-export across warm-cache machines. Item 2 changes no baked bytes (no current shader hits it).
- **Pre-staged open decisions for `/external-grill-plan`:**
  1. Item 1 — bump the `GetVersion` raw `14` literal now (recommended) vs rely on `CheckDirty` dep-tracking to re-export HexShield on the next shader edit.
  2. Item 2 — `ASSERT(array[0] != 0)` fail-loud (recommended) vs mirror `runtimeArrayCount`'s `UINT32_MAX` sentinel.
