# IslandTerrain.cpp Client/Shared Split

## Context

`Engine/Source/Frame/IslandTerrain.cpp` is 890 lines and a `/reduce-file` flag from the code review. The file has a clean, already-bracketed seam: a large `#if defined(BT_CLIENT)` GPU-residency span versus the shared CPU elevation/normal queries and template ingest. Splitting along the existing client guard both reduces the file and gives the engine's client/server rules a cleaner home (a fully-`BT_CLIENT` translation unit appears only in the client vcxproj).

**Domains in the current file** (verify line ranges at execution — they drift):

- **Shared CPU domain** (compiles in both builds): the ctor `IslandTerrain::IslandTerrain()` (~`:80-150`, contains a small `#if defined(BT_CLIENT)` channel-CRC-uniqueness assert block at `:113-137` and a `BT_CLIENT`/`BT_SERVER` pair inside `WaitForElevationMaps`), `~IslandTerrain()` (`:152`), `WaitForElevationMaps` (`:157`), `GlobalElevation` (`:207`), `BuildElevationGrid` (`:270`), `FrameElevation` (`:360`), `FrameNormal` (`:392`), and `GlobalNormal` (`:834-855`). These are the elevation/normal queries (sim hot path + render path) and the template ingest. They read `gpIslandTerrain`, `FrameStaticData`, `IslandPlacement`, `IslandChainPlacement`, and the heightmaps — **no GPU types**.
- **Client GPU-residency domain** (fully `#if defined(BT_CLIENT)`): the contiguous span `:410-832` — `CreateClientMeshBuffers` (`:411`, lazy GPU mesh buffer upload), `AcquireTextureSlot` (`:504`, lazy CRC→bindless-slot first-mint), `AnyEvictionPending` (`:621`), `AnyRestorationPending` (`:640`), `EvictionSweep` (`:679`), `RestorationSweep` (`:779`) — plus the second client-only span `:856-888` — `ReleaseGpuResources` (`:857`) and `ResetTextureSlots` (`:874`). These are the only users of `Graphics/Managers/PipelineManager.h`, `Graphics/Managers/TextureManager.h` (the two `BT_CLIENT`-gated includes at `:6-9`), the per-template `Buffer mMeshBuffer` / `Texture mElevationTexture` GPU members, and the LRU slot machinery (`miNextTextureSlot`, `mFreeTextureSlots`). The residency/eviction-symmetry and slot-0-placeholder invariants for this domain live in `Engine/Source/Graphics/Managers/CLAUDE.md`.

**The header `IslandTerrain.h` is shared and is NOT split** — it already `#if defined(BT_CLIENT)`-gates the GPU members (`IslandTemplate::mMeshBuffer`/`mElevationTexture`, the LRU state) and the GPU methods, and is included by both builds. Only the `.cpp` bodies move. This makes the split cleaner than the `NavBuildSplit` sibling: there is no shared-private-header / cross-TU-symbol-promotion step (the shared domain calls no client method, and the client domain reaches shared members through the existing public header).

This is a pure code-organization refactor: **NO behavior change, NO algorithm change, NO serialized-layout/CRC change.** The per-cell elevation grid and NavData are derived (not serialized, not in the CRC); the heightmap/hull/mesh payloads are unchanged; nothing here touches `Frame::kiVersion`, `kiNavDataVersion`, or any `.pack` layout. The determinism contract is unaffected — the split only relocates function bodies; the same code runs in the same order on both sides.

`IslandTerrain.cpp` compiles into **both** the client and server executables today (it is ungated). The new file is client-only, so it goes into the **client** vcxproj + filters only; the kept `IslandTerrain.cpp` stays in both.

## Design

**This plan does NOT execute the split** — it documents intent so a later session runs the `/reduce-file` skill against `IslandTerrain.cpp` with this seam. The skill produces the mechanical move; the notes below constrain its choices.

### Translation units after the split

1. **`IslandTerrain.cpp`** (kept) — owns the public header and the **shared CPU domain**: ctor, dtor, `WaitForElevationMaps`, `GlobalElevation`, `BuildElevationGrid`, `FrameElevation`, `FrameNormal`, `GlobalNormal`. Stays ungated → stays in **both** vcxprojs. The two client-only includes (`PipelineManager.h`, `TextureManager.h` at `:6-9`) move to the new file **only if** nothing in the retained ctor's `BT_CLIENT` channel-CRC assert block needs them (it reads `IslandHeader` channel CRCs from the chunk map — verify it needs neither manager; if it is self-contained on `common::IslandHeader`, the two manager includes move out cleanly). The ctor's small inline `BT_CLIENT` assert block and the `WaitForElevationMaps` `BT_CLIENT`/`BT_SERVER` pair **stay in this file** (they are interleaved with shared logic in a single function body — do not hoist them into the client TU; that would fragment two functions across TUs for no benefit).

