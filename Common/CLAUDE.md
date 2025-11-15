# /Common/

The `/Common/` directory contains shared utilities and data format specifications used across DataPacker, Engine, and Projects. This is the foundation layer with no dependencies outside the codebase.

## Overview

All code is in `namespace common`. The directory provides:
- **Data Formats**: Binary file format specifications and vertex structures
- **Core Utilities**: Logging, profiling, debugging, and error handling
- **Math & Algorithms**: 3D math helpers, random number generation, CRC hashing
- **Platform Abstractions**: Threading, timing, Windows utilities

## Core Files

### DataFile.h
Defines the engine's custom binary data format (`.pack` files). Specifies chunk-based file structure with headers for different asset types (fonts, models, shaders, textures, islands, audio, glTF). Includes vertex format definitions and material structures. Uses 16-byte alignment for all file chunks.

### Defines.h
Core macros used throughout the codebase for debugging (`DEBUG_BREAK`, `ASSERT`), logging (`LOG`, `SCOPED_LOG_INDENT`), error checking (`CHECK_HRESULT`, `CHECK_VK`, `VERIFY_SUCCESS`), Vulkan object naming, and profiling (`CPU_PROFILE`, `GPU_PROFILE`, `BOOT_TIMER`). Macros are conditionally compiled based on build configuration.

### ExternalHeaders.h
Central include file for all external dependencies. Included in pre-compiled headers and available in all C++ files. Manages include order and dependencies. Uses Volk meta-loader for Vulkan integration.

### Flags.h
Type-safe bitfield template class for enum-based bit flags. Provides operators for setting, clearing, testing, and toggling flags while maintaining type safety.

### Log.h
Thread-safe logging system with per-thread buffers. Outputs to debugger (`OutputDebugString`) and optional file stream. Supports hierarchical indentation for structured logging. Uses custom formatters from LogFormatters.h for DirectX Math and Vulkan types.

### LogFormatters.h
Custom formatters enabling `LOG()` macro to format complex types including DirectX Math types (`XMFLOAT3`, `XMFLOAT4`, `XMVECTOR`) and Vulkan enums.

### MathUtils.h & MathUtils.cpp
3D math helpers using DirectX Math library. Includes vector/matrix operations for rotation, direction calculation, distance, and projection. Provides quad/area calculations with point-in-polygon testing. Contains utility templates for rounding, normalized value conversion, and gamma correction.

### Random.h
Deterministic random number generator for reproducible simulations. Supports seeding and saving/restoring state via equality comparison. Provides both custom `RandomEngine` and standard library `std::mt19937` wrapper functions.

### ScopedLambda.h
RAII utility for guaranteed cleanup on scope exit. Executes provided lambda when object is destroyed, ensuring cleanup even during exception unwinding.

### Smoothed.h
Value smoothing utilities for time-based averaging. `InTheLastSecond` tracks occurrences in a rolling 1-second window. `Smoothed<T, COUNT>` provides running averages over configurable time windows. Used for FPS counters and performance metrics.

### StackWalker.h
Stack trace utilities for debugging and crash reporting (Engine builds only). Provides logging and file output for stack traces using external StackWalker library.

### ThreadLocal.h & ThreadLocal.cpp
Per-thread data storage management. Each thread gets a thread ID, name, 1MB log buffer, and 16MB work buffer. Avoids heap allocation and lock contention. Includes vectored exception handler for intercepting crashes and logging debug output exceptions.

### Timer.h
High-resolution timer for performance measurement using `std::chrono`. Simple interface: construct and call `GetDeltaNs()` to get elapsed time.

### Utils.h
General utility functions including:
- Debug helpers for array comparison
- Math utilities (rounding, time conversion)
- Color operations (packing, unpacking, interpolation)
- CRC hashing (compile-time `Crc()` function and `ConstexprCrcArray` for arrays)
- String conversions (Unicode, path sanitization, formatting)
- Memory utilities
- File comparison (`ContentsEqual()`)
- Texture size calculation for all Vulkan formats (compressed, uncompressed, depth/stencil)
- Threading (`WaitAll()` for futures)

### WindowsUtils.h
Windows-specific platform utilities for error handling, process management, system info, time formatting, and registry access.
