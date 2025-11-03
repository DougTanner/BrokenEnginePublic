# /Common/

The `/Common/` directory contains shared utilities and data format specifications used by DataPacker, Engine, and Projects. This is the foundation layer with no dependencies.

## Overview

All code is in `namespace common`. The directory contains:
- **Data Formats**: Binary file specifications and vertex structures
- **Core Utilities**: Logging, profiling, debugging, and error handling
- **Math & Algorithms**: 3D math, random numbers, CRC hashing
- **Platform Abstractions**: Threading, timing, Windows utilities

## Core Files

### DataFile.h
- Defines the engine's custom binary data format specification
- Key constants: `kiAlignmentBytes = 16` for file alignment
- Core structures for file format:
  - `ChunkLocation` - Maps CRC hashes to file offsets/sizes
  - `ChunkFlags` - Bitfield for asset types (Model, Shader, Texture sub-flags)
  - `ChunkHeader` - File chunk metadata with magic number validation
  - `DataHeader` - Main file header with version control
- Asset-specific headers: `FontHeader`, `GltfHeader`, `ModelHeader`, `ShaderHeader`, `TextureHeader`, `IslandHeader`, `AudioHeader`
- Vertex formats: `VertexPos`, `VertexPosNorm`, `VertexPosTex`, `VertexPosNormTex`, `GltfVertex`
- Material data: `GltfShaderData` for PBR properties
- Font data: `Character` structure for font metrics

### Defines.h
- Core macros used throughout the codebase
- Debug utilities: `DEBUG_BREAK()`, `ASSERT(condition)`
- Logging macros: `LOG()`, `LOG_INDENT()`, `SCOPED_LOG_INDENT()`
- Error checking: `CHECK_HRESULT(hr)`, `CHECK_VK(result)`, `VERIFY_SUCCESS(condition)`
- Vulkan debugging: `VK_NAME(object, name)` for naming objects in debug builds
- Profiling macros:
  - Boot timers: `BOOT_TIMER_START/STOP()`, `SCOPED_BOOT_TIMER()`
  - CPU profiling: `CPU_PROFILE_START/STOP()`, `SCOPED_CPU_PROFILE()`
  - GPU profiling: `GPU_PROFILE_START/STOP/READ()`
  - Profile display: `PROFILE_TOGGLE_TEXT()`, `UPDATE_PROFILE_TEXT()`

### ExternalHeaders.h
- Central include file for ALL external dependencies
- Included in pre-compiled headers, available in all C++ files
- Manages include order and dependencies for external libraries

### Flags.h
- Type-safe bitfield template class
- Usage: `Flags<MyEnum> flags;` for enum-based bit flags
- Provides operators for bitwise operations while maintaining type safety

### Log.h
- Thread-safe logging system with per-thread buffers
- Outputs to debugger (OutputDebugString) and optional file
- Supports hierarchical indentation for structured logging
- Global instance: `gpLog` (main logger)

### LogFormatters.h
- Custom formatters for DirectX and Vulkan types
- Supports formatting of:
  - `std::wstring` - Wide string conversion
  - DirectX Math types: `XMFLOAT3`, `XMFLOAT4`, `XMFLOAT4A`, `XMVECTOR`
  - Vulkan enums: `VkFilter`, `VkSamplerAddressMode`
- Enables LOG() macro to format complex types

### MathUtils.h & MathUtils.cpp
- Extensive 3D math helpers using DirectX Math
- Data structures:
  - `AreaVertices` - Four vertices defining a quad with `Center()` method
- Vector/Matrix operations:
  - `Project()`, `ToBaseHeight()` - Vector projection
  - `RotationFromPosition()`, `QuaternionFromDirection()`, `RotationMatrixFromDirection()`
  - `Closest()` - Find closest point on line segment
  - `CalculateArea()` - Calculate quad vertices from position/direction
  - `InsideAreaVertices()` - Point-in-polygon test
  - `RotateTowards()`, `RotateTowardsPercent()` - Rotation interpolation
  - `CircleJitter()` - Random position within circle
  - `DirectionTo()`, `Distance()` - Basic vector operations
- Utility templates:
  - `RoundUp()`, `RoundDown()` - Rounding to multiples
  - `FloatToUnorm()`, `UnormToFloat()` - Normalized value conversion
  - `FromGamma()` - Gamma correction

### Random.h
- Deterministic random number generator for reproducible simulations
- `RandomEngine` struct with seed support
- Random generation functions:
  - `Random(uint32_t max, RandomEngine&)` - Integer in range [0, max]
  - `Random<float MAX>(RandomEngine&)` - Float in range [0, MAX]
  - `UniformRandom()` templates for std::mt19937
- Supports save states via equality comparison

### ScopedLambda.h
- RAII utility for guaranteed cleanup on scope exit
- Executes provided lambda when object is destroyed
- Usage: `auto cleanup = common::ScopedLambda([&] { /* cleanup code */ });`
- Ensures cleanup even during exception unwinding

### Smoothed.h
- Value smoothing utilities for time-based averaging
- Classes:
  - `InTheLastSecond` - Tracks occurrences in rolling 1-second window
  - `Smoothed<T, COUNT>` - Running average over configurable time window
- Used for FPS counters and performance metrics
- Methods: `Get()`, `Max()`, `Current()`, `Average()`, `Update()`

### StackWalker.h
- Stack trace utilities for debugging and crash reporting (Engine builds only)
- `LogStackWalker` - Outputs stack traces to engine logging system
- `OfstreamStackWalker` - Writes stack traces to file stream
- Based on external StackWalker library

### ThreadLocal.h & ThreadLocal.cpp
- Per-thread data storage management
- Each thread gets:
  - Thread ID and name for logging
  - 1MB log buffer for thread-safe logging
  - 16MB work buffer for temporary allocations
- Functions: `GetThreadLogBuffer()`, `GetThreadWorkBuffer()`
- Avoids heap allocation and lock contention

### Timer.h
- High-resolution timer for performance measurement
- Simple interface: construct, then `GetElapsedTime<Duration>()`
- Template-based duration support (milliseconds, microseconds, etc.)
- Uses std::chrono for cross-platform timing

### Utils.h
- General utility functions collection
- Debug helpers: `BreakOnNotEqual()`, `Equal()` for array comparison
- Math utilities: `MinAbs()`, `MaxAbs()`, `Ceil()`, time conversions
- Color operations: `ColorToVector()`, `ColorToUint()`, `ColorLerp()`
- CRC hashing: `Crc()` compile-time CRC32, `ConstexprCrcArray` for arrays
- String conversions: Unicode conversions, path sanitization, formatting
- Memory utilities: `MemOr()`, `Count()`, `VectorByteSize()`
- File operations: `FileContentsEqual()`, texture size calculation
- Texture utilities: `SizeInBytes()` - calculates memory size for all Vulkan texture formats
  - Supports block-compressed formats (BC1-BC7, ETC2, EAC, ASTC)
  - Supports uncompressed formats (8-bit to 64-bit per channel, 1-4 channels)
  - Supports packed formats (RGB565, RGBA4444, RGB5A1, RGB10A2, etc.)
  - Supports depth/stencil formats (D16, D24, D32, D24S8, D32S8, D16S8)
- Threading: `WaitAll()` for multiple futures

### WindowsUtils.h
- Windows-specific platform utilities
- Error handling: `LastErrorString()`, `HresultToString()`
- Process management: `RunExecutable()`
- System info: `LogicalCoreCount()`, `HardwareCoreCount()`
- Time formatting: `FileTimeToU32String()`
- Registry access: `GetStringValueFromHKLM()`
