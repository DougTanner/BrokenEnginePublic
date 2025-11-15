# `/Engine/Source/Debug/`

Provides debug utilities for improved error messages and logging in Vulkan code.

## EnumToString.h

**Purpose**: Converts Vulkan enum values to human-readable strings for logging and error messages.

**Conditional Compilation**: Only compiled when `ENABLE_LOGGING` is defined to avoid overhead in release builds.

**Architecture**: Singleton pattern with global `gEnumToString` instance. Uses templated `Convert()` method with compile-time type checking (`std::is_same_v`) to select appropriate lookup maps for different Vulkan enum types.

**Integration with Logging**: Provides C++20 `std::formatter` specialization for `VkResult`, enabling direct use in format strings via `std::format()` and logging macros.

**Error Detection**: Triggers debug break when encountering unmapped enum values to catch missing entries during development.
