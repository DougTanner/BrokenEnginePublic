# Architecture: DebugRender Dead Primitives (Box/Sphere)

## Context

Source: /external-architecture-review on `Engine/Source/Graphics/Debug`. `DebugRender::Box` and `DebugRender::Sphere` are dead public API: declared (`DebugRender.h:12-13`), defined (`DebugRender.cpp:62-87`), with full GPU infrastructure built behind them in the Debug config — pipelines, unit meshes, staging-type table entries, and per-frame indirect-draw records — yet a repo-wide grep finds **zero submission sites** (the only `DebugRender::Box(`/`::Sphere(` matches are the definitions themselves). `Circle`/`Line` are actively used (engine `MainUniforms.cpp` overlays, game `PlayersRender.cpp`). Until a caller exists, the Box/Sphere paths cost dead code plus, in Debug config, two never-drawn pipelines, two unit-mesh vertex buffers, two recorded indirect draws per frame, and two unconditional `WriteIndirectBuffer` zero-writes per frame in `EndRender`.

## Design

Recommended option: **remove** (git preserves; re-adding later is mechanical). Alternative pre-staged for grill: keep as intentional debug-toolkit completeness.

### Engine/Source/Graphics/Debug/DebugRender.h
- Remove the `DebugRender::Box` and `DebugRender::Sphere` static declarations (`:12-13`) [~2m]

### Engine/Source/Graphics/Debug/DebugRender.cpp
- Remove the `DebugRender::Box` / `DebugRender::Sphere` definitions (`:62-87`), the `kiBox`/`kiSphere` index constants (`:26-27`), and the `"DebugBox"`/`"DebugSphere"` entries of the file-scope `sTypes` table (`:20-21`); renumber `kiCircle`/`kiLine` to 0/1 [~5m]
- If **keep** is chosen instead: optionally dedupe `Sphere`/`Circle` — bodies are identical except the type index (`:76-100`) — into one uniform-scale `AddLayout` wrapper [~5m]

### Engine/Source/Graphics/Managers/PipelineManager.h / PipelineManager.cpp
- Remove `kPipelineDebugBox` / `kPipelineDebugSphere` from the `Pipelines` enum, and their `DebugRenderPipelineEntry` rows in `PipelineManager::CreateDebugRenderPipelines` (`PipelineManager.cpp:812-813`) — each row also drives the `CreateDynamicBuffer` call for its CRC, so the two dynamic storage buffers disappear with the rows [~5m]

### Engine/Source/Graphics/Managers/BufferManager.h / BufferManager.cpp
- Remove the `mDebugBoxVertexBuffer` / `mDebugSphereVertexBuffer` members and their unit-mesh builds inside the `kbDebugRender` block of the debug-mesh creation (`BufferManager.cpp:39-67` box including its comment line, `:69-110` sphere) [~10m]

### Engine/Source/Graphics/Managers/CommandBufferRecordMain.cpp
- Remove the `kPipelineDebugBox` / `kPipelineDebugSphere` `RecordDrawIndirect` calls (`:313-314`) [~2m]

## Critical files

- `Engine/Source/Graphics/Debug/DebugRender.h`
- `Engine/Source/Graphics/Debug/DebugRender.cpp`
- `Engine/Source/Graphics/Managers/PipelineManager.h`
- `Engine/Source/Graphics/Managers/PipelineManager.cpp`
- `Engine/Source/Graphics/Managers/BufferManager.h`
- `Engine/Source/Graphics/Managers/BufferManager.cpp`
- `Engine/Source/Graphics/Managers/CommandBufferRecordMain.cpp`
- `Engine/Source/Graphics/Debug/CLAUDE.md` (drop box/sphere from the primitive list)
- `Engine/Data/Shaders/Debug/CLAUDE.md` (`:5` "boxes, spheres, circles, lines" + "The three shaders back four pipelines — boxes, spheres, and lines share the world-space vertex shader"; `:9` `DebugRender.vert` "for boxes, spheres, and lines" — doc text only, shader files untouched)

## Out of scope

- Shaders: `Debug/DebugRender.vert` + `DebugRender.frag` are shared with `Line` (and `DebugRenderBillboard.vert` is Circle's) — all stay untouched.
- `Circle`/`Line`/`Toggle`/`BeginRender`/`EndRender` behavior.
- The empty-active-set stale-indirect-count issue in `RenderFrameMain` — sibling plan `Graphics/Architecture_EmptyCoordsStaleIndirectCounts.md`.
- vcxproj changes — no files are added or removed.

## Notes

- **Decision pre-staged for `/external-grill-plan`**: remove (recommended) vs keep-for-future-debugging. Root CLAUDE.md's "never remove working features without confirmation" applies — the grill is that confirmation gate.
- Removing enum values shifts subsequent `Pipelines` enumerator values — runtime-only (pipelines and record-once CBs are rebuilt every boot; nothing serializes a `Pipelines` value), so no `kiVersion`/`.pack`/save exposure.
- No CRC/determinism/network exposure; all touched code is client-only and `kbDebugRender`-gated (dead weight only exists in the Debug build config).

## Verification Notes

Independently verified 2026-06-10 (external-deep-analysis verification pass):

- **Zero callers confirmed repo-wide**: the only `DebugRender::Box(`/`DebugRender::Sphere(` matches are the definitions (`DebugRender.cpp:62`/`:76`). `kiBox`/`kiSphere` are file-static (`DebugRender.cpp:26-27`) and referenced only by the two dead functions — no indirect dispatch from outside. No test/tooling references exist.
- **All cited file:line locations verified exact**: `DebugRender.h:12-13`; `DebugRender.cpp:20-21` (`sTypes` rows), `:26-27`, `:62-87` (Box `:62-74`, Sphere `:76-87`); `PipelineManager.h:55-56` (enum values); `PipelineManager.cpp:812-813` inside `CreateDebugRenderPipelines` with the per-row `CreateDynamicBuffer` at `:820`; `BufferManager.h:107-108`; `CommandBufferRecordMain.cpp:313-314`. The Sphere/Circle dedupe range `:76-100` (keep-option) also verified — bodies identical except type index.
- **Corrected**: BufferManager.cpp unit-mesh extents were cited as `:53-95`/`:96-128` (the `.Create(` call lines); the full removable blocks are `:39-67` (box, incl. comment) and `:69-110` (sphere). Fixed in the Design section above.
- **Added missed doc site**: `Engine/Data/Shaders/Debug/CLAUDE.md` (`:5`, `:9`) describes box/sphere and "four pipelines" — added to Critical files. No other CLAUDE.md mentions box/sphere debug primitives beyond the two already listed.
- **Shader sharing confirmed**: box/sphere pipelines consume `data::kShadersDebugDebugRendervertCrc` + `data::kShadersDebugDebugRenderfragCrc`, both still consumed by the Line pipeline (Circle uses `DebugRenderBillboardvertCrc`) — no shader or DataPacker change, "Out of scope: Shaders" stands.
- **Enum-shift safety confirmed**: repo-wide `kPipeline` grep shows `Pipelines` values used only in Graphics runtime code (pipelines/record-once CBs rebuilt every boot); nothing in File/Network/Save serializes a `Pipelines` value. ThirdParty matches are imgui's own examples, unrelated.
- **No plan overlap**: `Engine/DeadCodeAndUnusedIncludesSweep.md` does not cover Box/Sphere; no other live Graphics plan touches these sites.
- Nothing removed from this plan; both options (remove vs keep) remain pre-staged for the grill per root CLAUDE.md's never-remove-without-confirmation rule.
