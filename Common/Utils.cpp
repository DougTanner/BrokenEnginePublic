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
		Log(kLogNetwork, "LogDifferences {} {} Client: {} Server: {}", gpLogDifferenceContext, pcName, f4One, f4Two);

	return bEqual;
}

bool XM_CALLCONV LogDifference_Vec(const char* pcName, int64_t iIndex, FXMVECTOR rOne, FXMVECTOR rTwo)
{
	XMFLOAT4 f4One, f4Two;
	XMStoreFloat4(&f4One, rOne);
	XMStoreFloat4(&f4Two, rTwo);
	bool bEqual = std::memcmp(&f4One, &f4Two, sizeof(XMFLOAT4)) == 0;

	if (!bEqual) [[unlikely]]
		Log(kLogNetwork, "LogDifferences {} {}[{}] Client: {} Server: {}", gpLogDifferenceContext, pcName, iIndex, f4One, f4Two);

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

XMVECTOR XM_CALLCONV ColorToVector(uint32_t uiColor)
{
	static constexpr float kfMultiplier = 1.0f / 255.0f;
	return XMVectorSet(kfMultiplier * static_cast<float>(uiColor >> 24), kfMultiplier * static_cast<float>((uiColor & 0x00FF0000) >> 16), kfMultiplier * static_cast<float>((uiColor & 0x0000FF00) >> 8), kfMultiplier * static_cast<float>(uiColor & 0x000000FF));
}

uint32_t XM_CALLCONV ColorToUint(FXMVECTOR vecColor)
{
	XMFLOAT4A f4Color {};
	XMStoreFloat4A(&f4Color, vecColor);

	static constexpr float kfMultiplier = 255.0f;
	return static_cast<uint32_t>(kfMultiplier * f4Color.x) << 24 | static_cast<uint32_t>(kfMultiplier * f4Color.y) << 16 | static_cast<uint32_t>(kfMultiplier * f4Color.z) << 8 | static_cast<uint32_t>(kfMultiplier * f4Color.w);
}

uint32_t ColorLerp(uint32_t uiA, uint32_t uiB, float fPercent)
{
	return ColorToUint(XMVectorLerp(ColorToVector(uiA), ColorToVector(uiB), fPercent));
}

std::string ToString(std::wstring_view wideChars)
{
	std::wstring_convert<std::codecvt_utf8<wchar_t>, wchar_t> convert;
	return convert.to_bytes(wideChars.data());
}

std::string ToString(std::u32string_view unicodeChars)
{
	if (unicodeChars.size() == 0)
	{
		return "null";
	}

	std::wstring_convert<std::codecvt_utf8<char32_t>, char32_t> convert;
	return convert.to_bytes(unicodeChars.data());
}

std::wstring ToWstring(std::string_view chars)
{
	return std::wstring(chars.begin(), chars.end());
}

std::u32string ToU32string(std::string_view chars)
{
	return std::u32string(chars.begin(), chars.end());
}

void WaitAll(std::vector<std::future<void>>& futures)
{
	for (std::future<void>& future : futures)
	{
		future.get();
	}
}

std::string FromFloat(float fValue, int64_t iDecimals)
{
	std::string str = std::to_string(fValue);
	return str.substr(0, str.find(".") + iDecimals + 1);
}

std::string ToLower(std::string_view in)
{
	std::string out(in);
	std::transform(out.begin(), out.end(), out.begin(), [](unsigned char uc)	{ return static_cast<char>(std::tolower(uc)); });
	return out;
}

std::string PathToCppVariable(std::string_view in)
{
	std::string out(in);
	out.erase(std::remove(out.begin(), out.end(), '\\'), out.end());
	out.erase(std::remove(out.begin(), out.end(), '.'), out.end());
	out.erase(std::remove(out.begin(), out.end(), ' '), out.end());
	out.erase(std::remove(out.begin(), out.end(), '['), out.end());
	out.erase(std::remove(out.begin(), out.end(), ']'), out.end());
	out.erase(std::remove(out.begin(), out.end(), '-'), out.end());
	out.erase(std::remove(out.begin(), out.end(), ','), out.end());
	return out;
}

} // namespace common
