# Architecture: Output Write Trust Boundary

## Context
Source: /external-architecture-review on `DataPacker/Source/` (non-recursive). No output write in `Main.cpp` checks stream state: on disk-full / permission failure the temp files are written truncated, the `std::filesystem::rename` calls still succeed, and the run exits 0 — silently replacing good `.pack`/`.manifest`/generated-header outputs with corrupt ones. The engine's CRC validation may catch a corrupt pack at runtime, but a truncated generated `.h` feeds compiles with no detector at all. This contradicts the repo trust-boundary directive (validate OS/third-party API results). `FileManager.cpp` is already the good citizen (`GetTempPath` checked, `VERIFY_SUCCESS(exists(...))` on output/temp dirs).

## Design

### DataPacker/Source/Main.cpp
- `WriteIfChanged` (`Main.cpp:56-65`) — verify the output `std::fstream` after writing: `VERIFY_SUCCESS(stream.good())` before `close()` (or after, re-checking). [~5m]
- `WriteCrcHeader` (`Main.cpp:318-338`) — same stream-state check before returning; this header is later renamed over the live generated header. [~5m]
- `RunExportJobs<T>` (`Main.cpp:340-474`) — check `temporaryManifestFileStream` and `temporaryPackFileStream` state after the final writes / before `close()` at `Main.cpp:439-440`, and only proceed to the `std::filesystem::rename` calls (`Main.cpp:463-469`) on success. A failed stream should follow the existing failure path (log + `Quit` + remove temp files) rather than renaming truncated output over good files. [~15m]
- `main` (`Main.cpp:588-589`) — replace `__assume(hMutex != nullptr)` with `VERIFY_SUCCESS(hMutex != nullptr)`; a NULL handle currently flows into `WaitForSingleObject` and the unconditional `ReleaseMutex`/`CloseHandle` deleter. `CreateMutex` is exactly the OS-API trust boundary the directive names. [~5m]
- `LoadBc7AsFloatPixelsMip0` (`Main.cpp:216-287`; header reads at `Main.cpp:218-240`) — diagnostic-only path reading an opaque on-disk intermediate: ASSERT the stream/read results and sanity-bound the header fields (`riWidth`/`riHeight` > 0, `iMipMaps` in a plausible range) before they size allocations at `Main.cpp:238/252/259`. It already ASSERTs the zlib result (`Main.cpp:255`). [~10m]

## Critical files
- `DataPacker/Source/Main.cpp`

## Out of scope
- The ExportJobs-side clean-path cached-chunk read — already covered by `DataPacker/ExportJobCleanPathCachedChunkReadCheck.md`.
- Cross-volume rename robustness (`%TEMP%` on a different volume than a CLI output dir makes `std::filesystem::rename` fail on Windows) — surfaces as an exception → MessageBox → exit 1, not silent corruption; separate concern, not filed.
- The per-type modal `Quit` stacking across multiple failing export types (`RunAllMainExports` continues past failures by design) — error-reporting policy, not a trust-boundary check.
- Any change to `.pack`/`.manifest` layout, `DataHeader`, or chunk formats.

## Notes
- Offline DataPacker only — no runtime, CRC-replay, network, or determinism exposure. Failure modes change from silent-corrupt-exit-0 to loud failure; no behavior change on healthy runs.
- Sibling of `ExportJobCleanPathCachedChunkReadCheck.md` (same trust-boundary-read theme, different file).

## Verification Notes
- All stream-state claims verified against source: `WriteIfChanged` (`Main.cpp:56-65`), `WriteCrcHeader` (`Main.cpp:317-338`), and the `RunExportJobs<T>` manifest/pack streams (closed unchecked at `Main.cpp:439-440`, renamed at `Main.cpp:463-469`) have zero stream-state checks; a badbit write followed by successful renames returns `true` → exit 0. `FileManager.cpp` good-citizen claim verified (`GetTempPath` checked at `:36-40`, `VERIFY_SUCCESS(exists)` at `:27/:42`).
- `VERIFY_SUCCESS` (`Common/ErrorUtils.h:14`) routes failure through `common::Assert` → LOG + `DEBUG_BREAK()` (debugger-only) + `throw std::runtime_error`. Outside the debugger the throw is caught by `main`'s try/catch → MessageBox → exit 1 — correct loud-failure fit for the tool.
- **Caveat (`main:588` mutex item)**: the `CreateMutex` call sits *before* `main`'s try/catch and before any `ThreadLocal` exists, so a failing `VERIFY_SUCCESS(hMutex != nullptr)` there throws out of `main` → `std::terminate` (no MessageBox, but `common::Assert` LOGs first and the log path null-checks `gpThreadLocal`). Still loud; accept, or note in the grill whether to hoist the check inside the guarded `runOnce` region.
- **Caveat (`WriteIfChanged` semantics)**: unlike the pack/manifest paths, `WriteIfChanged` writes directly to the live output file (no temp+rename), so by the time the stream check fires the old content is already truncated on disk. The check converts silent corruption into loud failure but cannot preserve the prior file; a temp+rename conversion is deliberately not in scope.
