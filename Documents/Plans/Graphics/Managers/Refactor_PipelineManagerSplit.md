# Refactor: PipelineManager.cpp /reduce-file Split (Graphics/Managers)

## Context

`Engine/Source/Graphics/Managers/PipelineManager.cpp` is 883 lines — over the 500-line `/reduce-file`
soft guideline. A repo-code-review this session flagged it as an **OPTIONAL, pre-existing** `/reduce-file`
candidate (only ~10 lines were touched this session; the size is not caused by this session's refactor).
The reviewer noted a natural split by render subsystem: the `CreateLighting*` / `CreatePipelineShadows`
group, the `CreateSmoke*` / `CreateParticle*` / `CreateDebugRender*` / `CreateTerrainData*` group.

The file is a single class (`PipelineManager`) whose ctor loads SPIR-V and orchestrates a load-bearing
boot sequence of pipeline-creation member functions; the eight `Create*` members have **no external
callers** (only the ctor invokes them — verified). The whole file is already client-only
(`#if defined(BT_CLIENT)` at `:1`, client-vcxproj-only per the `Managers/` guard convention).

**Prior-review constraint (must honor).** The landed `Refactor_PipelineAndDeviceDecomposition.md`
(Graphics/Managers `/external-refactor-clean`, executed + removed in commit `cc8fcbc4`) explicitly
**cleared `PipelineManager`'s big declarative pipeline tables as cohesive-as-is** — specifically the
ctor and `CreateLightingShadowDependentPipelines` — and listed them as out-of-scope: "do not split."
That judgment was about not *fragmenting individual functions* into helpers (an in-function-decomposition
lens). It does **not** conflict with a *file-level* split that relocates **whole** `Create*` functions to
sibling TUs. This plan therefore does whole-function relocation only: no function body is broken apart,
and the ctor's boot orchestration is not touched. The one open question for `/external-grill-plan` is
whether to split the file at all given that prior "cohesive" verdict, vs. accept-and-document the size.

## Design

Keep `PipelineManager.h` exactly as-is (class declaration + the eight `Create*` member decls +
`FillWaterSharedDescriptors` + `VerifyAllDescriptorGenerations` + the `gpPipelineManager` global). All
member-function definitions can live in any TU that includes the header — the standard "split member defs
across TUs" pattern.

**Stays in `PipelineManager.cpp`** (~220 lines): the shader-load loop + trust-boundary validation, the
ctor (`PipelineManager::PipelineManager`, `:14-164` — the load-bearing boot-order orchestration **and**
its inline `kPipelineLog` / `kPipelineProfileText` / `kPipelineUiDepthPrepass` / `kPipelineDebugTexture`
creations), the dtor (`:166-172`), and `VerifyAllDescriptorGenerations` (`:839-883` — a per-frame Global-
record verifier, not a boot `Create*`).

**New TU 1 — `PipelineManagerLightingShadow.cpp`** (~340 lines): the lighting/shadow/water cluster —
- `CreateLightingPipelines` (`:174`)
- `CreatePipelineShadows` (`:257`)
- `CreateLightingBlurPipelines` (`:340`)
- `CreateLightingShadowDependentPipelines` (`:367`)
- `FillWaterSharedDescriptors` (`:502`) — water descriptor-prefix helper, called from
  `CreateLightingShadowDependentPipelines` (water pipelines are built inside it), so it moves with it.

**New TU 2 — `PipelineManagerEffects.cpp`** (~320 lines): the terrain/smoke-wind/particle/debug cluster —
- `CreateTerrainDataPipelines` (`:518`)
- `CreateSmokeWindPipelines` (`:540`)
- `CreateParticlePipelines` (`:699`)
- `CreateDebugRenderPipelines` (`:792`)

Each new TU mirrors the home file's prologue: whole-file `#if defined(BT_CLIENT)` wrap, the same
`#include "Graphics/Managers/PipelineManager.h"` (+ `Data/Shader.h` / `Data/Texture.h` as the moved bodies
require), the file-scope `using enum DescriptorFlags;` + `using enum PipelineFlags;` the `Create*` bodies
rely on, and the `namespace engine { ... }` block.

Exact TU granularity (2 new files as above, or a different bucketing) is a `/reduce-file` execution-time
call — all three resulting TUs land well under 500 lines with the 2-file split, which is the KISS target.

**Build wiring (client only).** Add the two new cpps to the **client** vcxproj
`Projects/BrokenEngineSandbox/Platforms/VisualStudio2026/BrokenEngineSandbox.vcxproj` + its `.filters`
under the `Engine\Graphics\Managers` filter. Do **not** add them to
`BrokenEngineSandboxServer.vcxproj` — like every other `Managers/` cpp they are client-vcxproj-only, and
the `#if defined(BT_CLIENT)` wrap makes an accidental server add fail at the preprocessor.

## Critical files

- `Engine/Source/Graphics/Managers/PipelineManager.cpp` (shrinks to ctor/dtor/shader-load/verify)
- `Engine/Source/Graphics/Managers/PipelineManager.h` (unchanged — declarations + `gpPipelineManager`)
- `Engine/Source/Graphics/Managers/PipelineManagerLightingShadow.cpp` (new)
- `Engine/Source/Graphics/Managers/PipelineManagerEffects.cpp` (new)
- `Projects/BrokenEngineSandbox/Platforms/VisualStudio2026/BrokenEngineSandbox.vcxproj` (+ `.filters`)

## Out of scope

- **Any change to function bodies, the boot/creation order, or the ctor orchestration.** Pure relocation;
  behavior byte-identical. The ctor's call sequence (which encodes the load-bearing dependency order —
  e.g. lighting-spread buffers + BRDF LUT before lighting pipelines, water-normal textures before
  `kPipelineWater`) stays in `PipelineManager.cpp` unchanged.
