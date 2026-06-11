# Refactor: In-Function Cleanups

## Context

Source: /external-refactor-clean on `Engine/Source/Profile`. In-function mechanics only. No file exceeds the `/reduce-file` thresholds (378/477/357 lines); hot-path allocation discipline is fully compliant (both `// Heap:` sites suppression-wrapped at `ProfileManagerBase.cpp:122-128` and `:169-171`, no float specs in any `LOG`, all formatters workbuffer-based).

## Design

### Engine/Source/Profile/ProfileManagerBase.h (+ call sites)
- `CpuStop(int64_t iCpuTimer, bool bSmoothNow, bool bCrossThread = false)` (`:184`) — two positional bools produce unreadable call sites (`CpuStop(kCpuTimerAcquireToGlobal, true, true)` at `CommandBufferManager.cpp:105`) → `common::Flags<CpuStopFlags>` (`kSmoothNow`, `kCrossThread`; unsigned underlying type per `Flags` requirements), defaulted empty so the common `CpuStop(timer, false)` sites become `CpuStop(timer)`. Root CLAUDE.md Flags-over-booleans pattern; precedent: `Audio/Refactor_VoiceStartClickDiscipline.md`'s `LoadVoiceFlags`. 17 call sites across `Main.cpp` (1), `GameBase.cpp` (6), `Graphics.cpp` (3), `SwapchainManager.cpp` (1), `CommandBufferManager.cpp` (3), `ProfileManagerBase.cpp` (1, the `ScopedCpuProfile` dtor), and game `ClientSession.cpp` (2). [~30m]

### Engine/Source/Profile/ProfileScreens.cpp
- Decompose `FormatGpuScreen` (`:195-353`, ~158 lines) into three file-local helpers mirroring its existing block structure: graphics-info text (`:199-217`), GPU-timer rows + per-pass dynamic-resolution annotations (`:219-301`), VMA memory stats (`:303-352`). Behavior-identical; each block already owns its own `ScopedWorkbufferArena`, so the seams are clean. [~20m]

### Engine/Source/Profile/ProfileManagerBase.cpp
- Empty `ProfileManagerBase()` / `~ProfileManagerBase()` definitions (`:19-25`) → `= default` in the header; delete the .cpp definitions. [~5m]

## Critical files
- Engine/Source/Profile/ProfileManagerBase.h
- Engine/Source/Profile/ProfileManagerBase.cpp
- Engine/Source/Profile/ProfileScreens.cpp
- `CpuStop` call-site files (mechanical signature update)

## Out of scope
- `UpdateProfileText`/`LogTimers` `#ifdef` width — owned by `Architecture_ClientServerGuardScope.md`.
- `bSmoothNow` latch semantics — `Architecture_SmoothNowDoubleLatch.md` (same function; co-schedule, with the flags conversion landing with or after the latch fix).
- `FormatCpuScreen`/`FormatFpsHeader` decomposition — under 50 lines each, fine as-is.

## Notes
- No determinism/CRC/network/`kiVersion` exposure — profiling-only signature change and file-local decomposition, compile-checked in both builds.
- Grill decision (pre-staged): `CpuStopFlags` home — namespace-scope in `ProfileManagerBase.h` beside the other profile enums (recommended) vs nested in the class.

## Verification Notes
Verified against source (2026-06-10 pass); one count corrected:
- **Corrected**: call-site count was 18, actual is 17 (repo grep, excluding the `.h` declaration and `.cpp` definition): `Main.cpp:282`, `GameBase.cpp:84/149/207/208/388/389`, `Graphics.cpp:224/233/255`, `SwapchainManager.cpp:476`, `CommandBufferManager.cpp:105/109/170`, `ProfileManagerBase.cpp:474` (dtor), `ClientSession.cpp:182/228`.
- `common::Flags` unsigned-underlying requirement confirmed (`Flags.h:10` `static_assert(std::is_unsigned_v<underlying_t>)`); the cited precedent plan exists (`Audio/Refactor_VoiceStartClickDiscipline.md`, `LoadVoiceFlags` item).
- `FormatGpuScreen` block boundaries and seams verified: graphics-info `:199-217`, GPU-timer rows `:219-301`, VMA stats `:303-352`; each block owns its own `ScopedWorkbufferArena` (`:202`, `:221`, `:304`), so the three-helper split is behavior-identical. The water-LOD locals (`:226-229`) are consumed only inside the GPU-timer block — clean capture.
- Empty ctor/dtor at `:19-25` confirmed; `= default` in the header is safe (virtual dtor stays virtual; no out-of-line anchor needed — the class already has out-of-line virtuals).
- Context claims spot-checked: file sizes 378/477/357 lines exact; both `// Heap:` sites suppression-wrapped (`:122-128`, `:169-171`); no float format specs in any Profile `LOG`.
- Cross-references consistent: latch semantics owned by `Architecture_SmoothNowDoubleLatch.md`, `#ifdef` width by `Architecture_ClientServerGuardScope.md`; the flags conversion must land with or after the latch fix (both touch `CpuStop`).
