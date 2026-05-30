# ExportJob::CheckDirty: Guard the Unchecked On-Disk Sentinel Reads

## Context

This is philosophy parity with the just-landed `ExportScene` `.PreExport` fix. That change made the
version-marker reads explicit about read success: `ExportScene::CheckDirty`
(`DataPacker/Source/ExportJobs/ExportScene.cpp:78`) and `ExportScene::Export`
(`:222`) now gate on `if (!fileStreamIn || iStoredVersion != GetVersion())`, so a failed/short read of
the marker forces regeneration instead of trusting whatever bytes landed in the destination buffer
(`Common/FileUtils.h`'s `GetFileOrStringContent` was hardened in the same session to verify a full
read).

The session audit flagged the sibling pattern in the base class `ExportJob::CheckDirty`
(`DataPacker/Source/ExportJobs/ExportJob.cpp:48`), which reads two on-disk sentinels with no
read-success check before comparing the loaded value:

1. The magic + version record. `std::fstream chunkFileStream` is opened at
   `ExportJob.cpp:84`; `chunkFileStream.read(reinterpret_cast<char*>(piMagicAndVersion), sizeof(piMagicAndVersion))`
   at `:87`; the loaded values are compared at `:90`
   (`if (piMagicAndVersion[0] != kiMagic || piMagicAndVersion[1] != GetVersion())`). `piMagicAndVersion`
   is value-initialized to `{}` at `:86`.
2. The cached last-modified-time. `std::fstream lastModifiedTimeFileStream` is opened at
   `ExportJob.cpp:108`; `lastModifiedTimeFileStream.read(reinterpret_cast<char*>(&iLoadedLastModifiedTime), sizeof(iLoadedLastModifiedTime))`
   at `:109`; the loaded value is compared at `:110` (`if (iLastModifiedTime != iLoadedLastModifiedTime)`).
   `iLoadedLastModifiedTime` is initialized to `0` at `:107`.

The read/write contract: both sentinels are produced in `ExportJob::RunExport`. The magic+version
pair is written at `ExportJob.cpp:173-174`
(`int64_t piMagicAndVersion[2] = { kiMagic, GetVersion() }; fileStream.write(...)`), and the
last-modified-time at `:177-179`
(`int64_t iLastModifiedTime = std::filesystem::last_write_time(mInputPath).time_since_epoch().count(); ... lastModifiedTimeFileStream.write(...)`).
Both are fixed-width single-record writes, so the read side reads exactly one fixed-width record back.

**Both reads fail safe TODAY — this is latent-hardening, not a live bug.** On a short/failed read the
destination keeps its initialized contents (zero `piMagicAndVersion`, zero `iLoadedLastModifiedTime`),
which will not match the real `kiMagic`/`GetVersion()` pair or the real input mtime, so the job is
marked dirty and re-exported anyway. The value-mismatch path *incidentally* produces the correct
outcome. The fix makes "couldn't fully read the sentinel" mean "dirty/regenerate" *explicitly* —
independent of the bytes that happened to be in the buffer — matching the policy the `ExportScene`
`.PreExport` fix just established across the same file.

## Design

Add the stream-good guard to both comparison branches in `ExportJob::CheckDirty`, mirroring the
`if (!fileStreamIn || ...)` form the `ExportScene` fix uses.

### 1. Magic/version branch — `ExportJob.cpp:90`

Fold the stream state into the existing condition:

```cpp
if (!chunkFileStream || piMagicAndVersion[0] != kiMagic || piMagicAndVersion[1] != GetVersion())
```

`chunkFileStream` is closed at `:88` before the comparison; evaluate `!chunkFileStream` on the closed
stream (the failbit set by a short `read` survives `close()`), or move the test ahead of the `close()`
call — either is acceptable, pick the simpler diff. A short read on a fixed-width single-record
`read` sets failbit, so `!chunkFileStream` is true exactly when the full record was not read.

### 2. mtime branch — `ExportJob.cpp:110`

Fold the stream state into the existing condition:

```cpp
if (!lastModifiedTimeFileStream || iLastModifiedTime != iLoadedLastModifiedTime)
```

### KISS / scope notes

- `!stream` is sufficient for these fixed-width single-record reads — a short read sets failbit, which
  `operator bool()`/`operator!` reports. Do **not** add a separate `gcount()` byte-count check; that
  was only needed in `FileUtils` for a variable-length read. One token per branch.
- No new error handling, validation, or logging beyond reusing the existing dirty-marking branches
  (each already `LOG`s and sets `mbDirty = true; return mbDirty;`). Assume valid params per project
  policy.
- Offline DataPacker tooling only: no determinism, CRC, runtime, or network exposure. The change only
  affects which dirty path a corrupt/truncated cache sentinel takes, and the destination (regenerate)
  is unchanged.

## Critical files

- `DataPacker/Source/ExportJobs/ExportJob.cpp` — `ExportJob::CheckDirty` only. Magic/version branch
  condition at `:90` (stream `chunkFileStream`, opened `:84`, read `:87`); mtime branch condition at
  `:110` (stream `lastModifiedTimeFileStream`, opened `:108`, read `:109`). Writers at `:173-174` and
  `:177-179` in `ExportJob::RunExport` are the read/write-contract reference — not edited.

## Out of scope

- `Common/FileUtils.h` `ReadEntireFile` — its callers consume the returned bytes directly (not a
  skip-work gate), and the variable-length read there already got the `gcount()` treatment in the
  session that motivated this plan.
- The `ExportScene::MainExport` / `ReadMaterialInfosFromModel` `.MODEL` reads — they run *after* the
  file is freshly regenerated within the same `Export()` call, so there is no stale-sentinel skip-work
  decision to harden.
- The `ExportScene` `.PreExport` reads (`ExportScene.cpp:78`, `:222`) — already fixed in the landed
  session; this plan is the base-class sibling only.
- Any TOCTOU robustness (the file existing at `std::filesystem::exists` then vanishing before the
  `fstream` open), throwing-`std::filesystem` hardening, or directory-guard robustness.
- All style findings in `ExportJob.cpp` (Hungarian, brace placement, `reinterpret_cast` idiom, etc.) —
  owned by `code-style-review`, not this plan.

## Acceptance criteria

- A `CheckDirty` whose `.chunk` magic/version sentinel exists but cannot be fully read marks the job
  dirty via the explicit `!chunkFileStream` check at `:90` (rather than incidentally via the zeroed
  buffer failing the value comparison).
- A `CheckDirty` whose last-modified-time `.txt` sentinel exists but cannot be fully read marks the job
  dirty via the explicit `!lastModifiedTimeFileStream` check at `:110`.
- DataPacker builds clean.
- No behavior change on the happy path: a fully-readable matching sentinel still returns clean
  (`mbDirty = false`), a value mismatch still returns dirty.

## Notes

- Same fail-safe direction as the landed `ExportScene` `.PreExport` fix and the `FileUtils`
  full-read verification — "couldn't read it" and "doesn't match" both mean regenerate; this only makes
  the first reason explicit at the base-class sentinel reads instead of relying on a zeroed-buffer
  mismatch.
- All symbols and line numbers verified against `DataPacker/Source/ExportJobs/ExportJob.cpp`
  (`ExportJob::CheckDirty`, `ExportJob::RunExport`, `chunkFileStream`, `lastModifiedTimeFileStream`,
  `piMagicAndVersion`, `iLoadedLastModifiedTime`).
