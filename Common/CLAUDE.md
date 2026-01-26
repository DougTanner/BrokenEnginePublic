# /Common/

Shared utilities and data format specifications used across DataPacker, Engine, and Projects. Foundation layer with no dependencies outside the codebase. All code is in `namespace common`.

## Architecture

### Binary Data Format (DataFile.h)
Defines `.pack` file format with 16-byte aligned chunks. Each chunk has a `ChunkHeader` with type flags and a union of type-specific headers for fonts, models, shaders, textures, terrain islands, audio, and glTF assets. Vertex classes define GPU vertex formats for various attribute combinations. Includes skeletal animation structures: skeleton hierarchy with joint bind poses and inverse bind matrices, animation clips with named channels, keyframe data for translation/rotation/scale interpolation.

### Warning Suppressions (Defines.h)
Disables specific compiler and code analysis warnings that conflict with the codebase style.

### Error Handling (ErrorUtils.h/.cpp)
Pure C++20 functions using `std::source_location` for call site information:
- **Assert()**: Condition validation with automatic file/line capture
- **CheckHresult()**: Windows HRESULT validation with error string lookup
- **VerifySuccess()**: Boolean validation with `GetLastError()` reporting
- **DebugBreak()**: Conditional debugger breakpoint (only when debugger attached and `kbEnableDebugBreak` is true)

Inline wrapper functions check conditions and call out-of-line `[[noreturn]]` failure handlers for optimal code generation.

### Profiling Utilities (Defines.h)
RAII classes and inline functions for performance profiling:
- **ScopedBootTimer**: RAII wrapper for boot-time measurements
- **ScopedCpuProfile**: RAII wrapper for CPU timing sections
- **ProfileSetCount()**: Inline function for setting counter values

All profiling utilities use `if constexpr (kbEnableProfiling)` for compile-time elimination when profiling is disabled. ProfileManager methods are called directly via `gpProfileManager->Method()` (gpProfileManager is always valid).

### External Dependencies (ExternalHeaders.h)
Central include for all external libraries. Configures DirectX Math for SSE4 only (no AVX for determinism). Adds comparison operators for XMFLOAT types. Conditionally includes DirectXTK (audio, gamepad), PerlinNoise, and StackWalker for engine builds. Uses Volk meta-loader for Vulkan.

### Thread-Local Storage (ThreadLocal.h/.cpp)
`ThreadLocal` class provides per-thread log buffer and reusable work buffer. Engine builds install vectored exception handlers for crash logging and stack traces. Avoids heap allocation and lock contention in hot paths.

### Logging (Log.h, LogFormatters.h)
Thread-safe logging via per-thread buffers using `if constexpr (kbEnableLogging)` for compile-time elimination when disabled. Each project defines `kbEnableLogging` in its Pch.h. Provides `Log()`, `LogIndent()`, and `ScopedLogIndent` (RAII indent helper). Outputs to `OutputDebugString` and optional file stream. Custom `std::formatter` specializations for DirectX Math types, filesystem paths, and Vulkan enums.

## Key Utilities

### CRC Hashing (Utils.h/.cpp)
Compile-time string hashing for asset identification. `Crc()` is constexpr (works at compile-time or runtime), while `CrcConsteval()` forces compile-time evaluation with a compiler error if used with runtime values. Overloads for trivially copyable types and arrays. `ConstexprCrcArray` generates numbered hash sequences at compile time for related asset name lookups.

### Type-Safe Flags (Flags.h)
`Flags<ENUM_TYPE>` template wraps enum bitfields with type-safe operators. Fully constexpr-compatible for use as compile-time template parameters. Supports serialization and CRC generation for replay verification.

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
`InTheLastSecond` tracks event counts in rolling 1-second window. `Smoothed<T, COUNT>` provides running averages for metrics display.

### Platform Utilities
- **Timer.h**: High-resolution `std::chrono` timer with nanosecond precision
- **ScopedLambda.h**: RAII scope-exit lambda execution for cleanup operations
- **WindowsUtils.h/.cpp**: Error string conversion, process execution with stdout capture, core count detection (logical and physical), registry access
