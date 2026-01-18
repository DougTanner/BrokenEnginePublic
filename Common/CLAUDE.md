# /Common/

Shared utilities and data format specifications used across DataPacker, Engine, and Projects. Foundation layer with no dependencies outside the codebase. All code is in `namespace common`.

## Architecture

### Binary Data Format (DataFile.h)
Defines `.pack` file format with 16-byte aligned chunks. Each chunk has a `ChunkHeader` with type flags and a union of type-specific headers for fonts, models, shaders, textures, terrain islands, audio, and glTF assets. Vertex classes define GPU vertex formats for various attribute combinations.

### Macro System (Defines.h)
Conditionally-compiled macros for:
- **Debugging**: `DEBUG_BREAK`, `ASSERT`
- **Error checking**: `CHECK_HRESULT`, `CHECK_VK`, `VERIFY_SUCCESS`
- **Logging**: `LOG`, `SCOPED_LOG_INDENT`
- **Profiling**: `CPU_PROFILE`, `GPU_PROFILE`, `SCOPED_BOOT_TIMER`
- **Vulkan naming**: `VK_NAME` (debug layers only)

`CHECK_VK` handles Vulkan device lost and swapchain recreation by setting `gpGraphics->meDestroyType`.

### External Dependencies (ExternalHeaders.h)
Central include for all external libraries. Configures DirectX Math for SSE4 only (no AVX for determinism). Adds comparison operators for XMFLOAT types. Conditionally includes DirectXTK (audio, gamepad), PerlinNoise, and StackWalker for engine builds. Uses Volk meta-loader for Vulkan.

### Thread-Local Storage (ThreadLocal.h/.cpp)
`ThreadLocal` class provides per-thread log buffer and reusable work buffer. Engine builds install vectored exception handlers for crash logging and stack traces. Avoids heap allocation and lock contention in hot paths.

### Logging (Log.h, LogFormatters.h)
Thread-safe logging via per-thread buffers. Outputs to `OutputDebugString` and optional file stream. Custom `std::formatter` specializations for DirectX Math types, filesystem paths, and Vulkan enums.

## Key Utilities

### CRC Hashing (Utils.h)
Compile-time string hashing for asset identification. `Crc()` is constexpr (works at compile-time or runtime), while `CrcConsteval()` forces compile-time evaluation with a compiler error if used with runtime values. Overloads for trivially copyable types and arrays. `ConstexprCrcArray` generates numbered hash sequences at compile time for related asset name lookups.

### Type-Safe Flags (Flags.h)
`Flags<ENUM_TYPE>` template wraps enum bitfields with type-safe operators. Fully constexpr-compatible for use as compile-time template parameters. Supports serialization and CRC generation for replay verification.

### Debug Verification (Utils.h)
`BreakOnNotEqual()` compares two values for equality and triggers a debug breakpoint if they differ (when `kbVerifyFrame` is enabled). Uses byte-level comparison (memcmp) for XMFLOAT types and XMVECTOR to match Crc() behavior, ensuring replay verification detects differences like -0.0f vs +0.0f that floating-point == would miss.

### Deterministic RNG (Random.h)
`RandomEngine` struct using Xorshift64 algorithm with 64-bit state for fast, reproducible simulations. Provides seed constructor, time-based seeding, and state comparison for replay verification. Two `Random()` overloads: integer version returns `uint32_t` in range [0, max], float version returns value in range [0, MAX) with compile-time MAX parameter. Also wraps `std::mt19937` via `UniformRandom()` for standard library compatibility.

### Math Helpers (MathUtils.h/.cpp)
DirectX Math wrappers for rotation, direction, distance, and quaternion operations. Area/quad calculations with point-in-polygon testing. AABB computation and intersection tests. Rounding templates with compile-time power-of-2 optimization. Frame-rate independent exponential decay and interpolation using Pade approximation. Random jitter utilities for XY offset, position jitter, and direction jitter with both compile-time template and runtime parameter variants.

### Binary I/O (Utils.h)
`Write()`/`Read()` template functions for trivially copyable types, arrays, and vectors. Eliminates reinterpret_cast boilerplate throughout serialization code.

### Aligned Memory (Utils.h)
`AlignedUniquePtr<T>` and `MakeAligned<T>()` for 64-byte aligned SIMD allocations with RAII cleanup.

### Performance Smoothing (Smoothed.h)
`InTheLastSecond` tracks event counts in rolling 1-second window. `Smoothed<T, COUNT>` provides running averages for metrics display.

### Platform Utilities
- **Timer.h**: High-resolution `std::chrono` timer with nanosecond precision
- **ScopedLambda.h**: RAII scope-exit lambda execution for cleanup operations
- **WindowsUtils.h**: Error string conversion, process execution with stdout capture, core count detection (logical and physical), registry access
