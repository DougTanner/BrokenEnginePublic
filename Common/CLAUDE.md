# Common - Shared Utilities and Data Formats

## Overview

Foundation layer (`namespace common`) with no dependencies outside the codebase. Provides utilities, data format definitions, threading, logging, and math helpers used by DataPacker, Engine, and Projects. All headers are included via `Common.h` (which is pulled in by `Pch.h`) — new headers should only be added there.

## Key Systems

- **DataFile.h** - Defines the `.pack` binary format with 16-byte aligned chunks and type-specific headers for all asset types (fonts, models, shaders, textures, terrain, audio, scenes)
- **Workbuffer** - Push/pop stack allocator for reusable scratch memory, eliminating per-frame heap allocations. Accessed via `gpThreadLocal->mWorkbuffer`. Supports raw buffers, string building, and typed element operations with nested usage. `ScopedWorkbufferPop` is a RAII wrapper that auto-pops and provides implicit `const char*` conversion (with `std::formatter` specialization for direct use in `Log()`)
- **Flags\<EnumType\>** - Type-safe bitfield wrapper with `Set()`/`Clear()`/`Toggle()` methods, replacing raw booleans. Supports serialization and CRC for replay verification
- **Multithreading** - Worker pool accessed via `common::gpMultithreading`. `Dispatch(count, processRange)` splits work across all workers plus the main thread, then joins. `IsMainThread()` guards operations that must not run on worker threads. The 3-arg constructor creates a non-global instance for dedicated dispatch pools
- **PersistentWorker** - Dedicated reusable thread for recurring async work with semaphore-based wake/wait and exception forwarding
- **Timer** - High-resolution steady-clock timer. `GetDeltaNs(bReset)` measures elapsed nanoseconds
- **ThreadLocal** - Per-thread storage owning a log buffer and `Workbuffer`. Configures deterministic floating-point math on every thread. Accessed via `common::gpThreadLocal`

## Architecture Notes

### Logging
- **Zero-allocation logging**: `Log()` writes directly into per-thread buffers via `std::format_to`. Accepts up to three args before the format string: optional `LogCategory` (default `kLogDefault`) and optional `LogLevel` (default `kDebug`). Category aliases all use the `kLog*` prefix to avoid enum collisions: `kLogDefault`, `kLogLoading`, `kLogNetwork`, `kLogAudio`, `kLogGraphics`. Level aliases: `kVerbose`, `kDebug`, `kWarning`, `kError`. `kVerbose` is used for per-frame/per-tick spam; `kWarning` for issues worth investigating; `kError` always outputs regardless of the current level threshold. Avoid `std::format` hex specifiers in `Log()` calls (MSVC may heap-allocate); use `common::ToHex()` with a stack buffer instead
- **Level and category filtering**: `geLogLevel` sets the minimum level threshold; messages at or above it are shown for all categories. `geFocusedLogCategory` bypasses the threshold for one category, showing all levels for it. Both globals are initialized from `keLogLevelDefault` and `keFocusedLogCategoryDefault` defined in each project's `Pch.h`
- **In-memory ring buffers**: Each log category has a 128-line wrapping ring buffer (`gLogRingBuffers[]`); a separate 1024-line write-once global buffer (`gLogGlobalBuffer`) captures all output. On crash, `LogDumpBuffers()` writes both to the crash report file
- **DiagnosticLog**: `FILE_LOG_INIT(index, filename)` / `FILE_LOG(index, ...)` for thread-safe diagnostic output to up to 4 simultaneous log files. `FILE_LOG` is for data comparison logging with descriptive labels; prefer `Log(kLogCategory, "")` with categories for temporary diagnostic logging
- **LogDifference**: Template helpers (`LogDifference<NAME>`, `LogDifference_Vec`) for field-by-field comparison logging. Used by frame/collection `LogDifferences()` methods for desync diagnosis. `ScopedLogDifferenceContext` sets a thread-local context string so log output identifies which collection or frame section produced the mismatch

### Windows Utilities
- **WindowsUtils.h** - Windows-specific helpers: error code formatting (`LastErrorString`, `HresultToString`), file time to locale string conversion, CPU core count queries, synchronous subprocess execution with output capture (`RunExecutable`, used by DataPacker), and fire-and-forget process launch (`LaunchExecutable`)

### Math & Headers
- **Deterministic math**: `ThreadLocal` constructor sets MXCSR state (flush denormals, round-to-nearest) on every thread for cross-thread consistency
- **ExternalHeaders.h**: Central include for all external/standard library headers. New `#include <header>` additions go here, not in individual source files
- **Compile-time CRC**: `Crc()` is constexpr; `CrcConsteval()` forces compile-time evaluation. Used for asset identification throughout the engine

### Validation
- **Validation macros**: `ASSERT`, `CHECK_HRESULT`, `VERIFY_SUCCESS` with `std::source_location` for automatic call site capture

## See Also

- [Architecture diagrams](../Documents/Architecture/)
