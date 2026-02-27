# /Common/

Shared utilities and data format specifications used across DataPacker, Engine, and Projects. Foundation layer with no dependencies outside the codebase. All code is in `namespace common`.

**Header Inclusion**: All headers in this directory should only be included in `Common.h`, which is included in `Pch.h`.

## Architecture

### Binary Data Format (DataFile.h)
Defines the `.pack` file format with 16-byte aligned chunks. Each chunk has a `ChunkHeader` with type flags and a union of type-specific headers for fonts, models, shaders, textures, terrain islands, audio, and scene assets. Headers are compact count-only structs; variable-length arrays live in chunk data payloads for zero-copy pointer access at runtime.

Scene chunks link to associated model chunks via CRC. Shader chunks store Vulkan descriptor and vertex layout alongside SPIR-V bytecode. Skeletal animation structures separate node hierarchy and joint data into a data stream for efficient sequential access, with compact and cubic keyframe types.

`ModelNode` stores both bind matrix and TRS components separately; the runtime combines `matrix * S * R * T` (row-major equivalent of `T * R * S * matrix` in column-major). `JointMatrix` stores 3 rows instead of a full mat4 since the last row is always `(0,0,0,1)`. Normal matrices are precomputed CPU-side as `transpose(inverse(mat3(meshWorld)))`. Materials may be split from the original glTF to ensure correct per-primitive mesh world transforms.

### Error Handling (ErrorUtils.h/.cpp)
Validation macros (`ASSERT`, `CHECK_HRESULT`, `VERIFY_SUCCESS`) using `std::source_location` for automatic call site capture. Each macro checks its condition with `[[unlikely]]`, calls `DEBUG_BREAK()` on failure, then delegates to out-of-line functions that log and throw. `DEBUG_BREAK()` only fires when a debugger is attached and `kbEnableDebugBreak` is true.

### Profiling Utilities (Defines.h)
RAII profiling wrappers (`ScopedBootTimer`, `ScopedCpuProfile`) and `ProfileSetCount()`. All use `if constexpr (kbEnableProfiling)` for compile-time elimination when profiling is disabled.

### Workbuffer (Workbuffer.h/.cpp)
Reusable byte buffer with a push/pop stack allocator, eliminating per-frame heap allocations. Supports three access modes: raw pointer access via `PushBuffer<T>()`, string building via `Push()`/`Append()`/`View()`, and typed element operations via `PushBack<T>()`/`Span<T>()`. All modes require `Pop()` when done. Nested usage is supported via the internal stack. Auto-grows if capacity is exceeded (triggers `DEBUG_BREAK()` as a sizing alert). Owned by `ThreadLocal`, accessed via `gpThreadLocal->mWorkbuffer`.

### External Dependencies (ExternalHeaders.h)
Central include for all external libraries and standard library headers. New `#include <header>` additions go here, not in individual source files. Configures DirectX Math for SSE4 only (no AVX for determinism). Conditionally includes CRT debug heap and mimalloc (`BT_ENGINE`), DirectXTK, PerlinNoise, and `mmdeviceapi.h` (client-only via `BT_CLIENT` / `!BT_SERVER`), Dear ImGui (`BT_ENGINE`), and StackWalker (always). Uses Volk meta-loader for Vulkan. Includes LZ4 for fast lossless compression and ENet for reliable UDP networking.

### Thread-Local Storage (ThreadLocal.h/.cpp)
Provides per-thread log buffer and `Workbuffer` for reusable scratch memory. Owns backing memory internally. Each instance accepts an optional thread ID for categorization (engine threads use the `Threads` enum, DataPacker export jobs use their job ID). Optionally installs vectored exception handlers for crash logging and stack traces. Accessed via `thread_local` pointer `common::gpThreadLocal`.

### Logging (Log.h, LogFormatters.h)
Zero-allocation thread-safe logging using `if constexpr (kbEnableLogging)` for compile-time elimination. Writes directly into the per-thread log buffer via `std::format_to` with no heap allocations, falling back to a static buffer when `gpThreadLocal` is null. Custom `std::formatter` specializations for DirectX Math types, Vulkan enums, filesystem paths, and chrono durations write directly to the output iterator.

