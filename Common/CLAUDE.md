# /Common/

Shared utilities and data format specifications used across DataPacker, Engine, and Projects. Foundation layer with no dependencies outside the codebase. All code is in `namespace common`.

## Architecture

### Binary Data Format (DataFile.h)
Defines `.pack` file format with 16-byte aligned chunks. Each chunk has a `ChunkHeader` with type flags and a union of type-specific headers for fonts, models, shaders, textures, terrain islands, audio, and scene assets. `kiChunkDataOffset` is the aligned offset from the start of a chunk to its data payload (16-byte aligned `sizeof(ChunkHeader)`). Headers are compact count-only structs; variable-length arrays live in chunk data payloads for zero-copy pointer access at runtime.

**Scene chunks**: `SceneHeader` stores texture/material counts, animation flag, and `modelCrc` linking to the associated .MODEL vertex/index chunk. Chunk data payload layout: `[textureCrcs ALIGN16] [indexStarts ALIGN16] [MaterialShaderData...]`.

**Shader chunks**: `ShaderHeader` stores descriptor binding count, vertex attribute count, and vertex input stride. Chunk data payload layout: `[VkDescriptorSetLayoutBindings ALIGN16] [VkVertexInputAttributeDescriptions ALIGN16] [SPIR-V bytecode]`.

**Skeletal animation structures**: `Skeleton` stores node and skin joint counts only; the actual node hierarchy (`ModelNode[]`), `skinJointToNode[]` mapping, and `inverseBindMatrices[]` live in the animation data stream. `AnimationHeader` stores animation/channel/keyframe counts and a `Skeleton`; `AnimationClip[]` and `MaterialInfo[]` arrays also live in the animation data stream. Animation clips contain named channels referencing nodes by `uiNodeIndex`; keyframe data uses separate compact (`AnimationKeyframe` for STEP/LINEAR) and full (`AnimationKeyframeCubic` with in/out tangents for CUBICSPLINE) keyframe types.

`ModelVertex` defines the GPU vertex format with 5 UV channels (f2Uv through f2Uv4) for per-material texture coordinate selection and provides `operator==` plus a `std::hash` specialization for hash-based vertex deduplication during export. `ModelNode` stores both the node matrix property (`f4x4BindMatrix`) and TRS components (`f4BindTranslation`, `f4BindRotation`, `f4BindScale`) separately; the runtime combines matrix * S * R * T when building local transforms, matching Vulkan-glTF-PBR's transform order. `MaterialInfo` stores per-material metadata including `iOriginalMaterialIndex` to track the original glTF material index when materials are split (the exported model may have more materials than the original glTF to ensure correct per-primitive mesh world transforms at runtime). `MeshData` contains per-mesh GPU data including world matrix, precomputed normal matrix (stored as 3 vec4s), joint count, and joint matrix offset; the normal matrix is computed CPU-side as `transpose(inverse(mat3(meshWorld)))` to avoid shader-side inverse() calls. `JointMatrix` stores 3 rows (48 bytes) instead of a full mat4 (64 bytes), since row 3 is always (0, 0, 0, 1) for rigid bone transforms; the shader reconstructs the full mat4 from the 3 stored rows. `MaterialShaderData` contains per-material PBR shader data.

### Warning Suppressions (Defines.h)
Disables specific compiler and code analysis warnings that conflict with the codebase style.

### Error Handling (ErrorUtils.h/.cpp)
Validation functions using `std::source_location` for automatic call site capture. All validation functions require an expression string parameter for diagnostics; use the corresponding macros which automatically stringify the expression:

- **ASSERT(expr)**: Macro for condition validation - calls `Assert()` with stringified expression
- **CHECK_HRESULT(expr)**: Macro for Windows HRESULT validation - calls `CheckHresult()` with error string lookup
- **VERIFY_SUCCESS(expr)**: Macro for boolean validation - calls `VerifySuccess()` with `GetLastError()` reporting
- **DebugBreak()**: Conditional debugger breakpoint (only when debugger attached and `kbEnableDebugBreak` is true)

Inline wrapper functions check conditions and call out-of-line `[[noreturn]]` failure handlers for optimal code generation.

### Profiling Utilities (Defines.h)
RAII classes and inline functions for performance profiling:
- **ScopedBootTimer**: RAII wrapper for boot-time measurements
- **ScopedCpuProfile**: RAII wrapper for CPU timing sections
- **ProfileSetCount()**: Inline function for setting counter values

All profiling utilities use `if constexpr (kbEnableProfiling)` for compile-time elimination when profiling is disabled. ProfileManager methods are called directly via `gpProfileManager->Method()` (gpProfileManager is always valid).

