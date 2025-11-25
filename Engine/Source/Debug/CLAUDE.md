# `/Engine/Source/Debug/`

Debug utilities for Vulkan development. Only compiled when `ENABLE_LOGGING` is defined.

## EnumToString.h

Converts Vulkan enum values to human-readable strings for logging. Global singleton `gEnumToString` with templated `Convert()` method uses compile-time type dispatch (`std::is_same_v`) to select lookup maps for VkResult, VkFormat, VkColorSpaceKHR, VkPresentModeKHR, VkObjectType, and VkDebugReportFlagsEXT.

Includes `std::formatter<VkResult>` specialization for direct use in `std::format()` and LOG macros.

Triggers debug break on unmapped enum values to catch missing entries during development.
