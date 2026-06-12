# Architecture: Pack Staleness and CRC Collision Guards

## Context
Source: /external-architecture-review on `DataPacker/Source/`. `RunExportJobs<T>` (`Main.cpp:340-474`) has two silent-staleness/ambiguity gaps:

1. **Deleted source assets never dirty the pack.** `bDirty` is set only by the clean flag, missing output files, or per-*existing*-file `CheckDirty` (`Main.cpp:343-372`). Removing a source asset shrinks the job list but dirties nothing, so the stale chunk persists in `.pack`/`.manifest` and its constant persists in the generated CRC header until an unrelated edit dirties that type.
2. **No duplicate-CRC detection.** `mRelativeFile`/`mCrc` are derived relative to whichever input root matched (`ExportJob.cpp:12-34`), so the same relative path under `Engine/Data` and the project `Data` dir produces two jobs with identical CRCs: two manifest entries with the same CRC (ambiguous runtime lookup, silent) and two identically named `inline constexpr` constants in the generated header (compile error downstream only in that case).

## Design

### DataPacker/Source/Main.cpp — `RunExportJobs<T>`
- **Chunk-count staleness check**: after the job-collection loop (`Main.cpp:360-372`), when the manifest exists, read its `common::DataHeader` and add `bDirty |= (dataHeader.iChunkCount != static_cast<int64_t>(exportJobs.size()))`. Validate magic/version on the read (opaque file input) and treat a short/invalid read as dirty. This is the cheap detector for the deleted-asset case (renames already self-heal: the new file is dirty and a dirty run rewrites the whole pack from the current job list; same-count delete+add is likewise caught by the added file's dirty). [~15m]
- **Duplicate collision ASSERT**: after the sort at `Main.cpp:383`, walk adjacent pairs and ASSERT no two jobs share a `mRelativeFile` (case-folded) or `mCrc` — closing the two-input-roots collision silently corrupting manifest lookups. Case-folded duplicates sort adjacent under the existing `common::ToLower` comparator, so the adjacent-pair walk sees every collision. This also guarantees all lowered sort keys are unique, making the comparator's ordering fully deterministic — no separate tie-break needed. [~10m]

## Critical files
- `DataPacker/Source/Main.cpp`

## Out of scope
- `CheckDirty` internals (`ExportJobs/ExportJob.cpp`) — the per-file dirty rules are correct; only the aggregate-level gaps are in scope.
- Any `.pack`/`.manifest` layout change — `DataHeader`/`ChunkLocation` formats untouched; the manifest header read is read-only.
- Cross-run pack byte-stability work — chunk ordering is already decoupled from filesystem-enumeration and job-completion order by the sort + ordered future harvest (`Main.cpp:383/415-437`), and the collision ASSERT removes the only unspecified-order case (equal lowered keys), verified clean.
- Deduplicating colliding assets automatically (picking one root over the other) — the ASSERT makes the collision loud; resolution policy is a user decision if it ever fires.

## Notes
- Offline DataPacker only, but output-affecting: the chunk-count check forces a one-time re-export wherever counts already drifted (that is the fix working). No runtime CRC/replay exposure.
- Worst regression case is a spurious extra re-export (over-dirtying), never a missed one.

## Verification Notes
- Both gaps verified against source. `bDirty` sources confirmed exhaustive (`Main.cpp:343/348/353/358/369`); `ExportJob::CheckDirty` (`ExportJob.cpp:48-129`) and the `ExportShader`/`ExportIsland` overrides only ever check *existing* inputs — nothing detects a removed asset. `common::DataHeader` carries `kiMagic`/`kiVersion`/`iChunkCount` (`Common/DataFile.h:347-361`), exactly what the count check needs (`iChunkCount` is `int64_t`; cast the `size()` comparison).
- Identical-CRC claim verified: `ExportJob.cpp:12-34` strips whichever input root matched and sets `mCrc = common::Crc(mRelativeFile)`, so equal relative paths across the two roots produce byte-identical `mRelativeFile` and identical `mCrc`.
- **Original third item (sort tie-break on raw `mRelativeFile`) removed**: once the ASSERT rejects case-folded duplicate paths, the lowered sort keys are provably unique and `std::sort` never compares tied keys — the tie-break would be unreachable code. One mechanism covers both the ambiguity and the ordering instability.
- **ASSERT is safe against current assets**: the two data roots share exactly two relative paths today (`LICENSE.md`, `Shaders\CLAUDE.md`), and neither is claimed by any exporter's `Handles` (`ExportRaw` requires a `Raw/` path component; `ExportShader` claims `.comp/.frag/.vert`; the rest are extension/directory-gated) — so the ASSERT does not fire on the existing asset tree.
- Limitation (acceptable): the adjacent-pair walk catches path-identical and case-folded collisions, not a hypothetical 64-bit `Crc` collision between two *different* relative paths — astronomically unlikely and not the bug class this closes.