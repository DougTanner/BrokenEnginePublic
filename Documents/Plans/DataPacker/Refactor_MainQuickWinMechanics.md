# Refactor: Main Quick-Win Mechanics

## Context
Source: /external-refactor-clean on `DataPacker/Source/` (non-recursive), plus one dependency-structure item from the architecture pass. Small mechanical cleanups in the orchestrator and tool-config TUs; none change packer output except the line-ending normalization (one-time header regen).

## Design

### DataPacker/Source/Main.cpp
- Add `#include "ExportJobs/ExportJob.h"` — `Main.cpp` uses the `IsExportJob` concept (`Main.cpp:317/340`) and `ExportJob` base members (`mRelativeFile`, `mFuture`, `mCrc`, `CheckDirty`, `RunExport`) but receives the header only transitively via the nine `Export*.h` includes. Load-bearing usage should be a direct include. [~5m]
- Remove the unused `using enum common::ChunkFlags;` (`Main.cpp:29`) — no bare `ChunkFlags` enumerator appears in the TU (only `common::ChunkFlags_t` at `Main.cpp:365`). [~5m]
- `MainThread` (`Main.cpp:546`) — `LogIndent(1)` is never balanced before return; replace with `ScopedLogIndent` (the pattern already used in `RunExportJobs`, `Main.cpp:380`). [~5m]
- `RunExportJobs<T>` (`Main.cpp:466-470`) — when `common::ContentsEqual(temporaryHeaderFile, headerFile)` is true the temp header is leaked in the temp dir; `std::filesystem::remove(temporaryHeaderFile)` in that branch. [~5m]
- `WriteCrcHeader` (`Main.cpp:320`) — opens text-mode while `WriteIfChanged` (`Main.cpp:60`) opens binary, so per-type CRC headers get CRLF and `DataTypes.h`/`Data.h` get LF. Add `std::ios::binary` for consistent LF across all generated headers. One-time regen of the CRC headers (content change), then stable. [~5m]

### DataPacker/Source/FileManager.cpp
- `FileManager::FileManager` (`FileManager.cpp:46`) — the temp-directory LOG reads `gpFileManager->mTempDirectory` from inside the constructor instead of the member; use `mTempDirectory` directly. [~5m]

### DataPacker/Source/Pch.h
- Remove the dead `kbIsDataPacker` (`Pch.h:9`) — repo-wide grep finds zero consumers (definition + one CLAUDE.md sentence only). Update the `DataPacker/Source/CLAUDE.md` line that presents it as meaningful ("Pch.h sets `kbIsDataPacker = true` …"). [~5m]

## Critical files
- `DataPacker/Source/Main.cpp`
- `DataPacker/Source/FileManager.cpp`
- `DataPacker/Source/Pch.h`
- `DataPacker/Source/CLAUDE.md`

## Out of scope
- Stream-state/error-handling checks on these same functions — `Architecture_OutputWriteTrustBoundary.md`.
- The sort comparator's per-comparison `common::ToLower` allocations (`Main.cpp:383`) — offline one-shot sort, micro-perf not worth the key-caching machinery (chat observation only).
- Renaming `Quit` (`Main.cpp:579` — shows a MessageBox, does not exit) — cosmetic (chat observation only).
- `mbCleanExport` (`FileManager.h:15`) — documented deliberate debugger-only toggle; not dead code.
- The RDO-sweep LOG float format specs — covered by the existing decision plan `DataPacker/DataPackerLogFloatSpecException.md`.

## Notes
- Offline DataPacker only — no determinism/CRC/runtime exposure. The line-ending item is the only output-visible change (one-time `.h` regen → one game recompile).
- Execute before `Architecture_RdoSweepRelocation.md` (shared `Main.cpp`, line drift).

## Verification Notes
- All items verified against source:
  - `using enum common::ChunkFlags;` (`Main.cpp:29`): grep over all 14 enumerators of `common::ChunkFlags` (`Common/DataFile.h:46-68`) finds zero bare or qualified uses in `Main.cpp` — the TU's only `ChunkFlags` reference is the `common::ChunkFlags_t` type at `Main.cpp:365`. Removal safe.
  - `kbIsDataPacker` (`Pch.h:9`): repo-wide case-insensitive grep finds only the definition and the one `DataPacker/Source/CLAUDE.md` sentence. No other project's `Pch.h` defines the symbol at all, which proves `Common/` cannot reference it (engine/game builds would already fail to compile) — removal cannot break a Common compile. The other `kb*` constants (`kbLogging` etc.) ARE consumed by Common; do not touch them.
  - `WriteCrcHeader` text-vs-binary (`Main.cpp:320` `std::ios::out` only, vs `WriteIfChanged` `Main.cpp:60` `| std::ios::binary`): confirmed — `std::endl`'s `\n` gets CRLF-translated in text mode on Windows, so per-type CRC headers are CRLF while `DataTypes.h`/`Data.h` are LF. Adding `std::ios::binary` is the minimal fix. Nuance: the regen lands per-type on each type's *next dirty export*, not all in one run.
  - `LogIndent(1)` (`Main.cpp:546`): confirmed unbalanced (no matching `LogIndent(-1)` before `MainThread` returns); `ScopedLogIndent` (`Common/Log/Log.h:105`) is the established replacement, already used at `Main.cpp:380`. Today the imbalance is masked (`gpThreadLocal` dies with `MainThread`'s local `ThreadLocal`, and `LogIndent` no-ops on null) — the change is unwind-correctness/pattern hygiene, kept as a quick-win mechanic.
  - Temp-header leak (`Main.cpp:467-470`) and the `gpFileManager->mTempDirectory` ctor self-reference (`FileManager.cpp:46`) confirmed as described.
  - Direct `ExportJob.h` include: `Main.cpp` includes exactly nine `Export*.h` headers (`Main.cpp:4-12`) and no `ExportJob.h`; `IsExportJob` used at `Main.cpp:317/340`. Valid include-hygiene fix.
