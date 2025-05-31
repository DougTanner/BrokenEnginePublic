# /Engine/Source/Debug/

The `/Engine/Source/Debug/` directory contains debug utilities for converting Vulkan enums to human-readable strings, improving error messages and logging.

## Files

### EnumToString.h
Vulkan enum-to-string conversion utilities.

#### Features
- **Conditional Compilation**: Only available when `ENABLE_LOGGING` is defined
- **EnumToString Class**: Contains unordered_map lookups for:
  - `VkColorSpaceKHR` - Color space formats (sRGB, Display P3, HDR10, etc.)
  - `VkDebugReportFlagsEXT` - Debug report severity levels
  - `VkFormat` - All Vulkan pixel formats including:
    - Standard formats (R8G8B8A8, etc.)
    - Compressed formats (BC1-7, ETC2, ASTC, PVRTC)
    - Depth/stencil formats
  - `VkPresentModeKHR` - Swap chain presentation modes
  - `VkResult` - API return codes for error handling

#### Global Utilities
- `gEnumToString` - Global instance for direct map access
- `VkResultToChar()` - Converts VkResult to string, falls back to numeric value when `ENABLE_LOGGING` is disabled
- C++20 `std::formatter<VkResult>` specialization for use with `std::format`

#### Example Usage
```cpp
// Check Vulkan operation result
VkResult result = vkCreateDevice(...);
LOG_ERROR("Device creation failed: {}", result);  // Uses formatter

// Direct enum lookup
auto format = gEnumToString.mVkFormatToStringMap[VK_FORMAT_BC7_SRGB_BLOCK];
```
