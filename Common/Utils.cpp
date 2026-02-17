#include "Utils.h"

namespace common
{

void VerifyFrameBreak()
{
	DEBUG_BREAK();
}

bool XM_CALLCONV BreakOnNotEqual(FXMVECTOR rOne, FXMVECTOR rTwo)
{
	XMFLOAT4 f4One, f4Two;
	XMStoreFloat4(&f4One, rOne);
	XMStoreFloat4(&f4Two, rTwo);
	bool bEqual = std::memcmp(&f4One, &f4Two, sizeof(XMFLOAT4)) == 0;

	if constexpr (kbVerifyFrame)
	{
		if (!bEqual) [[unlikely]]
		{
			DEBUG_BREAK();
		}
	}
	return bEqual;
}

int64_t SizeInBytes(VkFormat vkFormat, int64_t iWidth, int64_t iHeight)
{
	int64_t iPixels = iWidth * iHeight;
	switch (vkFormat)
	{
		case VK_FORMAT_BC4_UNORM_BLOCK:
			return iPixels / 2;

		case VK_FORMAT_BC7_UNORM_BLOCK:
		case VK_FORMAT_R8_UNORM:
			return iPixels;

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

// https://stackoverflow.com/a/50821858
std::wstring GetStringValueFromHKLM(const std::wstring& rRegSubKey, const std::wstring& rRegValue)
{
	size_t uiBufferSize = 0xFFF;
	std::wstring valueBuf;
	valueBuf.resize(uiBufferSize);
	DWORD uiCbData = static_cast<DWORD>(uiBufferSize * sizeof(wchar_t));
	LSTATUS iRc = RegGetValueW(HKEY_LOCAL_MACHINE, rRegSubKey.c_str(), rRegValue.c_str(), RRF_RT_REG_SZ, nullptr, valueBuf.data(), &uiCbData);

	while (iRc == ERROR_MORE_DATA)
	{
		uiCbData /= sizeof(wchar_t);

		if (uiCbData > static_cast<DWORD>(uiBufferSize))
		{
			uiBufferSize = static_cast<size_t>(uiCbData);
		}
		else
		{
			uiBufferSize *= 2;
			uiCbData = static_cast<DWORD>(uiBufferSize * sizeof(wchar_t));
		}

		valueBuf.resize(uiBufferSize);

		iRc = RegGetValueW(HKEY_LOCAL_MACHINE, rRegSubKey.c_str(), rRegValue.c_str(), RRF_RT_REG_SZ, nullptr, valueBuf.data(), &uiCbData);
	}

	if (iRc == ERROR_SUCCESS)
	{
		uiCbData /= sizeof(wchar_t);

		// Remove end null character
		valueBuf.resize(static_cast<size_t>(uiCbData - 1));

		return valueBuf;
	}
	else
	{
		throw std::runtime_error("Windows system error code: " + std::to_string(iRc));
	}
}

} // namespace common