2. **`IslandTerrainResidency.cpp`** (new, `#if defined(BT_CLIENT)`-wrapped in full) — the **client GPU-residency domain**: `CreateClientMeshBuffers`, `AcquireTextureSlot`, `AnyEvictionPending`, `AnyRestorationPending`, `EvictionSweep`, `RestorationSweep`, `ReleaseGpuResources`, `ResetTextureSlots`. Includes `IslandTerrain.h` (public types + the GPU-method decls), `Graphics/Managers/PipelineManager.h`, `Graphics/Managers/TextureManager.h`, and whatever else those eight bodies reference (the chunk map / `Data.h`, `Game.h` — confirm by reading the moved bodies). Name `IslandTerrainResidency.cpp` echoes the domain (GPU residency / LRU eviction); trivial choice — `IslandTerrainGpu.cpp` is an acceptable swap at execution.

No private `*Internal.h` is needed: unlike `NavBuildSplit`, the two domains share no file-local helper that must cross the TU boundary — the client domain reaches shared state (`mIslands`, `mIslandCrcsSorted`, `mfSeaFloorElevation`, the per-template fields) through the unchanged public header, and the shared domain never calls a client method. Confirm at execution that no file-local `static`/anonymous-namespace helper in the `:410-888` span is also called from the shared `:80-408` span (a grep for each moved file-local symbol); if one is, promote it like `NavBuildSplit` does. (Expected: none — the GPU helpers are self-contained.)

### Build wiring (client only — the key difference from NavBuildSplit, which wires both)

`IslandTerrain.cpp`/`.h` currently appear in **all four** project files (client + server vcxproj and both `.filters`). The new **client-only** `IslandTerrainResidency.cpp` is added to **two** files only — the client vcxproj and its filters — next to the existing `IslandTerrain.cpp` entry, in the same `Engine\Frame` filter:

- `Projects/BrokenEngineSandbox/Platforms/VisualStudio2026/BrokenEngineSandbox.vcxproj` — add a `<ClCompile>` for `IslandTerrainResidency.cpp` beside the existing `IslandTerrain.cpp` entry.
- `Projects/BrokenEngineSandbox/Platforms/VisualStudio2026/BrokenEngineSandbox.vcxproj.filters` — add the matching `<ClCompile>` under the `Engine\Frame` filter.
- **Do NOT** add it to `BrokenEngineSandboxServer.vcxproj` / `.filters` — the file is fully `BT_CLIENT`-wrapped, so per [VisualStudio2026/CLAUDE.md](../../../Projects/BrokenEngineSandbox/Platforms/VisualStudio2026/CLAUDE.md) (a `BT_CLIENT`-only `.cpp` belongs only to the client project; do not compile an empty TU in the server) it must not appear in the server project. `IslandTerrain.cpp` itself stays in all four (still ungated).

The new file follows the `*Render.cpp`-style client-affinity naming signal loosely (it is not a render file, but it is client-only); keep it `#if defined(BT_CLIENT)`-wrapped in full so the affinity is unambiguous.

## Critical files

- `Engine/Source/Frame/IslandTerrain.cpp` — source of the split. Keeps the shared CPU domain (ctor/dtor, `WaitForElevationMaps`, `GlobalElevation`, `BuildElevationGrid`, `FrameElevation`, `FrameNormal`, `GlobalNormal`); loses the eight client GPU-residency bodies; loses the two `BT_CLIENT` manager includes (if verified unused by the retained ctor assert block).
- `Engine/Source/Frame/IslandTerrainResidency.cpp` (new, `BT_CLIENT`-only) — receives `CreateClientMeshBuffers`, `AcquireTextureSlot`, `AnyEvictionPending`, `AnyRestorationPending`, `EvictionSweep`, `RestorationSweep`, `ReleaseGpuResources`, `ResetTextureSlots`; gains the `PipelineManager.h`/`TextureManager.h` includes.
- `Engine/Source/Frame/IslandTerrain.h` — public header; **unchanged**. All public decls (shared queries + the `#if defined(BT_CLIENT)` GPU-method block + `IslandTemplate` with its gated GPU members) stay; only the definitions of the eight client methods move TU. Verify no edit is needed.
- `Projects/BrokenEngineSandbox/Platforms/VisualStudio2026/BrokenEngineSandbox.vcxproj` and `BrokenEngineSandbox.vcxproj.filters` — add the new client-only file under `Engine\Frame`. (Server vcxproj/filters: **no change** — confirm `IslandTerrainResidency.cpp` is absent from both.)
- `Engine/Source/Frame/CLAUDE.md` — the **IslandTerrain** bullet (and the "client side additionally owns lazy CRC-to-bindless-slot…" sentence) describes the GPU-residency responsibilities; note the TU split (shared queries in `IslandTerrain.cpp`, client GPU residency in `IslandTerrainResidency.cpp`) and that no version/CRC is affected. Update via `update-claude-docs` in the code-change process. The residency invariants in `Engine/Source/Graphics/Managers/CLAUDE.md` still describe the same code — refresh only if it cites the old file name.

## Out of scope

