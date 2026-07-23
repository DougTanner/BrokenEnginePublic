<!-- broken-engine-plan/v1 {"createdUtc":"2026-07-02T02:29:26.000Z","dependsOn":[]} -->
# Island Placement SSBO Residency

Part of the island resident-memory scaling series — see `IslandResidentMemoryScaling_Overview.md`. The smallest resident bucket; lowest priority of the series.

## Context

`Islands::mIslandsStorageBuffers` (`Islands.h:44`) is a per-framebuffer host-visible SSBO sized `miTemplateCount × kiMaxPlacementsPerTemplate(1024) × sizeof(AxisAlignedQuadLayout)(60 B) × kiMaxFramebuffers(4)` ≈ **16.4 MiB at 70 templates**, permanently resident, scaling with total template count. It reserves a fixed 1024-placement slab per template regardless of how many placements a template actually has on screen. `UpdateActiveIslands` (`Islands.cpp:107-177`) zeroes it each frame and writes only active-template slots; the per-template indirect `firstInstance` is baked at boot to `iTemplate * kiMaxPlacementsPerTemplate` (`Islands.cpp:62-69`), so the fixed slab layout is load-bearing for the record-once CB.

The `Islands.h:16-18` comment calls this "negligible" but omits the ×4 framebuffer factor (under-counts by 4×).

## Design (decision plan — present options)

- **A — Compact arena keyed on active templates.** Size the SSBO to `activeTemplateCap × kiMaxPlacementsPerTemplate` rather than total template count, with a per-frame active-template → slab-index map. Bounds by concurrent residency. Touches the per-frame `UpdateActiveIslands` write path **and** the indirect `firstInstance` bake (`Islands.cpp:62-69`), which currently assumes a fixed per-template slab — reconciling that with the record-once CB is the hard part.
- **B — Right-size `kiMaxPlacementsPerTemplate`.** If 1024 over-provisions the actual max placements per template, lower it — cheap, partial, no addressing change.
- **C — Accept + fix the misleading comment.** 16.4 MiB may not justify per-frame-path churn.

Profile the real max placements-per-template before choosing.

## Critical files
- `Engine/Source/Graphics/Islands.{h,cpp}` — `mIslandsStorageBuffers` (`:44`), `UpdateActiveIslands` (`:107-177`), indirect `firstInstance` bake (`:62-69`), the `:16-18` comment.
- `Engine/Data/Shaders/Terrain/Terrain.vert` — reads the SSBO via `firstInstance`/`gl_InstanceIndex` (option A changes the addressing).

## Out of scope
- Other residency buckets (see overview); the `kiMaxIslands` cap.

## Acceptance criteria
- Per-template SSBO bytes bounded by concurrent residency (A) or reduced via a justified `kiMaxPlacementsPerTemplate` (B) — or the comment corrected + rationale recorded (C).

## Notes
- Client/graphics-only; no determinism/CRC/`.pack`/`kiVersion` exposure. Option A touches the record-once-CB `firstInstance` bake and the per-frame SSBO write hot path — playtest.
- **Decision plan**: A vs B vs C for `/external-grill-plan`; profile first.
- Lowest priority of the residency series (smallest bucket).