- **Fragmenting the declarative pipeline tables** (the ctor, `CreateLightingShadowDependentPipelines`,
  the Water/WaterSkyboxOne descriptor mirroring) — cleared as cohesive by the landed
  `Refactor_PipelineAndDeviceDecomposition` review; this split moves whole functions only.
- **Splitting `PipelineManager.h`** — the header stays single; no private `*Internal.h` (no cross-TU
  helper exists — every moved function is already a public member).
- **`DynamicPipelines` / `InstanceManager` / `DeviceManager`** — those were the actual targets of the
  landed `Refactor_PipelineAndDeviceDecomposition`; not touched here.
- Any `DataPacker`/`.pack` payload-offset edits (that is `DataPacker/Architecture_PayloadLayoutSingleSource.md`).

## Acceptance criteria

- Client builds clean; pipeline-creation logs and rendering identical (move-only).
- `PipelineManager.cpp` and both new TUs are each under the 500-line soft guideline.
- New TUs are in the client vcxproj/`.filters` only and carry the `#if defined(BT_CLIENT)` wrap.

## Notes

- **Invariant exposure: none beyond build wiring.** Move-only; **no** `kiVersion` / `.pack`-layout / CRC /
  determinism / network-wire exposure; client/graphics-only, boot-path only (no per-frame or cross-frame
  state). The only risks are (a) build wiring (new files in the correct — client-only — vcxproj/filters)
  and (b) accidentally disturbing the load-bearing pipeline-creation order — fully contained because that
  order lives in the ctor, which does not move and must not be edited.
- File Groups: shares the Graphics/Managers "Pipeline/device" cluster (`PipelineManager.{h,cpp}`) with
  `Architecture_PipelineRegistrationOwnership.md` (which reshapes the `Pipeline::Create`/`Write` paths the
  `Create*` bodies call) and `Architecture_BindlessSlotLifecycle.md` (cites
  `PipelineManager.cpp:856-862`, the `VerifyAllDescriptorGenerations` carve-out that stays in the home
  TU). A file split invalidates their line citations — sequence this split first, or refresh their cites
  after it lands. Both of those plans edit `PipelineManager.cpp` lines that remain in `PipelineManager.cpp`
  after the split (the registration paths are called from the moved `Create*` bodies, but the verifier and
  any ctor-side edits stay home), so the split is a citation refresh for them, not a relocation.
- Tag: **Plan only — `/reduce-file` runs at execution.** Grill decision pre-staged above (split vs
  accept-and-document, given the prior "cohesive tables" verdict).
