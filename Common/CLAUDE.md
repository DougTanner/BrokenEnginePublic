# Common - Shared Utilities and Data Formats

## Overview

Foundation layer (`namespace common`) with no dependencies outside the codebase. Provides utilities, data formats, threading, logging, and math helpers consumed by DataPacker, Engine, and Projects. All headers are aggregated through `Common.h` (pulled in by `Pch.h`); new headers are added only there.

## Key Systems

- **DataFile.h** - `.pack` binary format: 16-byte aligned chunks, type-tagged headers. `DataHeader::kiVersion` incorporates `sizeof(ChunkHeader)` so any header layout change bumps the version automatically. Also defines `kfSeaBottomMeters` — the engine-wide sea-floor elevation that unifies the elevation render-target clear (via `IslandTerrain::mfSeaFloorElevation`) with the CPU `GlobalElevation()` open-ocean fallback. DataPacker asserts each island's per-bake sea floor matches this constant so divergence is caught at bake time.
- **Workbuffer** - Stack allocator via `gpThreadLocal->mWorkbuffer`. `Push()` returns a `[[nodiscard]] ScopedWorkbufferArena` (RAII scope, dtor pops); `PushBuffer<T>(size)` returns a `[[nodiscard]] ScopedWorkbufferAllocation<T>` (typed pointer + RAII, with `operator T()` and `operator->()` for transparent access). `Pop()` is private — only the RAII handles call it. The arena type forwards `Append`/`AppendFloat`/`PushBack<T>`/`View`/`Span<T>`/`ShrinkLastPushBuffer` and is used for both flat scratch frames and loop- or lambda-driven content. `std::formatter` integrates with `LOG()` via per-argument wrappers (`Wb`, `WbV2`) and a passthrough specialization on the arena itself. **Invariant**: growth path calls `DEBUG_BREAK()` — buffers must be sized correctly up front.
- **Flags\<EnumType\>** - Type-safe bitfield wrapper; serializable and CRC-hashable. Underlying type must be unsigned.
- **Multithreading / PersistentWorker** - `gpMultithreading->Dispatch` worker pool. Workers run at `THREAD_PRIORITY_TIME_CRITICAL`, own a private `ThreadLocal`, forward exceptions across wake/wait via `std::exception_ptr`.
- **ThreadLocal** - Per-thread storage (log buffer, Workbuffer, tick/indent context). Ctor sets MXCSR and installs exception handlers (see Determinism / Exception Handling below).
- **RandomEngine** - Xorshift64 seeded via splitmix64, with equality + CRC for replay/desync verification. All gameplay randomness flows through this — never `std::random`.
- **Smoothed\<T, COUNT\>** - Rolling buffer with slow-drift near target, snap on large gap.
- **InTheLastSecond / Timer** - Time-windowed event counter and high-resolution steady-clock timer.

## Architecture Notes

### Determinism (centralized here)
- **DirectXMath**: SSE4-only; AVX/AVX2 banned as non-deterministic across CPUs. Enforced by `#error` guard in `ExternalHeaders.h`.
- **MXCSR**: Every thread's `ThreadLocal` ctor sets flush-denormals + round-to-nearest for cross-thread FP consistency.
- **Equality**: Deterministic bitwise `operator==` for `XMFLOAT2/3/4` / `XMVECTOR` in `ExternalHeaders.h` (not epsilon).
- **CRC**: `Crc()` is constexpr; `XMVECTOR` hashing round-trips via `XMFLOAT4` for consistent layout. Two-target overload XORs into both full + shared CRCs for collection `Crcs()` / `CrcsShared()`.
- **ValidateVector\<IS_POSITION\>**: `MathUtils.h` helper asserting all 4 lanes finite plus the W invariant (positions W=1.0, directions/velocities W=0.0). Called at collection Spawn/Transfer boundaries to catch W-lane corruption before it poisons downstream `XMVectorMultiplyAdd` / `XMVector3Normalize` consumers. See root [CLAUDE.md](../CLAUDE.md) "XMVECTOR W invariant".
- **Math helpers**: `ExponentialDecay` / `ExponentialInterpolant` use Padé (1,1) rational approximation — no transcendentals, frame-rate independent, never overshoot. Prefer over `std::exp`-based lerps.

### Binary Serialization
- `Read`/`Write` helpers in `Utils.h` are the engine-wide binary I/O pattern for trivially-copyable types and `std::vector<T>`. `XMVECTOR` overloads round-trip via `XMFLOAT4`. Do not `reinterpret_cast` manually.

### Logging
- **Public API**: `LOG(category, level, format, ...)` — compile-time filtered via each project's `constexpr LogLevel keLogLevels[]` in `Pch.h`, so filtered calls are eliminated by `if constexpr`.
- **Tick tagging**: `LogTickScope` (RAII, nestable) prefixes all logs within a tick.
- **Buffers**: Lockless per-category ring buffers + global write-once buffer; only the emission step (`OutputDebugString`/`printf`) holds a mutex. A counter suppresses the vectored exception handler from re-logging the engine's own debug-string calls. Dumped to crash reports.
- **DiagnosticLog / LogDifference**: `FILE_LOG_INIT` declares scoped per-file diagnostic logs (up to 4 concurrent). `LogDifference<NAME>` drives field-by-field desync diagnosis using `FixedString` NTTP labels and a scoped section-context tag.
- **`kTemp`** category is reserved for transient agent diagnostics.

### Exception Handling
- `ThreadLocal` ctor installs SEH translator, invalid-parameter handler, vectored exception handler, and terminate handler. Combined with `/EHa`, this promotes structured faults into C++ exceptions and logs a stack walk before `DEBUG_BREAK()`.
- **Validation macros**: `ASSERT` / `CHECK_HRESULT` / `VERIFY_SUCCESS` all `DEBUG_BREAK()` + throw on failure with `std::source_location`. `DEBUG_BREAK()` only fires with a debugger attached. Standard `assert` is `#define`d to a compile error — use `ASSERT`.

### Memory, Headers
- **ExternalHeaders.h** - Central include for all external/standard library headers. New `#include <header>` additions go here, not in individual source files.
- **Aligned allocation**: `AlignedUniquePtr<T>` + `MakeAligned<T>` provide RAII 64-byte aligned heap buffers for SIMD storage.

### Compile-Time Utilities
- `FixedString<N>` NTTP wrapper so string literals can be template args.
- `ConstexprCrcArray<SIZE>` for compile-time CRC tables of numbered names.
- `ScopedLambda` RAII deferred callable for cleanup that doesn't fit other scope guards.

## See Also

- [Architecture diagrams](../Documents/Architecture/)
