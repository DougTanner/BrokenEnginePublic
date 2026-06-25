# AnimationData Chunk Byte-Extent Bound (Pattern B)

**Decision plan (present options).**

## Context

This session landed trust-boundary validation in `AnimationData::Load` (`Engine/Source/Graphics/AnimationData.cpp`). Two validation patterns were attempted:

- **Pattern A — secondary-index validation (LANDED).** `Load` now bounds every on-disk index against its target-array maximum: header counts vs structural maxima (`Skeleton::kiMaxNodes`, `kiMaxSkinJoints`, `AnimationHeader::kiMaxAnimations`, `SceneHeader::kiMaxMaterials`, `kiMaxDeserializedCapacity` for channels/keyframes), then the secondary indices (skin joint→node, `MaterialInfo::iParentNodeIndex`, channel `uiNodeIndex` + per-channel keyframe range, clip channel range). This closes the OOB-**write** class — a corrupt count or index can no longer overrun the fixed-size member arrays (`mBindPoseLocalMatrices[kiMaxNodes]`, `mbAnimatedNodes`, etc.) or write joints/nodes out of range.

- **Pattern B — chunk-byte extent bound (ATTEMPTED, REVERTED — this plan).** A count that is `<=` its structural maximum but still exceeds the chunk's *actual* on-disk bytes is not caught by Pattern A. Such a count walks the `reinterpret_cast` alias-pointer advances (`AnimationData.cpp:37-59`) and the validation loops (`:66-100`) off the end of the eagerly-loaded pack buffer — an OOB **read** of unrelated pack memory (and a `throw` or garbage parse, not a crash, in most cases, but still reads past the chunk). Closing it requires bounding each pointer advance against the animation region's true byte extent. That extent is **not currently available** at the `Load` trust boundary, so the byte-bound was reverted; the residual gap is documented in the `Load` comment (`AnimationData.cpp:20-22`) and in `Graphics/CLAUDE.md` (AnimationData → Trust-boundary validation). **This plan finishes Pattern B.**

### Why the byte extent is unavailable today (verified against source)

- `AnimationData::Load(const std::byte* pAnimationData, common::crc_t crc)` receives only a base pointer — no length.
- The obvious source `rChunk.pHeader->iSize` is **wrong for scene chunks**. The DataPacker sets the scene chunk's `iSize` to `iSceneArraysSize + uiMaterialCount * sizeof(MaterialShaderData)` (animation **excluded**) in `AllocateHeaderAndData` (`DataPacker/Source/ExportJobs/ExportScene.cpp:547`), then **grows** `mHeaderAndData` to append the animation section **without updating `iSize`** (`ExportScene.cpp:766`). Documented at `DataPacker/Source/ExportJobs/CLAUDE.md:11` ("the scene chunk's `iSize` excludes it"). So `iAnimationBytes = iSize - iSceneArraysSize - iMaterialDataSize <= 0`, which would throw for **every** animated scene at boot. (That bug was caught in review and reverted.)
- The runtime `EagerChunk` (`Engine/Source/File/FileManager.h:21-25`) retains only `{common::ChunkHeader* pHeader, std::byte* pData}` — no in-memory data length. The **true** in-memory data extent (which **does** include the appended animation bytes, since the `.pack` stores the full `mHeaderAndData`) is `ChunkLocation::uiSize - common::kiChunkDataOffset`. It is computed at eager-load time (the eager site is `FileManager.cpp:419-421`; the same `uiSize - kiChunkDataOffset` math appears for lazy chunks at `:314`/`:558`) and then **discarded** — only `pHeader` and `pData` survive into the map.

The caller `LoadAnimationDataFromEagerChunks` (`AnimationData.cpp:442-476`) computes `iSceneArraysSize` and the **rounded** `iMaterialDataSize` from the scene header to locate the animation base pointer (`rChunk.pData + iSceneArraysSize + iMaterialDataSize`). Any byte-extent solution must keep that pointer math consistent (the reader uses `RoundUp<…, kiAlignmentBytes>` for `iMaterialDataSize`; the DataPacker mirrors it at `ExportScene.cpp:763`).

## Design

The fix needs the animation region's true byte size at the `Load` trust boundary so each pointer advance can be bounded (reject when the running offset would exceed the region). Three options, in increasing-to-decreasing invasiveness:

### Option A — DataPacker: make the scene chunk's `iSize` include the appended animation section

