#pragma once

namespace common
{

// Calculates memory size in bytes for a texture given its Vulkan format and dimensions.
// The switch in SizeInBytes is the authoritative set of supported VkFormats (BCn block-compressed + uncompressed); an unsupported format is a fatal ASSERT, not a guessed size.
// Precondition: iWidth/iHeight are positive and iWidth * iHeight * bytesPerPixel fits in int64_t (signed overflow is UB). Runtime callers derive dimensions from Vulkan-bounded extents, so this holds.
// Parameters: vkFormat - Vulkan texture format, iWidth - Width in pixels, iHeight - Height in pixels
// Returns: Size in bytes required for the texture
int64_t SizeInBytes(VkFormat vkFormat, int64_t iWidth, int64_t iHeight);

// Total byte size of a full mip chain across all array layers and depth slices: sums SizeInBytes per mip with each dimension halved and clamped at 1 (never 0).
// Single source of truth for texture staging / readback / cache size math so the size computation and the data that fills it cannot drift. Parameters mirror Vulkan image extent / mip / layer counts.
int64_t ComputeImageByteSize(VkFormat vkFormat, int64_t iWidth, int64_t iHeight, int64_t iMipLevels, int64_t iArrayLayers, int64_t iDepth);

} // namespace common
