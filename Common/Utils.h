#pragma once

namespace common
{

inline constexpr bool kbVerifyFrame = false;

inline void BreakOnNotEqual(bool bEqual)
{
	if constexpr (kbVerifyFrame)
	{
		if (!bEqual) [[unlikely]]
		{
			DEBUG_BREAK();
		}
	}
}

template<typename T, size_t SIZE>
bool Equal(const T(&pOne)[SIZE], const T(&pTwo)[SIZE])
{
	bool bEqual = true;

	for (int64_t i = 0; i < SIZE; ++i)
	{
		bEqual &= pOne[i] == pTwo[i];
		common::BreakOnNotEqual(bEqual);
	}

	return bEqual;
}

constexpr float MinAbs(float fA, float fB)
{
	return fA >= 0.0f ? std::min(fA, fB) : -std::min(-fA, fB);
}

constexpr float MaxAbs(float fA, float fB)
{
	return fA >= 0.0f ? std::max(fA, fB) : -std::max(-fA, fB);
}

consteval int64_t Ceil(float f)
{
	return static_cast<float>(static_cast<int64_t>(f)) == f ? static_cast<int64_t>(f) : static_cast<int64_t>(f) + ((f > 0.0f) ? 1 : 0);
}

template<typename FLOAT_TYPE>
constexpr FLOAT_TYPE NanosecondsToFloatSeconds(std::chrono::nanoseconds nanoseconds)
{
	return std::chrono::duration_cast<std::chrono::duration<FLOAT_TYPE, std::ratio<1, 1>>>(nanoseconds).count();
}

inline DirectX::XMVECTOR XM_CALLCONV ColorToVector(uint32_t uiColor)
{
	static constexpr float kfMultiplier = 1.0f / 255.0f;
	return DirectX::XMVectorSet(kfMultiplier * static_cast<float>(uiColor >> 24), kfMultiplier * static_cast<float>((uiColor & 0x00FF0000) >> 16), kfMultiplier * static_cast<float>((uiColor & 0x0000FF00) >> 8), kfMultiplier * static_cast<float>(uiColor & 0x000000FF));
}

inline uint32_t XM_CALLCONV ColorToUint(DirectX::FXMVECTOR vecColor)
{
	DirectX::XMFLOAT4A f4Color {};
	DirectX::XMStoreFloat4A(&f4Color, vecColor);

	static constexpr float kfMultiplier = 255.0f;
	return static_cast<uint32_t>(kfMultiplier * f4Color.x) << 24 | static_cast<uint32_t>(kfMultiplier * f4Color.y) << 16 | static_cast<uint32_t>(kfMultiplier * f4Color.z) << 8 | static_cast<uint32_t>(kfMultiplier * f4Color.w);
}

inline uint32_t ColorLerp(uint32_t uiA, uint32_t uiB, float fPercent)
{
	return ColorToUint(DirectX::XMVectorLerp(ColorToVector(uiA), ColorToVector(uiB), fPercent));
}

using crc_t = uint64_t;

constexpr crc_t Crc(std::string_view pData)
{
	crc_t crc = 0xabcdef123456789a;
	for (const char& c : pData)
	{
		crc = (crc ^ c) * 0x123456789abcdef1;
	}
	return crc;
}

template <typename T>
void MemOr(T& rOut, const T& rInOne, const T& rInTwo)
{
	static_assert(sizeof(T) % sizeof(uint64_t) == 0);

	int64_t iInts = sizeof(T) / sizeof(uint64_t);
	std::span outSpan(reinterpret_cast<uint64_t*>(&rOut), iInts);
	std::span inOneSpan(reinterpret_cast<const uint64_t*>(&rInOne), iInts);
	std::span inTwoSpan(reinterpret_cast<const uint64_t*>(&rInTwo), iInts);

	for (int64_t i = 0; i < iInts; ++i)
	{
		outSpan[i] = inOneSpan[i] | inTwoSpan[i];
	}
}

inline std::string ToString(std::wstring_view pcWideChars)
{
	std::wstring_convert<std::codecvt_utf8<wchar_t>, wchar_t> convert;
	return convert.to_bytes(pcWideChars.data());
}

inline std::string ToString(std::u32string_view pcUnicodeChars)
{
	if (pcUnicodeChars.size() == 0)
	{
		return "null";
	}

	std::wstring_convert<std::codecvt_utf8<char32_t>, char32_t> convert;
	return convert.to_bytes(pcUnicodeChars.data());
}

inline std::wstring ToWstring(std::string_view pcChars)
{
	return std::wstring(pcChars.begin(), pcChars.end());
}

inline std::u32string ToU32string(std::string_view pcChars)
{
	return std::u32string(pcChars.begin(), pcChars.end());
}

template <size_t U> int64_t Count(const bool(&pbArray)[U])
{
	int64_t iCount = 0;
	for (bool bCount : pbArray)
	{
		iCount += bCount ? 1 : 0;
	}
	return iCount;
}

template<typename T>
std::vector<T> Split(const T& rString, const T& rDelimiter)
{
	int64_t iStart = 0;
	int64_t iEnd = 0;
	int64_t iDelimiterLength = rDelimiter.length();
	T token;

	std::vector<T> splits;
	while ((iEnd = rString.find(rDelimiter, iStart)) != T::npos)
	{
		token = rString.substr(iStart, iEnd - iStart);
		iStart = iEnd + iDelimiterLength;
		splits.push_back(token);
	}
	splits.push_back(rString.substr(iStart));
	return splits;
}

constexpr std::string IntToString(int64_t i)
{
	std::string string;

	do
	{
		int64_t digit = i % 10;
		i = i / 10;
		string.push_back(static_cast<char>(digit) + '0');
	}
	while (i > 0);

	std::reverse(string.begin(), string.end());
	return string;
}

inline void WaitAll(std::vector<std::future<void>>& futures)
{
	for (std::future<void>& future : futures)
	{
		future.get();
	}
}

inline int64_t SizeInBytes(VkFormat vkFormat, int64_t iWidth, int64_t iHeight)
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
			return 2 * iPixels;

