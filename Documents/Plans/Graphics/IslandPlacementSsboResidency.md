<!-- broken-engine-plan/v1 {"createdUtc":"2026-07-02T02:29:26.000Z","dependsOn":[]} -->
# Island Placement SSBO Residency

Part of the island resident-memory scaling series — see `IslandResidentMemoryScaling_Overview.md` (same directory). The smallest resident bucket; lowest priority of the series.

## Context

- `Islands::mIslandsStorageBuffers` (`Engine/Source/Graphics/Islands.h:44`) is a host-visible SSBO with one instance per framebuffer index. Each instance holds `miTemplateCount × kiMaxPlacementsPerTemplate(1024)` slots of `shaders::AxisAlignedQuadLayout` (60 B — `Engine/Data/Shaders/ShaderLayoutsBase.h:679-687`), and `kiMaxFramebuffers(4)` instances exist (`Managers/BufferManager.h:10`) ≈ **16.4 MiB at 70 templates**. All instances are allocated once in `Islands::Islands()` (`Islands.cpp:36-73`), permanently resident, scaling with total boot-time template count. Every template reserves a fixed 1024-placement slab regardless of how many placements it actually has on screen.
- `UpdateActiveIslands` (`Islands.cpp:95-259`) rewrites only the acquired framebuffer's instance each frame: it clears the previously written per-template ranges recorded in `mLastWrittenCounts` (`Islands.h:64`, clear loop `Islands.cpp:134-142`), then `EmitPlacement` packs active placements densely from each template's base `iTemplate * kiMaxPlacementsPerTemplate` (`Islands.cpp:167-198`), guarded by the overflow skip-with-assert at `Islands.cpp:172-178`.
- The per-template indirect `firstInstance` is baked once at boot to `iTemplate * kiMaxPlacementsPerTemplate` (`Islands.cpp:61-72`), and the record-once Global command buffer draws the full fixed slot count `miTemplateCount * kiMaxPlacementsPerTemplate` for the shadow-elevation and elevation prepasses (`Managers/CommandBufferRecordGlobal.cpp:80` and `:106-110`). The fixed per-template slab layout is therefore load-bearing for the record-once command buffers.
- The `Islands.h:11-18` sizing comment calls total SSBO memory "negligible"; its formula (`N_templates × 1024 × sizeof(AxisAlignedQuadLayout)`) omits the ×`kiMaxFramebuffers` factor (under-counts by 4×).

## Design (decision plan — present options)

This plan is not yet decision-complete: choose exactly one option below, informed by profiling, before implementation. Profile the real max placements-per-template before choosing.

- **A — Compact arena keyed on active templates.** Size the SSBO to `activeTemplateCap × kiMaxPlacementsPerTemplate` rather than total template count, with a per-frame active-template → slab-index map. Bounds memory by concurrent residency. Touches the per-frame `UpdateActiveIslands` write path (including `mLastWrittenCounts` bookkeeping), the boot-time indirect `firstInstance` bake (`Islands.cpp:61-72`), the fixed-count prepass draws (`CommandBufferRecordGlobal.cpp:80`, `:106-110`), and SSBO addressing in `Terrain.vert` — all of which currently assume a fixed per-template slab. Reconciling a dynamic slab map with the record-once command buffers is the hard part.
- **B — Right-size `kiMaxPlacementsPerTemplate`** (`Islands.h:19`). If 1024 over-provisions the actual max placements per template, lower the constant — cheap, partial, no addressing change; the overflow guard (`Islands.cpp:172-178`) skips excess with an assert.
- **C — Accept + fix the misleading comment.** 16.4 MiB may not justify per-frame-path churn; correct the `Islands.h:11-18` comment (add the ×`kiMaxFramebuffers` factor) and record the rationale.

## Scope contract

The listed scope is both target and ceiling: after the option decision, make the smallest complete change for that option only. Add no abstractions, configuration, refactors, or fixes to adjacent code encountered along the way. Naming a file grants no permission to touch anything in it beyond the regions named below plus the mechanical necessities (includes, declarations) the named change requires.

**In scope** (per chosen option):

- `Engine/Source/Graphics/Islands.h` — `kiMaxPlacementsPerTemplate` (`:19`) and its `:11-18` sizing comment; for option A also the `mIslandsStorageBuffers` (`:44`) and `mLastWrittenCounts` (`:64`) declarations/comments as the new sizing requires.
- `Engine/Source/Graphics/Islands.cpp` — `Islands::Islands()` SSBO sizing and indirect `firstInstance` bake (`:33-73`); `UpdateActiveIslands` (`:95-259`) slab clearing and `EmitPlacement` addressing (options A/B only; option C touches neither).
- `Engine/Source/Graphics/Managers/CommandBufferRecordGlobal.cpp` — the two fixed-count prepass draw counts (`:80`, `:106-110`) — option A only.
- `Engine/Data/Shaders/Terrain/Terrain.vert` — SSBO indexing via `firstInstance`/`gl_InstanceIndex` (`pQuads[gl_InstanceIndex]` reads) — option A only.

**Out of scope:**

- Other residency buckets of the series (see overview); the `kiMaxIslands` cap; island texture-slot residency/eviction; the indirect buffers' own memory; any other member, function, or comment in the files above.

## Risk tier

Tier 2 — scoped client graphics behavior (client-only; no determinism/CRC, `.pack`, `kiVersion`, or protocol exposure). Option C alone is Tier 1 (comment-only). Option A touches the record-once-CB `firstInstance` bake and the per-frame SSBO write hot path — playtest required.

## Acceptance criteria

- Per-template SSBO bytes bounded by concurrent residency (A), or reduced via a profiling-justified lower `kiMaxPlacementsPerTemplate` (B), or the `Islands.h:11-18` comment corrected with the ×`kiMaxFramebuffers` factor and the accept rationale recorded (C).
- Options A/B: island terrain, elevation, and shadow-elevation rendering visually unchanged in a playtest; the `Islands.cpp:172-178` overflow assert does not fire.

## Notes

- **Decision plan**: A vs B vs C for `/external-grill-plan`; profile first.
- Lowest priority of the residency series (smallest bucket).