Update `ExportScene.cpp` so the scene chunk's `ChunkHeader::iSize` covers the appended animation bytes (set/grow `iSize` after the `mHeaderAndData.resize` at `:766`, or compute the full size up front). Then the reader's `iSize - iSceneArraysSize - iMaterialDataSize` *is* the true animation region size, and `Load` can take the byte bound.

- **Pros:** `iSize` becomes truthful (it currently lies for scene chunks — the `ExportJobs/CLAUDE.md:11` exception goes away); no new field; the existing reader math already subtracts `iSceneArraysSize + iMaterialDataSize`.
- **Cons / implications:**
  - Changes `.pack` byte semantics for scene chunks → **`DataHeader::kiVersion` / chunk-cache version bump + full re-export** (the DataPacker manifest-version check dirties the whole type on a `kiVersion` mismatch — see `DataPacker/Source/CLAUDE.md`).
  - `LoadAnimationDataFromEagerChunks`'s data-pointer math must stay consistent — it uses the **rounded** `iMaterialDataSize` (`RoundUp<…, kiAlignmentBytes>`), and the animation base = `pData + iSceneArraysSize + iMaterialDataSize`; that stays unchanged, but the *new* `iSize` must agree with `iSceneArraysSize (rounded) + iMaterialDataSize (rounded) + iAnimDataSize` so the subtraction yields the right region size.
  - The just-landed `ModelPipeline::Create` byte-bound (`ModelPipeline.cpp:59`) currently relies on `iSize` **excluding** animation: it checks `iSceneArraysSize + uiMaterialCount*sizeof(MaterialShaderData) <= iSize`. Growing `iSize` to include animation **loosens** that check (the threshold rises, so a corrupt material-extent overrun could slip past it into the animation bytes). Still memory-safe (the bytes are resident), but the check stops being tight — note and decide whether to re-tighten it to `iSceneArraysSize + iMaterialDataSize` explicitly.

### Option B — add a total-animation-byte (or end-offset) field to `common::AnimationHeader`

Add a field to `common::AnimationHeader` (`Common/DataFile.h`) — e.g. `uint64_t uiAnimationBytes` (or an end offset) — written by the DataPacker (`ExportScene.cpp` `WriteAnimationSection`, which already computes `iAnimDataSize` at `:751-759`), read by `Load` and used as the byte bound.

