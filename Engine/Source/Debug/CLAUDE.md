# `/Engine/Source/Debug/` - Client-Only Vulkan Debug Utilities

Vulkan development utilities for human-readable enum-to-string conversions used in logging and error messages. Included only in `BT_CLIENT` builds via `Engine.h`.

**Global**: `engine::gEnumToString` (inline global instance of `EnumToString`)

## Key Classes

- **EnumToString** - Converts Vulkan enum values (VkResult, VkFormat, VkColorSpaceKHR, VkPresentModeKHR, VkObjectType) to human-readable strings for logging and error messages. `Convert(EnumType, Workbuffer&)` returns a `ScopedWorkbufferPop` (using `PushBuffer` for scratch storage in non-logging builds instead of a `thread_local` buffer).

## Architecture Notes

Uses compile-time type dispatch (`std::is_same_v`) within a single templated `Convert()` method to select the appropriate lookup map based on enum type. Guarded by `if constexpr (kbEnableLogging)` so all lookup maps and string logic are eliminated at compile time in non-logging builds.

Includes a `std::formatter<VkResult>` specialization enabling direct use of VkResult values in `std::format()` and `Log()` calls.

Triggers debug break on unmapped enum values to catch missing lookup entries during development.
