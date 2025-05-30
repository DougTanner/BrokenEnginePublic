# /Engine/Source/Debug/

The `/Engine/Source//Debug/` directory contains helpers for BT_DEBUG builds.

## Files

### EnumToString.h
Provides utilities for converting Vulkan API enums to human-readable strings for debugging and logging purposes.

#### Key Features
- **Vulkan Enum Mapping**: Comprehensive string mappings for major Vulkan enums:
  - `VkColorSpaceKHR` - Color space formats
  - `VkDebugReportFlagsEXT` - Debug report flag types
  - `VkFormat` - Pixel formats (comprehensive list including BC, ASTC, ETC2, PVRTC formats)
  - `VkPresentModeKHR` - Presentation modes
  - `VkResult` - Vulkan API result codes
- **Global Instance**: Provides `gEnumToString` global instance for easy access
- **Utility Functions**: 
  - `VkResultToChar()` - Converts VkResult to string with fallback to numeric representation
- **Modern C++ Support**: Includes `std::formatter` specialization for `VkResult` to enable use with `std::format`

#### Usage
```cpp
// Convert VkResult to string for logging
VkResult result = vkCreateDevice(...);
const char* resultStr = VkResultToChar(result);

// Use with std::format (C++20)
std::string message = std::format("Vulkan operation failed: {}", result);

// Access enum maps directly
auto colorSpaceStr = gEnumToString.mVkColorSpaceKHRToStringMap[colorSpace];
```

## Purpose
This directory supports debugging and diagnostics for Vulkan API interactions, providing human-readable representations of Vulkan enums and results to aid in development and troubleshooting.
