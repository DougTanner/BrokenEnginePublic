# Reclaim Dead Eager Pack Buffers After Boot

Surfaced by the `Graphics/IslandMeshCpuSliceReclaim.md` `/next-plan` Step-6 sweep (extension-review gate verdict: **Surface**, low confidence). Different storage mechanism from the lazy-pool decommit plans — the mesh plan's API cannot touch it. **Speculative — measure first.**

## Context

Eager pack chunks live in `std::vector<std::byte> mPackFileData[data::kDataTypeCount]` (`PackChunks.h:61`), a **heap allocation**, NOT the `mpLazyPool` `VirtualAlloc` region. The whole `.pack` per type is `resize`d and read whole (`PackChunks.cpp:322-326`); every eager chunk pointer aliases into it via `mEagerChunkMap` (`&rPackBytes[uiDataOffset]`, `PackChunks.cpp:338`). The surviving reclaim candidates read their chunk once at boot and re-read only on device-loss / settings recreation — the same "dead after boot except recovery" shape as the island mesh:
- **kModel** → GPU vertex/index buffer (`Graphics/Managers/BufferManager.cpp:42-64`).
- **kShader** → pipeline create (`ModelPipeline.cpp`, `DynamicPipelines.cpp`, `PipelineManager.cpp`, `Pipeline.cpp`, `PipelineDescriptorWriter.cpp`).

Because each per-type pack is one buffer aliased by every chunk of that type, reclaim is a **whole-vector free + wholesale re-read + `mEagerChunkMap` alias rebuild**, not a sub-range decommit — categorically larger than the lazy-pool plans, hence its own plan.

## Design

Per-type, opt-in reclaim of the eager pack vector after all boot consumers have finished, with a device-loss/settings-recreation reload that re-reads the pack from disk and rebuilds the `mEagerChunkMap` aliases for that type. Because the whole vector is freed, this is only safe for types with **no runtime aliasing readers**.

**Hard safety constraint — kScene must NEVER be reclaimed:** `AnimationData` (`Graphics/AnimationData.cpp`) indexes the kScene eager pack **zero-copy and reads it every frame for skinning**; its map outlives Graphics (documented in `Graphics/AGENTS.md` AnimationData §). Any reclaim must be gated per data type with kScene excluded.

Steps (per reclaimable type — kModel/kShader only, after profiling proves the win):
- Add a per-type "eager consumers complete" signal (boot upload / pipeline-create done), coordinated with the existing async eager-load / `mbEagerLoadComplete` gating.
- Free `mPackFileData[type]` and clear its `mEagerChunkMap` aliases.
- On device-loss/settings recreation, re-read the pack for that type from disk and rebuild the aliases before the recreate path re-consumes them.

## Critical files
- `Engine/Source/File/PackChunks.{h,cpp}` — `mPackFileData` (`PackChunks.h:61`), eager load and `mEagerChunkMap` alias build (`PackChunks.cpp:322-351`), `GetEagerChunkMap`.
- `Engine/Source/Graphics/Managers/BufferManager.cpp` — kModel upload (`:42-64`).
- `Engine/Source/Graphics/AnimationData.cpp` — kScene zero-copy runtime reader (**the exclusion**).
- `Engine/Source/File/AGENTS.md` — eager/lazy storage docs.

## Out of scope
- kScene reclaim — forbidden (runtime zero-copy aliasing).
- Lazy-pool sub-range decommit (`Graphics/IslandMeshCpuSliceReclaim.md`, `Graphics/TextureChunkCpuPoolReclaim.md`).

## Notes
- **Profile first** — with only the model and shader packs remaining as candidates, confidence is lower that their resident size justifies whole-vector-free + alias-rebuild + reload machinery; this may resolve as "accept + document."
- Both builds load eager packs; the kModel/kShader consumers are client-side, but the vectors themselves are shared FileManager storage.
- **No determinism/CRC/`.pack`/`kiVersion` exposure** — these bytes are consumed into GPU resources, not sim state; reclaim changes residency and recovery handling, not packed content or layout.
- Independent of the lazy-pool plans (different mechanism) — no dependency on `IslandMeshCpuSliceReclaim.md`.
