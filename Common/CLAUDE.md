# Common - Shared Utilities and Data Formats

## Overview

Foundation layer (`namespace common`) with no dependencies outside the codebase. Provides utilities, data formats, threading, logging, and math helpers consumed by DataPacker, Engine, and Projects. Broadly-used singletons and small headers sit at the `Common/` root; focused groups live in subdirectories: `Log/` (logging, formatters, diagnostic/difference logs), `Threading/` (multithreading, persistent worker, thread-local), `Math/` (math helpers, convex-hull placement, random). All headers are aggregated through `Common.h` (pulled in by `Pch.h`); new headers are added only there.

## Key Systems

- **DataFile.h** - `.pack` binary format: 16-byte aligned chunks, type-tagged `ChunkHeader` (per-type union of font/scene/island/model/shader/texture/audio headers). `DataHeader::kiVersion` incorporates `sizeof(ChunkHeader)`, so layout edits that change that size auto-bump the version; edits that don't (reordering/shrinking a non-largest union member, or changing a non-union payload struct) are caught instead by per-struct `sizeof`/`offsetof` `static_assert`s that force a manual version bump. Defines the shared elevation constants `kfSeaBottomMeters` (engine-wide sea-floor; unifies the elevation render-target clear via `IslandTerrain::mfSeaFloorElevation` with the CPU `GlobalElevation()` open-ocean fallback — DataPacker asserts each island's per-bake value matches) and `kfUnderwaterMaskThresholdMeters` (depth below which island textures are zeroed at bake and above which a pixel joins the valid-area convex hull).
- **Workbuffer** - Stack allocator via `gpThreadLocal->mWorkbuffer`. `Push()` returns a `[[nodiscard]] ScopedWorkbufferArena` (RAII scope, dtor pops); `PushBuffer<T>(size)` returns a `[[nodiscard]] ScopedWorkbufferAllocation<T>` (typed pointer + RAII, with `operator T()` and `operator->()` for transparent access). `Pop()` is private — only the RAII handles call it. The arena type forwards `Append`/`AppendFloat`/`PushBack<T>`/`View`/`Span<T>`/`ShrinkLastPushBuffer` and is used for both flat scratch frames and loop- or lambda-driven content; `Append` has both a `string_view` and a `wstring_view` overload (the latter UTF-8-converts wide input into the buffer with no heap allocation); these accessors are valid only within an open frame (read/append at depth 0 asserts, since the buffer is frame-relative). `std::formatter` integrates with `LOG()` via per-argument wrappers (`Wb`, `WbV2`/`WbV3`/`WbV4`) and a passthrough specialization on the arena itself. **Invariant**: growth path calls `DEBUG_BREAK()` — buffers must be sized correctly up front. Non-copyable/non-movable (holds a reference to its backing vector).
- **Crc.h** - The codebase-wide non-standard 64-bit `Crc()` hash family (string / pointer+count / trivially-copyable / `XMVECTOR` overloads, plus a two-target overload feeding collection `Crcs()`), `FixedString`/`ConstexprCrcArray`/`IntToString` (compile-time), and the `NotStringLike` concept.
- **Serialization.h** - Engine-wide binary `Read`/`Write` stream overloads (see Binary Serialization) and `VectorByteSize`.
- **StringUtils.h / FileUtils.h / AlignedMemory.h / TextureFormat.h** - Offline string helpers (`ToString`, `ToLower`, `Split`, `PathToCppVariable`, zero-alloc `ToHex`), file helpers (`ReadEntireFile`, `ContentsEqual`, `GetFileOrStringContent`), 64-byte-aligned SIMD storage (`AlignedUniquePtr`/`MakeAligned`/`AlignedDeleter`), and Vulkan texture `SizeInBytes`.
- **Math/MathUtils.h** - XMVECTOR helpers (color pack/unpack, direction/quaternion/rotation builders, jitter, `Distance`, `ComputeLeadPosition` projectile lead-solver), integer/float rounding (`RoundUp`/`RoundDown`), `MinAbs`/`Ceil`, `ValidateVector`, and the Padé exponential helpers (see Determinism).
- **Math/ConvexHull.h** - Deterministic 2D convex-hull placement system (`ConvexHull2D` + `BuildWorldHull` + AABB broadphase + SAT `ConvexHullsOverlap`) used by island-chain packing (rectangles may overlap underwater, hulls may not).
- **Flags\<EnumType\>** - Type-safe bitfield wrapper; serializable and CRC-hashable. Underlying type must be unsigned.
- **Threading/Multithreading + Threading/PersistentWorker** - `gpMultithreading->Dispatch` worker pool. Workers run at `THREAD_PRIORITY_TIME_CRITICAL`, own a private `ThreadLocal`, forward exceptions across wake/wait via `std::exception_ptr`. `PersistentWorker` (long-lived single worker) enforces a strict contract via `ASSERT`: one `Wake` per `Wait`, `Wait` before the next `Wake`, `Wait` before destruction, all from one calling thread; non-copyable/non-movable (worker lambda captures `this`).
- **Threading/ThreadLocal** - Per-thread storage (log buffer, Workbuffer, tick/indent context). Ctor calls into `Determinism.h` to set MXCSR and install exception handlers (see Determinism / Exception Handling below). Exactly one per thread (ctor `ASSERT`s `gpThreadLocal == nullptr` and owns it for its lifetime); non-copyable/non-movable (buffers alias its own backing vectors).
- **Math/Random (RandomEngine)** - Xorshift64 seeded via splitmix64, with equality + CRC for replay/desync verification. All gameplay randomness flows through this — never `std::random`.
- **Smoothed\<T, COUNT\>** - Rolling buffer with slow-drift near target, snap on large gap.
- **InTheLastSecond / Timer** - Time-windowed event counter and high-resolution steady-clock timer.