- **Pros:** explicit, self-describing; `Load` reads the bound from the header it already `memcpy`s first (`AnimationData.cpp:11`); no reliance on `iSize` semantics; `ModelPipeline`'s check is untouched.
- **Cons:** format change → `kiVersion` bump + full re-export; `AnimationHeader` is layout-locked by a `static_assert` (`DataFile.h`) and folded into the Scene job version `sizeof` — both must move with the new field; small reader change. The bound must be validated as plausible too (a corrupt `uiAnimationBytes` is itself untrusted, so cross-check it against `ChunkLocation::uiSize`-derived extent — which loops back to needing Option C's length anyway, unless the cross-check is dropped and the field is trusted as the sole bound, which is weaker).

### Option C — retain the in-memory data length on `EagerChunk` (RECOMMENDED)

Engine-side only, **no `.pack` format change**:

1. Add a length field to `EagerChunk` (`FileManager.h:21-25`) — e.g. `int64_t iDataSize`.
2. Populate it at the eager-load site (`FileManager.cpp:419-421`) from `rChunkLocation.uiSize - common::kiChunkDataOffset` — the value is already in hand there (`uiDataOffset` is derived from `uiOffset + kiChunkDataOffset`; the size analogue is computed identically for lazy chunks at `:314`/`:558`).
3. Re-introduce the byte length on `AnimationData::Load` — `Load(const std::byte* pData, int64_t iAnimationBytes, common::crc_t crc)` — and have `LoadAnimationDataFromEagerChunks` pass `rChunk.iDataSize - iSceneArraysSize - iMaterialDataSize` (using the same rounded `iSceneArraysSize`/`iMaterialDataSize` it already computes to locate the base pointer). This is the **true** animation region size, because `iDataSize` (from `ChunkLocation::uiSize`) includes the appended animation bytes.
4. In `Load`, bound each `reinterpret_cast` pointer advance against `iAnimationBytes` (maintain a running offset; reject — `throw common::CorruptStreamException` — if any section's `[start, start+count*stride)` exceeds `iAnimationBytes`), closing the OOB-read class.

- **Pros:** **no `.pack`/`kiVersion`/re-export** — least invasive to ship; `iDataSize` is genuinely useful (the discarded-extent is a latent gap noted by the File deep-analysis); `ModelPipeline`'s check is untouched; the new bound is derived from authoritative chunk-table data (`ChunkLocation::uiSize`), not from a header field that is itself untrusted.
- **Cons:** touches the **File subsystem** (`FileManager.h` adds a member, `FileManager.cpp` populates it) and re-adds a parameter to the `Load` signature (every `Load` caller updates — currently just `LoadAnimationDataFromEagerChunks`). Crosses subsystem boundaries (Graphics + File), so coordinate with the queued File plans (`File/Architecture_LoadThreadLifecycleSafety.md`, `File/Architecture_FileManagerSplitDecision.md`) and the File-subsystem `## Dependencies`/`## File Groups` entries that reference `EagerChunk`/`GetEagerChunkMap`.

**Recommendation: Option C.** It is the only option with no `.pack`/version/re-export cost, derives the bound from authoritative data, and leaves the already-correct `ModelPipeline` byte-bound and the `AnimationHeader` layout untouched. Its single drawback is that it reaches into the File subsystem — flag the `EagerChunk`/`Load`-signature change to any in-flight File plan.

## Critical files

- `Engine/Source/Graphics/AnimationData.cpp` — `AnimationData::Load` (re-add byte bound + per-advance checks); `LoadAnimationDataFromEagerChunks` (pass the region length).
- `Engine/Source/Graphics/AnimationData.h` — `Load` signature (Option C).
- `Engine/Source/File/FileManager.h` — `EagerChunk` struct (Option C: add `iDataSize`).
- `Engine/Source/File/FileManager.cpp` — eager-load site `mEagerChunkMap.try_emplace` (~`:419-421`) (Option C: populate `iDataSize`).
- `DataPacker/Source/ExportJobs/ExportScene.cpp` — `AllocateHeaderAndData` `iSize` set (`:547`) and the animation-grow (`:766`) (Options A/B only).
- `Common/DataFile.h` — `AnimationHeader` (Option B only: new field + layout `static_assert` + Scene-job `sizeof` fold).
- `Engine/Source/Graphics/Objects/ModelPipeline.cpp` — `:59` byte-bound (Option A only: re-tighten if `iSize` grows).
- `Engine/Source/Graphics/CLAUDE.md` — AnimationData "Chunk byte-extent is intentionally not bounded" note (update once Pattern B lands).
- `DataPacker/Source/ExportJobs/CLAUDE.md` — `:11` "scene chunk's `iSize` excludes it" exception (update for Option A).

## Out of scope

- **Pattern A secondary-index validation** — already landed in `AnimationData::Load` this session (header-count maxima + skin-joint / `iParentNodeIndex` / channel-node + keyframe-range / clip-channel-range bounds). This plan does **not** revisit it.
- **`ModelPipeline` / `PipelineDescriptorWriter` / `PipelineManager` byte-bounds** — already landed and correct (`ModelPipeline.cpp:59`, `PipelineManager.cpp:62`). Their chunks do **not** have the appended-section problem (their `iSize` covers their full payload), so their byte-bounds are tight as-is. Option A's only interaction with `ModelPipeline` is the loosening note above.
- **`IslandTerrain`'s existing byte-bound** — already blocks OOB on the island chunk path; not touched.
- Any broader `EagerChunk`/`FileManager` redesign (the File split decision is its own plan).

## Notes

- **Decision plan (present options).** Resolve A vs B vs C via `/external-grill-plan` before any edit. Recommendation is C.
- **Invariant exposure:** client-only `.pack` chunk parse (`AnimationData` is in `Engine.h`'s `BT_CLIENT` span, client-vcxproj-only). Behavior changes **only for corrupt/tampered scene chunks** — well-formed assets parse identically. The OOB-**write** class is already closed by the landed Pattern-A validation; this plan closes only the residual **oversized-count OOB-read** class (lower severity).
  - **Options A / B touch `.pack` layout** → `DataHeader::kiVersion` / Scene chunk-cache version bump + full DataPacker re-export; no shared-CRC/replay/determinism path is involved (these are asset-path-hash CRCs and chunk bytes, not the runtime desync CRC).
  - **Option C is engine-only** — no `.pack`/version/re-export; re-adds the `int64_t iAnimationBytes` parameter to `AnimationData::Load` and an `int64_t iDataSize` member to `EagerChunk`; both compile-checked.
- **Effort grows if A/B is chosen** (DataPacker + version bump + re-export) vs C (narrow engine-side reader change + one File-struct field). Scored for the recommended Option C.
