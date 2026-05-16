# Water Fragment Descriptor-Set Audit

## Context

`Engine/Data/Shaders/Water/Water.frag` currently declares all 10 sampler bindings at `set = 1`:

```glsl
layout (set = 1, binding = 2)  uniform sampler2D pLightingSamplers[3];
layout (set = 1, binding = 3)  uniform sampler2D shadowTextureSampler;
layout (set = 1, binding = 4)  uniform sampler2D objectShadowsTextureSampler;
layout (set = 1, binding = 5)  uniform sampler2D elevationTextureSampler;
layout (set = 1, binding = 6)  uniform samplerCube skyboxSampler;
layout (set = 1, binding = 7)  uniform sampler2D noiseTextureSampler;
layout (set = 1, binding = 8)  uniform sampler2D pWaterNormalSamplers[17];
layout (set = 1, binding = 9)  uniform sampler2D depthLutSampler;
layout (set = 1, binding = 10) uniform sampler2D smokeSampler;
layout (set = 1, binding = 11) uniform sampler2D ambientLightingSampler;
```

Per the convention in `Engine/Data/Shaders/CLAUDE.md`:

> **Multi-set descriptors**: Set 0 = global (UBOs, samplers, bindless textures), Set 1 = per-pipeline (SSBOs, combined image samplers), Set 2 = per-material (models only).

Of the 10 bindings, only `pWaterNormalSamplers[17]` (the bindless atlas) is arguably per-pipeline. The remaining nine are global resources also used by other graphics pipelines (`Terrain.frag`, `Model.frag`, etc.), so by convention they should be on set 0.

Surfaced from the `02_Water.md` shader-review plan as an independent line item. Split off because the audit and migration is a multi-shader, multi-file graphics-layer change that exceeded the score budget of the parent plan.

## Design

Two-phase:

1. **Audit (read-only).**
   - Locate the water graphics pipeline layout on the C++ side (under `Engine/Source/Graphics/Pipelines/` or wherever the water-pipeline descriptor-set layouts are declared).
   - Confirm which of the nine candidate samplers are already exposed by set 0 in adjacent pipelines (`Terrain`, `Model`, `Smoke`, etc.).
   - Cross-reference all first-party fragment shaders that declare the same sampler names — those bindings must move together so descriptor-set indices stay consistent across pipelines that share global resources.

2. **Migration.**
   - Move the nine confirmed-global samplers from `set = 1` to `set = 0` in `Water.frag` with matching bindings to set 0's existing layout.
   - Update sibling fragment shaders that share these bindings so they agree (`Terrain.frag`, `Model.frag`, etc. — exact list determined by the audit).
   - Update the C++ descriptor-set layouts and `vkUpdateDescriptorSets` write sites to bind these resources via set 0 instead of set 1.
   - Leave `pWaterNormalSamplers[17]` on `set = 1` (it stays per-pipeline).

## Out of scope

- Compute pipelines. The compute-shader `set = 1` migration is tracked separately by `Graphics/ShaderReview/DescriptorSetComputeSweep.md` and its `Engine/Architecture_MultiSetComputePipelines.md` prerequisite — both deal with bringing compute pipelines into structural parity with graphics, which is a different problem.
- Set-2 (per-material) bindings used by `Model.frag` — unrelated to this audit.
- Sampler / image-view object lifetime, allocation pool, descriptor pool sizing — only the `(set, binding)` placement changes.

## Acceptance criteria

- No Vulkan validation layer errors on engine startup or in-game (in particular no `VUID-vkCmdBindDescriptorSets-pDescriptorSets-00358` or `VUID-VkPipelineLayoutCreateInfo-pSetLayouts` errors).
- Water surface renders identically before/after at the visual level (no missing shadows, smoke, lighting, or skybox reflection).
- `Engine/Data/Shaders/CLAUDE.md` "Multi-set descriptors" convention now accurately describes `Water.frag` (no mention required, but spot-check that the documented convention matches reality).

## Critical files

- `Engine/Data/Shaders/Water/Water.frag` (sampler declarations at lines 17-26)
- `Engine/Data/Shaders/Terrain/Terrain.frag` (likely shares global samplers)
- `Engine/Data/Shaders/Model/Model.frag` (likely shares global samplers)
- `Engine/Source/Graphics/Pipelines/` — water pipeline-layout C++ (exact file determined during audit)
- `Engine/Source/Graphics/Render/` — `vkUpdateDescriptorSets` write sites for the affected resources

## Notes

- Cross-reference dependency: independent of `Engine/Architecture_MultiSetComputePipelines.md` and `Graphics/ShaderReview/DescriptorSetComputeSweep.md` per `Documents/Plans/Order.md` line 55. Those plans are compute-pipeline-specific; this audit is graphics-pipeline-specific.
- This plan was surfaced as an open question in the parent `Graphics/ShaderReview/02_Water.md` plan (line "lines 17-26 — all samplers use `set = 1` but several are global resources …"). The seven defensive fixes from that parent plan landed in the same session that created this follow-up; only the descriptor-set audit was carried forward.