### Workbuffer (Workbuffer.h)
`Workbuffer` class providing a unified reusable byte buffer for multiple use cases without per-frame allocations. Does not own its backing memory -- accepts a `std::vector<std::byte>&` reference at construction. The caller is responsible for allocating and owning the backing storage. All operations use a push/pop stack allocator. Supports three modes of access:
- **Raw pointer access**: `PushBuffer<T>(iSizeInBytes)` pushes a new stack level and returns a typed pointer into the buffer, auto-growing if needed. Caller must call `Pop()` when done. Used for temporary storage of variable-size data (e.g., raw input messages, animation matrices, collision tracking).
- **String building**: `Push()` starts a new stack level, `Append()` overloads for string views and integers, `AppendFloat()` for formatted float output, `View()` returns a `std::string_view` of the current level's data. Caller must call `Pop()` when done. Used by ProfileManager and TimeStep for allocation-free text construction.
- **Typed element operations**: After `Push()`, `PushBack<T>()` appends a value, `Span<T>()` returns a typed span over the current level's contents. Caller must call `Pop()` when done. Used by TextManager for temporary float arrays and Buffer for Vulkan barrier arrays.

**Push/pop stack allocator**: Both `PushBuffer()` and `Push()` push the current base position onto an internal stack and start a new region at the current size offset. `Pop()` pops the stack, restoring the previous base and discarding the current region. This allows nested usage up to 8 levels deep -- an inner consumer can `Push()`/`Pop()` without disturbing an outer consumer's data. Each level's `View()` and `Span<T>()` only see data within that level's region (from its base to the current size).

Owned by `ThreadLocal` (one per thread), accessed via `common::gpThreadLocal->mWorkbuffer`.

### External Dependencies (ExternalHeaders.h)
Central include for all external libraries and standard library headers. Configures DirectX Math for SSE4 only (no AVX for determinism). Adds comparison operators for XMFLOAT types. Conditionally includes CRT debug heap headers when `ENABLE_CRT_DEBUG_HEAP` is defined (for memory leak tracking). Conditionally includes DirectXTK (audio, gamepad), PerlinNoise, and StackWalker for engine builds. Engine builds include mimalloc.h always; when `ENABLE_CRT_DEBUG_HEAP` is not defined, also includes `mimalloc-override.h` and `mimalloc-stats.h` for the custom allocator and shutdown statistics. Uses Volk meta-loader for Vulkan.

### Thread-Local Storage (ThreadLocal.h/.cpp)
`ThreadLocal` class provides per-thread log buffer pointer and a `Workbuffer` member for reusable scratch memory (raw pointer access, string building, and typed element operations). Does not own its backing memory -- accepts a `char*` for the log buffer and a `std::vector<std::byte>&` for the workbuffer backing storage at construction. The caller allocates these as stack-local or static C arrays/variables and passes them in, keeping all allocation out of ThreadLocal. Constructor and destructor are out-of-line (defined in ThreadLocal.cpp). Each instance accepts an optional `int64_t` thread ID for thread categorization (engine threads pass `Threads` enum values, DataPacker export jobs pass their job ID). A `bSetupExceptionHandling` parameter (default `true`) controls whether vectored exception handlers are installed for crash logging and stack traces. The `Threads` enum names all engine async threads (eager/lazy load, texture upload, DxDiag, render, submit global/main, present, screenshot, multithreading).

### Logging (Log.h, LogFormatters.h)
Zero-allocation thread-safe logging using `if constexpr (kbEnableLogging)` for compile-time elimination when disabled. Each project defines `kbEnableLogging` in its Pch.h. `Log()` uses `std::format_string<>` for compile-time format validation and `std::format_to` to write directly into the per-thread log buffer (`gpThreadLocal->mpLogBuffer`, a raw `char*`) with no heap allocations. Falls back to a static local C array when `gpThreadLocal` is null. Provides `Log()`, `LogIndent()`, and `ScopedLogIndent` (RAII indent helper). Outputs to `OutputDebugString` and optional file stream. Custom `std::formatter` specializations for `std::string`/`std::wstring`, filesystem paths, DirectX Math types (XMFLOAT3, XMFLOAT4, XMFLOAT4A, XMVECTOR), Vulkan enums (VkFilter, VkSamplerAddressMode, VkResult), and chrono duration types (nanoseconds, microseconds, milliseconds, seconds) write directly to the output iterator via `std::format_to()` to avoid temporary string allocations.

## Key Utilities

### CRC Hashing (Utils.h/.cpp)
Compile-time string hashing for asset identification. `Crc()` is constexpr (works at compile-time or runtime), while `CrcConsteval()` forces compile-time evaluation with a compiler error if used with runtime values. Overloads for trivially copyable types and arrays. `ConstexprCrcArray` generates numbered hash sequences at compile time for related asset name lookups.