**Hex formatting**: Do not use `std::format` hex specifiers (`{:X}`, `{:x}`, `{:#x}`) in `Log()` calls -- MSVC's `std::format_to` internals may heap-allocate, which is unsafe in error paths. Instead use `common::ToHex()` with a stack-allocated buffer:
```cpp
char pcHex[20] {};
Log("Code: {}", ToHex(std::span(pcHex), uiValue));
```

## Key Utilities

- **CRC Hashing (Utils.h)** - Compile-time string hashing for asset identification. `Crc()` is constexpr, `CrcConsteval()` forces compile-time evaluation. Overloads for trivially copyable types, arrays, and XMVECTOR. `ConstexprCrcArray` generates numbered hash sequences at compile time
- **Type-Safe Flags (Flags.h)** - Wraps enum bitfields with explicit `Set()`/`Clear()`/`Toggle()` methods. Fully constexpr-compatible. Supports serialization and CRC generation for replay verification
- **Debug Verification (Utils.h)** - `BreakOnNotEqual()` uses byte-level comparison for floating-point types to match `Crc()` behavior, catching differences like `-0.0f` vs `+0.0f` that `operator==` would miss
- **Deterministic RNG (Random.h/.cpp)** - Xorshift64 engine with state comparison for replay verification. Integer and float `Random()` overloads plus `UniformRandom()` wrapping `std::mt19937`
- **Math Helpers (MathUtils.h/.cpp)** - DirectX Math wrappers for rotation, direction, distance, quaternion, area/AABB calculations, rounding with compile-time power-of-2 optimization, frame-rate independent exponential decay/interpolation via Pade approximation, and random jitter utilities
- **Binary I/O (Utils.h)** - `Write()`/`Read()` templates for trivially copyable types, arrays, vectors, and XMVECTOR. Eliminates reinterpret_cast boilerplate
- **Hex Conversion (Utils.h)** - `ToHex()` for zero-allocation hex string conversion with compile-time buffer size verification
- **Aligned Memory (Utils.h)** - `AlignedUniquePtr<T>` and `MakeAligned<T>()` for 64-byte aligned SIMD allocations with RAII cleanup
- **Performance Smoothing (Smoothed.h/.cpp)** - `InTheLastSecond` tracks events in a rolling 1-second window via fixed-size circular buffer. `Smoothed<T>` provides stepped convergence toward a running average

## Threading

- **Multithreading (Multithreading.h/.cpp)** - Worker pool for data-parallel dispatch. `Dispatch(iCount, processRange)` splits a range across all workers plus the main thread, then joins. Accessed via `common::gpMultithreading`
- **PersistentWorker (PersistentWorker.h/.cpp)** - Reusable dedicated thread for recurring async work, avoiding per-dispatch thread creation overhead. Uses semaphore-based wake/wait with exception forwarding to the calling thread. Used by Graphics, CommandBufferManager, SwapchainManager, and Multithreading

## Diagnostic Logging

- **DiagnosticLog (DiagnosticLog.h/.cpp)** - Thread-safe file logger for diagnostic output. Uses `std::ofstream` with mutex-guarded writes and immediate flush. Lifetime managed via RAII with a global `gpDiagnosticLog` pointer. `FILE_LOG_INIT(filename)` creates an instance (typically in Main.cpp), and `FILE_LOG(...)` writes formatted lines when active (no-op when null). Uses stack-allocated 2048-byte buffer with `std::format_to_n` for zero-heap-allocation formatting

## Platform Utilities
- **Timer.h** - High-resolution `std::chrono` timer with nanosecond precision
- **ScopedLambda.h** - RAII scope-exit lambda execution for cleanup operations
- **WindowsUtils.h/.cpp** - Error string conversion, process execution with stdout capture, core count detection, registry access