## Architecture Notes

### Determinism (centralized in `Determinism.h`)
- **`Determinism.h`** holds the deterministic bitwise `operator==` for `XMFLOAT2/3/4` / `XMVECTOR` (not epsilon), plus `ConfigureThreadFloatingPoint()` (MXCSR) and `SetupExceptionHandling()`. `ExternalHeaders.h` includes it right after its DirectXMath includes (so the operators are globally visible) and still owns the SSE4-only build knob, the PI subdivisions, `kfEpsilon`, and `XmIsNan`/`XmIsInf`.
- **DirectXMath**: SSE4-only; AVX/AVX2 banned as non-deterministic across CPUs. Enforced by `#error` guard in `ExternalHeaders.h`.
- **MXCSR**: Every thread's `ThreadLocal` ctor calls `ConfigureThreadFloatingPoint()` to set flush-denormals + round-to-nearest for cross-thread FP consistency.
- **CRC**: `Crc()` (in `Crc.h`) is constexpr; `XMVECTOR` hashing round-trips via `XMFLOAT4` for consistent layout. Two-target overload XORs into both full + shared CRCs for collection `Crcs()` / `CrcsShared()`.
- **ValidateVector\<IS_POSITION\>**: `Math/MathUtils.h` helper asserting all 4 lanes finite plus the W invariant (positions W=1.0, directions/velocities W=0.0). Called at collection Spawn/Transfer boundaries to catch W-lane corruption before it poisons downstream `XMVectorMultiplyAdd` / `XMVector3Normalize` consumers. See root [CLAUDE.md](../CLAUDE.md) "XMVECTOR W invariant".
- **Math helpers**: `ExponentialDecay` / `ExponentialInterpolant` (in `Math/MathUtils.h`) use Padé (1,1) rational approximation — no transcendentals, frame-rate independent, never overshoot. Prefer over `std::exp`-based lerps.

### Binary Serialization
- `Read`/`Write` helpers in `Serialization.h` are the engine-wide binary I/O pattern for trivially-copyable types and `std::vector<T>`. `XMVECTOR` overloads round-trip via `XMFLOAT4`. Do not `reinterpret_cast` manually.

### Logging (`Log/` subdirectory)
- **Public API**: `LOG(category, level, format, ...)` (`Log/Log.h`) — compile-time filtered via each project's `constexpr LogLevel keLogLevels[]` in `Pch.h`, so filtered calls are eliminated by `if constexpr`.
- **Tick tagging**: `LogTickScope` (RAII, nestable) prefixes all logs within a tick.
- **Buffers**: Lockless per-category ring buffers + global write-once buffer; only the emission step (`OutputDebugString`/`printf`) holds a mutex. A counter suppresses the vectored exception handler from re-logging the engine's own debug-string calls. Dumped to crash reports.
- **Formatters**: `Log/LogFormatters.h` provides `std::formatter` specializations so `LOG`/`std::format` handle engine types directly (`XMVECTOR`/`XMFLOAT2/3/4[A]`, `Flags`, `RandomEngine`, paths/wide strings, chrono durations, narrow int8/uint8, the `Wb`/`WbV2`/`WbV3`/`WbV4` workbuffer float wrappers). All are allocation-free (paths/wide strings route through the workbuffer; the rest use stack buffers) and forward through a base formatter so width/precision/fill specs are honored. Add new specializations here.
- **DiagnosticLog / LogDifference** (`Log/DiagnosticLog.h`, `Log/LogDifference.h`): `FILE_LOG_INIT(i, file)` / `FILE_LOG(i, ...)` write per-file diagnostic logs (up to 4 concurrent, flushed each line, allocation-tracking suppressed). `LogDifference<NAME>` drives field-by-field desync diagnosis using `FixedString` NTTP labels and a scoped section-context tag.
- **`kTemp`** category is reserved for transient agent diagnostics.

### Exception Handling
- `ThreadLocal` ctor calls `SetupExceptionHandling()` (`Determinism.h`). The per-thread CRT handler slots (SEH translator, invalid-parameter handler) install on every ctor; the process-global handlers (vectored exception handler, terminate handler, error-mode / CRT-report setup) install exactly once via `std::call_once`, so concurrent ctors are race-free and the vectored handler is registered only once. Combined with `/EHa`, this promotes structured faults into C++ exceptions and logs a `LogStackWalker` (`StackWalker.h`) call stack before `DEBUG_BREAK()`.
- **Validation macros**: `ASSERT` / `CHECK_HRESULT` / `VERIFY_SUCCESS` all `DEBUG_BREAK()` + throw on failure with `std::source_location`. `DEBUG_BREAK()` only fires with a debugger attached. Standard `assert` is `#define`d to a compile error — use `ASSERT`.

### Headers, Platform
- **ExternalHeaders.h** - Central include for all external/standard-library headers; also sets the determinism build knobs (SSE4-only DirectXMath, AVX `#error` guard) and includes `Determinism.h` (which carries the deterministic `XMFLOAT2/3/4` / `XMVECTOR` `operator==`; see Determinism above). New `#include <header>` additions go here, not in individual source files.
- **WindowsUtils.h** - Win32 wrappers (last-error/HRESULT string formatting, file-time formatting, physical/logical core counts, child-process launch/capture) used mostly by tools and startup.
- **ScopedLambda** - RAII deferred callable for cleanup that doesn't fit other scope guards.

## See Also

- [Architecture diagrams](../Documents/Architecture/)
