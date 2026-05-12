#include "Utils.h"

namespace common
{

bool XM_CALLCONV LogDifference_Vec(const char* pcName, FXMVECTOR rOne, FXMVECTOR rTwo)
{
	XMFLOAT4 f4One, f4Two;
	XMStoreFloat4(&f4One, rOne);
	XMStoreFloat4(&f4Two, rTwo);
	bool bEqual = std::memcmp(&f4One, &f4Two, sizeof(XMFLOAT4)) == 0;

	if (!bEqual) [[unlikely]]
		LOG(kNetwork, kError, "LogDifferences {} {} Client: {} Server: {}", gpLogDifferenceContext, pcName, f4One, f4Two);

	return bEqual;
}

bool XM_CALLCONV LogDifference_Vec(const char* pcName, int64_t iIndex, FXMVECTOR rOne, FXMVECTOR rTwo)
{
	XMFLOAT4 f4One, f4Two;
	XMStoreFloat4(&f4One, rOne);
	XMStoreFloat4(&f4Two, rTwo);
	bool bEqual = std::memcmp(&f4One, &f4Two, sizeof(XMFLOAT4)) == 0;

	if (!bEqual) [[unlikely]]
		LOG(kNetwork, kError, "LogDifferences {} {}[{}] Client: {} Server: {}", gpLogDifferenceContext, pcName, iIndex, f4One, f4Two);

	return bEqual;
}

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
			DEBUG_BREAK();
			return 4 * iPixels;
	}
}

std::string ToString(std::wstring_view wideChars)
{
	if (wideChars.empty())
		return {};

	int iSize = WideCharToMultiByte(CP_UTF8, 0, wideChars.data(), static_cast<int>(wideChars.size()), nullptr, 0, nullptr, nullptr);
	std::string result(iSize, '\0');
	WideCharToMultiByte(CP_UTF8, 0, wideChars.data(), static_cast<int>(wideChars.size()), result.data(), iSize, nullptr, nullptr);
	return result;
}

std::string ToLower(std::string_view chars)
{
	std::string out(chars);
	std::transform(out.begin(), out.end(), out.begin(), [](unsigned char uc)	{ return static_cast<char>(std::tolower(uc)); });
	return out;
}

std::string PathToCppVariable(std::string_view path)
{
	std::string out(path);
	std::erase_if(out, [](char c) { return c == '\\' || c == '.' || c == ' ' || c == '[' || c == ']' || c == '-' || c == ','; });
	return out;
}

} // namespace common
