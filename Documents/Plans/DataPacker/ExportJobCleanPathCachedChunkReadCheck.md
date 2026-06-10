# ExportJob::RunExport: Check the Clean-Path Cached-Chunk Read

## Context

`ExportJob::RunExport` (`DataPacker/Source/ExportJobs/ExportJob.cpp:131-182`) has a fast clean path: when
`CheckDirty` decided the chunk is up to date (`!mbDirty`), it streams the cached `.chunk` payload straight back into the
`.pack` instead of re-running `Export()` (`:137-146`):

```cpp
if (!mbDirty)
{
    int64_t iChunkFileSize = std::filesystem::file_size(mChunkFile);
    int64_t iHeaderAndDataSize = iChunkFileSize - sizeof(kiMagic) - sizeof(int64_t);
    mHeaderAndData.resize(iHeaderAndDataSize);

    std::fstream fileStream(mChunkFile, std::ios::in | std::ios::binary);
    fileStream.seekg(sizeof(kiMagic) + sizeof(int64_t)); // Skip magic and version
    fileStream.read(reinterpret_cast<char*>(mHeaderAndData.data()), mHeaderAndData.size());
    return mHeaderAndData;
}
```

The `read()` at `:145` has no stream-state check before the bytes are returned and baked into the `.pack`. This session
hardened the *sentinel* reads in `CheckDirty` (`:90`, `:110`) that gate the dirty decision, but this clean-path
**payload** read was left untouched.

Risk is TOCTOU-only and low: `CheckDirty` validated the chunk file's magic and version moments earlier (`:84-95`), and
the byte count comes from `std::filesystem::file_size` (`:139`), so a same-run truncation/deletion between the dirty
check and this read is the only way it fails. Failure mode: a short/failed read leaves the tail of `mHeaderAndData`
holding the resize-zeroed bytes (`:141` `resize` value-initializes), and those silently-zeroed chunk bytes get written
into the `.pack` (`BakeRoute.cpp` / the pack assembly downstream consume `RunExport`'s return). DataPacker is an offline
tool, so this never affects the shipped game at runtime — only a (rare, racy) bad bake that would surface as a corrupt
asset at next engine load.

## Design

Add a stream-state check after the payload `read()` at `:145`, before returning `mHeaderAndData`. Two candidate shapes —
pick one in the grill:

- **(A) ASSERT** `ASSERT(fileStream)` (or `ASSERT(fileStream.gcount() == mHeaderAndData.size())`) after the read. Matches
  the offline-tool convention that a corrupt/racing intermediate is a hard, must-investigate error — the bake should
  fail loud, not silently emit a zeroed chunk. Simplest; consistent with the `ASSERT`s already in this file
  (`:168` `ASSERT(relativeFileString.length() < MAX_PATH)`).
- **(B) Dirty-fallback** on a failed read: set `mbDirty = true` and fall through to the `Export()` path (regenerate the
  chunk from source instead of trusting the cache). More resilient, but heavier — it duplicates/relocates control flow,
  and the failure it recovers from (a chunk file vanishing mid-run) is itself a sign something is wrong that an ASSERT
  would surface for diagnosis.

Recommendation: **(A) ASSERT**, matching the file's existing fail-loud posture and the offline-tool context (no need to
silently recover from a racing filesystem during a build). The check is also a guard against `iHeaderAndDataSize` going
negative if a future change ever let a sub-header-sized file reach here (`file_size` < 16 → negative resize arg →
`length_error`); an `ASSERT(iHeaderAndDataSize >= 0)` before the `resize` at `:141` is a cheap fold-in if (A) is chosen.

## Out of scope

- The `CheckDirty` sentinel reads (`:90`, `:110`) and `BakeRoute.cpp` `IsGaeaRawDirty`/`AreLeavesDirty` /
  `TextureCache.cpp` reads — all hardened this session.
- The dirty-path write-back (`:171-179`) — it is a `write`, not a trust-boundary read.
- Re-validating the chunk's magic/version here — `CheckDirty` already did (`:84-95`); re-reading them would duplicate
  that check. This plan only guards the payload read's stream state.
- Any change to the `.chunk` format, `GetVersion()`, or `ChunkHeader` layout.
- Adding error handling to the many other DataPacker file reads — scope is this one clean-path read.

## Acceptance criteria

- A failed/short payload read on the clean path no longer silently returns zeroed chunk bytes into the `.pack`:
  it either ASSERTs (shape A) or regenerates from source (shape B), per the grill decision.
- A normal clean run (chunk present and complete) still streams the cached chunk back unchanged — no behavior change on
  the common path.

## Critical files

- `DataPacker/Source/ExportJobs/ExportJob.cpp` — `ExportJob::RunExport` clean-path block (`:137-146`); the
  `fileStream.read` at `:145` is the change site.

## Notes

- Offline DataPacker only — no runtime, CRC-replay, network, or determinism exposure.
- Quick Win: a single added check (plus an optional `iHeaderAndDataSize >= 0` ASSERT). One design question (ASSERT vs
  dirty-fallback) is the only thing keeping it from being a pure mechanical one-liner.
- Sibling of the `CheckDirty` sentinel-guard work that landed this session — same file, same trust-boundary-read theme,
  but the payload read rather than the dirty-decision sentinels.
