#pragma once

namespace common
{

// Calculates memory size in bytes for a texture given its Vulkan format and dimensions.
// The switch in SizeInBytes is the authoritative set of supported VkFormats (BCn block-compressed + uncompressed); an unsupported format is a fatal ASSERT, not a guessed size.
// Precondition: iWidth/iHeight are positive and iWidth * iHeight * bytesPerPixel fits in int64_t (signed overflow is UB). Runtime callers derive dimensions from Vulkan-bounded extents, so this holds.
// Parameters: vkFormat - Vulkan texture format, iWidth - Width in pixels, iHeight - Height in pixels
// Returns: Size in bytes required for the texture
int64_t SizeInBytes(VkFormat vkFormat, int64_t iWidth, int64_t iHeight);

} // namespace common
