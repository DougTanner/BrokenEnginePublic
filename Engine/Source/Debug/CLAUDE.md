# `/Engine/Source/Debug/`

Vulkan enum-to-string conversion for improved error messages and logging.

## EnumToString.h

**Conditional**: Only compiled when `ENABLE_LOGGING` is defined

### Enum Mappings
- **VkColorSpaceKHR** - sRGB, Display P3, HDR10, etc.
- **VkDebugReportFlagsEXT** - Debug severity levels
- **VkFormat** - All pixel formats (standard, compressed BC/ETC/ASTC/PVRTC, depth/stencil)
- **VkPresentModeKHR** - Swap chain presentation modes
- **VkResult** - API return codes

### Global Access
- `gEnumToString` - Global instance with unordered_map lookups
- `VkResultToChar()` - String conversion with numeric fallback
- C++20 `std::formatter<VkResult>` specialization

### Usage
```cpp
// Error logging with formatter
VkResult result = vkCreateDevice(...);
LOG_ERROR("Device creation failed: {}", result);

// Direct lookup
auto format = gEnumToString.mVkFormatToStringMap[VK_FORMAT_BC7_SRGB_BLOCK];
```
