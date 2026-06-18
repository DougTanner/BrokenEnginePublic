#include "TextureFormat.h"

namespace common
{

int64_t SizeInBytes(VkFormat vkFormat, int64_t iWidth, int64_t iHeight)
{
	int64_t iPixels = iWidth * iHeight;
	switch (vkFormat)
	{
		// BC formats are stored as 4x4 blocks. Round dims up to block size so sub-block mips report the true on-disk size (e.g. 2x2 BC4 is one 8-byte block, not 2 bytes). Identical to the legacy `iPixels / [2|1]` formulas for any (w, h) that's a multiple of 4 (which is what the DataPacker currently emits — see `MakeMipmaps` 4x4 cap), so this is a strict superset.
		case VK_FORMAT_BC4_UNORM_BLOCK:
			return ((iWidth + 3) / 4) * ((iHeight + 3) / 4) * 8;

		case VK_FORMAT_BC5_UNORM_BLOCK:
		case VK_FORMAT_BC7_UNORM_BLOCK:
			return ((iWidth + 3) / 4) * ((iHeight + 3) / 4) * 16;

		case VK_FORMAT_R8_UNORM:
			return iPixels;

		case VK_FORMAT_R8G8_UNORM:
		case VK_FORMAT_R16_UNORM:
		case VK_FORMAT_R16_SFLOAT:
			return 2 * iPixels;

		case VK_FORMAT_R8G8B8A8_SRGB:
		case VK_FORMAT_R8G8B8A8_UNORM:
		case VK_FORMAT_R16G16_UNORM:
		case VK_FORMAT_R32_SFLOAT:
		case VK_FORMAT_R16G16_SFLOAT:
		case VK_FORMAT_B8G8R8A8_UNORM:
			return 4 * iPixels;

		case VK_FORMAT_R16G16B16A16_SFLOAT:
		case VK_FORMAT_R32G32_SFLOAT:
			return 8 * iPixels;

		default:
			ASSERT(false);
			return 4 * iPixels;
	}
}

int64_t ComputeImageByteSize(VkFormat vkFormat, int64_t iWidth, int64_t iHeight, int64_t iMipLevels, int64_t iArrayLayers, int64_t iDepth)
{
	int64_t iTotalSize = 0;
	int64_t iMipWidth = iWidth;
	int64_t iMipHeight = iHeight;
	for (int64_t i = 0; i < iMipLevels; ++i)
	{
		iTotalSize += iArrayLayers * iDepth * SizeInBytes(vkFormat, iMipWidth, iMipHeight);
		iMipWidth = std::max(iMipWidth / 2, static_cast<int64_t>(1));
		iMipHeight = std::max(iMipHeight / 2, static_cast<int64_t>(1));
	}
	return iTotalSize;
}

} // namespace common
