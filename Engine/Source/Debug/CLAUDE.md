# Debug - Vulkan Development Utilities

## Overview

Debug utilities for Vulkan development, providing human-readable string conversions for Vulkan enum values. Only compiled when `ENABLE_LOGGING` is defined.

## Key Classes

- **EnumToString** - Converts Vulkan enum values to human-readable strings for logging and error messages. Accessed via global singleton `gEnumToString`.

## Architecture Notes

Uses compile-time type dispatch (`std::is_same_v`) to select the appropriate lookup map based on the enum type passed to `Convert()`. Supports VkResult, VkFormat, VkColorSpaceKHR, VkPresentModeKHR, VkObjectType, and VkDebugReportFlagsEXT.

Includes a `std::formatter<VkResult>` specialization enabling direct use of VkResult values in `std::format()` and LOG macros.

Triggers debug break on unmapped enum values to catch missing lookup entries during development.
