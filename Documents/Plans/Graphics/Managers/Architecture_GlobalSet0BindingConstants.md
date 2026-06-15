# Architecture: Global Set-0 Binding-Number Constants (Graphics/Managers + Shaders)

## Context

Split out of `Architecture_ShaderCpuContractConstants.md` (the seven-item contract-constant plan) at
`/external-grill-plan` — the user chose to make this its own plan rather than fold the C++-only half
into the parent. The parent plan executed the other six items.

The global Set-0 descriptor binding numbers `0/1/3/4/12` are bare integer literals on the C++ side
(`TextureDescriptors.cpp` layout creation `~:25-31`, descriptor writes `~:104-108`, and
`UpdateTextureArrayDescriptors` `~:125`) AND hardcoded in `layout(set = 0, binding = N)` qualifiers
across dozens of shaders (e.g. `Model.frag`, `Billboards.frag`, every shader that reads the global
UBOs / repeat sampler / bindless texture array / clamp sampler). The two sides are kept in lockstep
**by comment only** — a binding-number change on one side silently mismatches the other.

## Design

Single-source the global Set-0 binding numbers in the dual-language `Engine/Data/Shaders/ShaderLayoutsBase.h`:

1. Add `CONSTEXPR int kiGlobalBinding* = N;` constants (one per binding: globalUniform=0, mainUniform=1,
   samplerRepeat=3, bindlessTextures=4, samplerClamp=12). `CONSTEXPR` resolves to `inline constexpr`
   (C++) / `const` (GLSL) so both languages see them. Match the `ki` naming convention (Shaders/CLAUDE.md).
2. **C++ side:** replace the bare `.binding =`/`.dstBinding =` literals in `TextureDescriptors.cpp`
   (layout `pBindings`, writes `pWrites`, and `UpdateTextureArrayDescriptors`) with the constants.
3. **Shader side (the large sweep):** rewrite every `layout(set = 0, binding = N)` in
   `Engine/Data/Shaders/**` to reference the constants. GLSL accepts `const int` in `layout(binding = ...)`
   only via constant expressions — verify glslang accepts the `CONSTEXPR` constants in a layout qualifier
   before committing to the full sweep; if it does not, keep the C++-side single-sourcing + a comment
   pointing shaders at the constants, and record the GLSL limitation here.
4. Requires a full DataPacker shader rebuild; render output is bit-identical (values unchanged).

## Critical files
- `Engine/Data/Shaders/ShaderLayoutsBase.h` (new `kiGlobalBinding*`)
- `Engine/Source/Graphics/Managers/TextureDescriptors.cpp` (layout + writes + array update)
- `Engine/Data/Shaders/**` (every shader declaring global Set-0 bindings — dozens of files)

## Out of scope
- The other six items of `Architecture_ShaderCpuContractConstants.md` (already executed): dead
  `TerrainPipelineBindings`, the lighting format literal, the water descriptor tables, the LightCombine
  push-constant pun, `kBlurSalt`, and the `mppWaterNormalTextures[17]` magic.
- The Set-0 per-framebuffer *indexing* question (`Graphics/PipelineGraphicsGlobalSet0Indexing.md`) —
  that is the bind-time index choice in `Pipeline.cpp`, unrelated to these binding *numbers*.
- Changing any binding number's value.

## Acceptance criteria
- Each global Set-0 binding number has one definition in `ShaderLayoutsBase.h`; all C++ sites (and, if
  GLSL allows, all shader sites) reference it; client + DataPacker build clean; render output unchanged.

## Notes
- No determinism/CRC exposure — client render path only. The shader sweep needs a DataPacker recompile and renders identically.
- One open verification gated to execution: does glslang accept the shared `const int` constants inside
  `layout(set = 0, binding = ...)` qualifiers? If not, the shader-side sweep degrades to C++-side-only + a comment.