		case VK_FORMAT_R8G8B8A8_SRGB:
		case VK_FORMAT_R8G8B8A8_UNORM:
		case VK_FORMAT_R16G16_UNORM:
		case VK_FORMAT_R32_SFLOAT:
			return 4 * iPixels;

		case VK_FORMAT_R16G16B16A16_SFLOAT:
		case VK_FORMAT_R32G32_SFLOAT:
			return 8 * iPixels;

		default:
			DEBUG_BREAK();
			return 0;
	}
}

template<int64_t SIZE>
struct ConstexprCrcArray
{
	int64_t miCount = SIZE;
	crc_t mArray[SIZE];

	constexpr ConstexprCrcArray(const char* pcPrefix, const char* pcSuffix)
	{
		for (int64_t i = 0; i < SIZE; ++i)
		{
			mArray[i] = Crc(std::string(pcPrefix) + IntToString(i) + std::string(pcSuffix));
		}
	}

	crc_t operator[](int64_t i) const
	{
		return mArray[i];
	}
};

template<typename T>
int64_t VectorByteSize(const std::vector<T>& rVector)
{
	return rVector.size() * sizeof(T);
}

inline std::string FromFloat(float fValue, int64_t iDecimals)
{
	return std::to_string(fValue).substr(0, std::to_string(fValue).find(".") + iDecimals + 1);
}

// https://stackoverflow.com/a/50821858
inline std::wstring GetStringValueFromHKLM(const std::wstring& rRegSubKey, const std::wstring& rRegValue)
{
	size_t uiBufferSize = 0xFFF;
	std::wstring valueBuf;
	valueBuf.resize(uiBufferSize);
	DWORD uiCbData = static_cast<DWORD>(uiBufferSize * sizeof(wchar_t));
	LSTATUS iRc = RegGetValueW(HKEY_LOCAL_MACHINE, rRegSubKey.c_str(), rRegValue.c_str(), RRF_RT_REG_SZ, nullptr, static_cast<void*>(valueBuf.data()), &uiCbData);

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

		iRc = RegGetValueW(HKEY_LOCAL_MACHINE, rRegSubKey.c_str(), rRegValue.c_str(), RRF_RT_REG_SZ, nullptr, static_cast<void*>(valueBuf.data()), &uiCbData);
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

#include "DataFile.h"
#include "Flags.h"
#include "Log.h"
#include "MathUtils.h"
#include "StackWalker.h"
#include "Random.h"
#include "ScopedLambda.h"
#include "Smoothed.h"
#include "ThreadLocal.h"
#include "Timer.h"
#include "WindowsUtils.h"