### Type-Safe Flags (Flags.h)
`Flags<ENUM_TYPE>` template wraps enum bitfields with explicit `Set()`/`Clear()` methods instead of bitwise operators. Supports single flag or initializer_list for multiple flags. `Toggle()` flips a flag and returns the new state. Read-only `operator&` tests flag presence. Fully constexpr-compatible for use as compile-time template parameters. Supports serialization and CRC generation for replay verification.

### Debug Verification (Utils.h/.cpp)
`BreakOnNotEqual()` compares two values for equality and triggers a debug breakpoint if they differ (when `kbVerifyFrame` is enabled). Uses byte-level comparison (memcmp) for XMFLOAT types and XMVECTOR to match Crc() behavior, ensuring replay verification detects differences like -0.0f vs +0.0f that floating-point == would miss.

### Deterministic RNG (Random.h)
`RandomEngine` struct using Xorshift64 algorithm with 64-bit state for fast, reproducible simulations. Provides seed constructor, time-based seeding, and state comparison for replay verification. Two `Random()` overloads: integer version returns `uint32_t` in range [0, max], float version returns value in range [0, MAX) with compile-time MAX parameter. Also wraps `std::mt19937` via `UniformRandom()` for standard library compatibility.

### Math Helpers (MathUtils.h/.cpp)
DirectX Math wrappers for rotation, direction, distance, and quaternion operations. Area/quad calculations with point-in-polygon testing. AABB computation and intersection tests. Rounding templates with compile-time power-of-2 optimization. Frame-rate independent exponential decay and interpolation using Pade approximation. Random jitter utilities for XY offset, position jitter, and direction jitter with both compile-time template and runtime parameter variants.

### Binary I/O (Utils.h/.cpp)
`Write()`/`Read()` template functions for trivially copyable types, arrays, and vectors. Eliminates reinterpret_cast boilerplate throughout serialization code.

### Aligned Memory (Utils.h/.cpp)
`AlignedUniquePtr<T>` and `MakeAligned<T>()` for 64-byte aligned SIMD allocations with RAII cleanup.

### Conditional Member Elimination (Utils.h/.cpp)
`Empty` struct for use with `[[no_unique_address]]` and `std::conditional_t` to eliminate member storage at compile time when a feature is disabled.

### Performance Smoothing (Smoothed.h)
`InTheLastSecond` tracks event counts in a rolling 1-second window using a fixed-size circular buffer (1024 entries) to avoid heap allocations from `std::deque`. `Smoothed<T, COUNT>` provides smoothed metrics via a circular buffer with stepped convergence toward the running average, plus max and most-recent-value queries.

### Multithreading (Multithreading.h)
`Multithreading` class providing a pool of `PersistentWorker` threads for parallel work dispatch. Created in `engine::MainThread()` with a worker count of `(hardware cores - 2)` (reserving one for main thread, one for render thread). Accessed globally via `common::gpMultithreading`.

**Dispatch Model**: `Dispatch(iCount, processRange)` splits a range `[0, iCount)` evenly across all workers plus the main thread. Each worker receives a `[iStart, iEnd)` sub-range via `Wake()`, the main thread processes the remaining tail, then all workers are joined via `Wait()`. The `processRange` callable receives `(int64_t iStart, int64_t iEnd)`. Ranges with zero items for a worker are skipped. `WorkerCount()` returns the number of background workers (excluding the main thread).

### Persistent Worker Thread (PersistentWorker.h)
`PersistentWorker` provides a reusable dedicated thread for recurring async work, avoiding the overhead of `std::async`/`std::future` thread creation per dispatch. The thread runs at `THREAD_PRIORITY_TIME_CRITICAL` and constructs its own `ThreadLocal` (with a C-array log buffer and optional workbuffer) from the specified `Threads` enum identifier.

**Dispatch Model**: `Wake()` accepts a `std::move_only_function<void()>` and signals the worker via `std::binary_semaphore`. `Wait()` blocks until the dispatched work completes. The calling thread tracks dispatch state to make `Wait()` a no-op when no work is pending.

**Lifecycle**: The worker thread blocks on `mWake.acquire()` between dispatches. Destructor sets a shutdown flag and wakes the thread for clean join. `mThread` is declared last to ensure all other members are initialized before the thread starts.

Used by Graphics (`mRenderFuture` for async render), CommandBufferManager (`mSubmitGlobal`, `mSubmitMain` for async queue submission), SwapchainManager (`mPresent` for async presentation), and Multithreading (worker pool for parallel dispatch).

### Platform Utilities
- **Timer.h**: High-resolution `std::chrono` timer with nanosecond precision
- **ScopedLambda.h**: RAII scope-exit lambda execution for cleanup operations
- **WindowsUtils.h/.cpp**: Error string conversion, process execution with stdout capture, core count detection (logical and physical), registry access
