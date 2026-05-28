#pragma once

namespace common
{

// Calculates memory size in bytes for a texture given its Vulkan format and dimensions
// Supports both compressed formats (BC4, BC7) and uncompressed formats (R8, RGBA8, RGBA16F, etc.)
// Parameters: vkFormat - Vulkan texture format, iWidth - Width in pixels, iHeight - Height in pixels
// Returns: Size in bytes required for the texture
int64_t SizeInBytes(VkFormat vkFormat, int64_t iWidth, int64_t iHeight);

} // namespace common