- **Any behavior, algorithm, or numeric change** — no elevation-sample tweak, no eviction-policy change, no slot-assignment change. Byte-for-byte identical sim grids, NavData, and GPU residency behavior.
- **Splitting `IslandTerrain.h`** — the header is shared and stays one file (its `#if defined(BT_CLIENT)` member/method gating already keeps GPU types out of the server build). Do not introduce a second header.
- **`Frame::kiVersion` / `kiNavDataVersion` / any `.pack` layout bump** — nothing serialized changes; a version bump here would be a bug.
- **Executing the `/reduce-file` split** — this plan only records intent and the seam; the move runs the skill at execution time in a later session.
- **The `kfMetersToUnits` multiplications** at `IslandTerrain.cpp` (`:49`/`:50`/`:147`/`:395`/`:818`) — owned by `Frame/ScaleEngineToMeters.md`; do not touch them here (note: `:818` sits inside the span this split *relocates* — coordination below).
- **The `[DEBUG-resmem]` instrumentation / per-template mesh-buffer LRU extension / `mNavContour` lifecycle** — owned by `Graphics/IslandResidentMemoryScalingStrategy.md` and `Graphics/IslandNavContourResidency.md`; this split only *relocates* the eviction/restoration sweeps those plans later edit (coordination below).
- **Any further Frame-file split** (e.g. `FrameTick.cpp`, `Collision.cpp`).

## Acceptance criteria

- `IslandTerrain.cpp` and the new `IslandTerrainResidency.cpp` compile; the **client** executable links (the eight relocated client methods resolve via the unchanged header decls). The **server** executable compiles `IslandTerrain.cpp` unchanged and does **not** see `IslandTerrainResidency.cpp`.
- `IslandTerrain.h` is unchanged (or changed only if an include/forward-decl gap surfaces at execution).
- A diff of each relocated method body against the pre-split source shows ZERO logic changes (move-only).
- `IslandTerrainResidency.cpp` appears in the client vcxproj + filters under `Engine\Frame` and is **absent** from the server vcxproj + filters; it is fully `#if defined(BT_CLIENT)`-wrapped.
- `IslandTerrain.cpp` remains in all four project files (still ungated, both builds).
- No `Frame::kiVersion` / `kiNavDataVersion` / `.pack`-layout change.
- `Engine/Source/Frame/CLAUDE.md` IslandTerrain bullet reflects the TU split.

## Notes

- **Precedent:** mirror the landed `NavBuildSplit` mechanics (kept TU keeps the public header + primary domain; new TU takes the other domain; file-local helpers move with their domain), but **drop** its private-`*Internal.h` + cross-TU-symbol-promotion step — there is no shared file-local helper here. The other structural difference: NavBuild is ungated (wires all four projects); this file's new TU is client-only (wires two).
- **Why client-only for the new file, not both:** the relocated span is entirely `#if defined(BT_CLIENT)` and depends on `PipelineManager`/`TextureManager`/`Buffer`/`Texture` — none of which exist in the server build (the server vcxproj includes no `Graphics/` `.cpp`). Putting it in the server project would compile an empty TU and pull graphics headers into a headless build.

### Coordination (existing File-Group concern)

`IslandTerrain.cpp` is already an `Order.md` File-Group shared by four queued plans (`Frame/ScaleEngineToMeters.md`, `Graphics/IslandResidentMemoryScalingStrategy.md`, `Graphics/IslandNavContourResidency.md`, `Engine/SingletonPublishGlobalGuardSweep.md`). **A file split invalidates those plans' line references**, so this split must be sequenced or co-scheduled with them:

- **`Graphics/IslandResidentMemoryScalingStrategy.md`** extends `EvictionSweep`/`RestorationSweep` and removes the `[DEBUG-resmem]` block in `CreateClientMeshBuffers` — **all three relocated by this split into `IslandTerrainResidency.cpp`.** If this split lands first, that plan must re-target the new file. **Recommended: land this split BEFORE (or in the same session as) `IslandResidentMemoryScalingStrategy.md`**, or have that plan re-point at execution. This ordering constraint is recorded in `## Dependencies`.
- **`Graphics/IslandNavContourResidency.md`** edits `WaitForElevationMaps` / `~IslandTerrain` — both **stay in `IslandTerrain.cpp`** (shared domain), so this split does not move its edit sites; only a line-number refresh is needed.
- **`Frame/ScaleEngineToMeters.md`** touches `:49/:50/:147` (stay in `IslandTerrain.cpp`) and `:395/:818` — `:818` is inside the relocated client span (it would move to `IslandTerrainResidency.cpp`). A line/file refresh is needed if this split lands first; not a hard ordering constraint (mechanical multiply removal, re-targetable).
- **`Engine/SingletonPublishGlobalGuardSweep.md`** edits the ctor/dtor (`:80`-ish / `:152`) — both **stay in `IslandTerrain.cpp`**; only a line refresh is needed.

Add this plan to that File-Group entry so the coordination is visible at scheduling time.
</content>
