#pragma once

namespace common
{


// Debug verification helper that compares two values for equality and triggers a debug breakpoint if they differ (when kbVerifyFrame is enabled)
// Used for frame-to-frame state validation to detect inconsistencies
// Parameters: one, two - Values to compare for equality
// Returns: true if values are equal, false otherwise
inline constexpr bool kbVerifyFrame = true;

template<typename T>
inline bool BreakOnNotEqual(const T& rOne, const T& rTwo)
{
	bool bEqual = (rOne == rTwo);
	if constexpr (kbVerifyFrame)
	{
		if (!bEqual) [[unlikely]]
		{
			DEBUG_BREAK();
		}
	}
	return bEqual;
}

// Returns the minimum absolute value while preserving the sign of the first parameter
// Used for clamping velocity changes while maintaining direction
// Parameters: fA - Value whose sign is preserved, fB - Maximum absolute value
// Returns: Value with sign of fA and minimum absolute value of fA and fB
constexpr float MinAbs(float fA, float fB)
{
	return fA >= 0.0f ? std::min(fA, fB) : -std::min(-fA, fB);
}

// Compile-time ceiling function that rounds up to the nearest integer
// Used for constant calculations requiring compile-time evaluation
// Parameters: f - Float value to round up
// Returns: Smallest integer greater than or equal to f
consteval int64_t Ceil(float f)
{
	return static_cast<float>(static_cast<int64_t>(f)) == f ? static_cast<int64_t>(f) : static_cast<int64_t>(f) + ((f > 0.0f) ? 1 : 0);
}

// Converts nanosecond durations to floating-point seconds
// Used for delta time calculations in the game loop
// Parameters: nanoseconds - Duration to convert, FLOAT_TYPE - Target floating-point type (float or double)
// Returns: Time in seconds as specified floating-point type
template<typename FLOAT_TYPE>
constexpr FLOAT_TYPE NanosecondsToFloatSeconds(std::chrono::nanoseconds nanoseconds)
{
	return std::chrono::duration_cast<std::chrono::duration<FLOAT_TYPE, std::ratio<1, 1>>>(nanoseconds).count();
}

// Converts packed RGBA uint32_t color to XMVECTOR with normalized components (0-1 range)
// Format: RGBA with 8 bits per channel (0xRRGGBBAA)
// Parameters: uiColor - Packed color value
// Returns: XMVECTOR with components in range [0.0, 1.0]
inline XMVECTOR XM_CALLCONV ColorToVector(uint32_t uiColor)
{
	static constexpr float kfMultiplier = 1.0f / 255.0f;
	return XMVectorSet(kfMultiplier * static_cast<float>(uiColor >> 24), kfMultiplier * static_cast<float>((uiColor & 0x00FF0000) >> 16), kfMultiplier * static_cast<float>((uiColor & 0x0000FF00) >> 8), kfMultiplier * static_cast<float>(uiColor & 0x000000FF));
}

// Converts XMVECTOR color to packed RGBA uint32_t (inverse of ColorToVector)
// Components are clamped to [0.0, 1.0] range before packing
// Parameters: vecColor - XMVECTOR color with normalized components
// Returns: Packed RGBA color (0xRRGGBBAA)
inline uint32_t XM_CALLCONV ColorToUint(FXMVECTOR vecColor)
{
	XMFLOAT4A f4Color {};
	XMStoreFloat4A(&f4Color, vecColor);

	static constexpr float kfMultiplier = 255.0f;
	return static_cast<uint32_t>(kfMultiplier * f4Color.x) << 24 | static_cast<uint32_t>(kfMultiplier * f4Color.y) << 16 | static_cast<uint32_t>(kfMultiplier * f4Color.z) << 8 | static_cast<uint32_t>(kfMultiplier * f4Color.w);
}

// Linear interpolation between two packed RGBA colors by a given percentage
// Parameters: uiA - Start color, uiB - End color, fPercent - Interpolation factor [0.0, 1.0]
// Returns: Interpolated color
inline uint32_t ColorLerp(uint32_t uiA, uint32_t uiB, float fPercent)
{
	return ColorToUint(XMVectorLerp(ColorToVector(uiA), ColorToVector(uiB), fPercent));
}

using crc_t = uint64_t;

// Compile-time CRC hash function for string hashing
// Used extensively for asset identification and lookup throughout the codebase
// Note: Custom hash algorithm, not standard CRC32/64
// Parameters: pData - String to hash
// Returns: 64-bit hash value
constexpr crc_t Crc(std::string_view pData)
{
	crc_t crc = 0xabcdef123456789a;
	for (const char& rC : pData)
	{
		crc = (crc ^ rC) * 0x123456789abcdef1;
	}
	return crc;
}

// Runtime overload for hashing binary data (void* + size)
// Parameters: pData - Pointer to data to hash, iDataSize - Size in bytes
// Returns: 64-bit hash value
inline crc_t Crc(const void* pData, int64_t iDataSize)
{
	return Crc(std::string_view(static_cast<const char*>(pData), iDataSize));
}

// Concept to exclude string-like types from template Crc
// Prevents ambiguous overload resolution by excluding types convertible to string_view
template<typename T>
concept NotStringLike = !std::is_convertible_v<T, std::string_view>;

// Generic hash function for trivially copyable types by reinterpreting bytes
// Excludes string-like types to avoid ambiguous overload with Crc(std::string_view)
// Parameters: rIn - Trivially copyable object to hash
// Returns: 64-bit hash value
template<typename T>
	requires NotStringLike<T>
inline crc_t XM_CALLCONV Crc(const T& rIn)
{
	static_assert(std::is_trivially_copyable_v<T>, "Type must be trivially copyable to hash by byte reinterpretation");
	return Crc(std::string_view(reinterpret_cast<const char*>(&rIn), sizeof(rIn)));
}

// Converts wide string (UTF-16) to UTF-8 narrow string using standard library codecvt
// Parameters: pcWideChars - Wide string to convert
// Returns: UTF-8 encoded string
inline std::string ToString(std::wstring_view pcWideChars)
{
	std::wstring_convert<std::codecvt_utf8<wchar_t>, wchar_t> convert;
	return convert.to_bytes(pcWideChars.data());
}

// Converts UTF-32 string to UTF-8 with special handling for empty strings (returns "null")
// Parameters: pcUnicodeChars - UTF-32 string to convert
// Returns: UTF-8 encoded string or "null" if input is empty
inline std::string ToString(std::u32string_view pcUnicodeChars)
{
	if (pcUnicodeChars.size() == 0)
	{
		return "null";
	}

	std::wstring_convert<std::codecvt_utf8<char32_t>, char32_t> convert;
	return convert.to_bytes(pcUnicodeChars.data());
}

// Converts narrow string to wide string
// Note: Simple character-by-character conversion, not proper UTF-8 to UTF-16
// Parameters: pcChars - Narrow string to convert
// Returns: Wide string
inline std::wstring ToWstring(std::string_view pcChars)
{
	return std::wstring(pcChars.begin(), pcChars.end());
}

// Converts narrow string to UTF-32 string
// Note: Simple character-by-character conversion, not proper UTF-8 to UTF-32
// Parameters: pcChars - Narrow string to convert
// Returns: UTF-32 string
inline std::u32string ToU32string(std::string_view pcChars)
{
	return std::u32string(pcChars.begin(), pcChars.end());
}

// Splits a string into a vector of substrings based on a delimiter
// Generic template works with any string type (std::string, std::wstring, etc.)
// Parameters: rString - String to split, rDelimiter - Delimiter to split on
// Returns: Vector of substrings
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

// Compile-time integer-to-string conversion
// Used for generating compile-time CRC arrays with numbered suffixes
// Parameters: i - Integer to convert (must be positive)
// Returns: String representation of the integer
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

// Synchronization helper that waits for all futures in a vector to complete
// Used for parallel task execution and ensures all tasks finish before proceeding
// Parameters: futures - Vector of futures to wait for (will be consumed)
inline void WaitAll(std::vector<std::future<void>>& futures)
{
	for (std::future<void>& future : futures)
	{
		future.get();
	}
}

// Calculates memory size in bytes for a texture given its Vulkan format and dimensions
// Supports both compressed formats (BC4, BC7) and uncompressed formats (R8, RGBA8, RGBA16F, etc.)
// Parameters: vkFormat - Vulkan texture format, iWidth - Width in pixels, iHeight - Height in pixels
// Returns: Size in bytes required for the texture
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

// Compile-time generation of CRC hash arrays with sequential numbering
// Generates array of CRCs for strings like "prefix0suffix", "prefix1suffix", etc.
// Used for creating lookup tables of related asset names
// Template parameter: SIZE - Number of elements in the array
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

// Returns the total size in bytes of a vector's contents (size * sizeof(T))
// Used for calculating buffer sizes and memory usage
// Parameters: rVector - Vector to calculate size for
// Returns: Total size in bytes
template<typename T>
int64_t VectorByteSize(const std::vector<T>& rVector)
{
	return rVector.size() * sizeof(T);
}

// Converts float to string with specified decimal precision by substring truncation
// Parameters: fValue - Float value to convert, iDecimals - Number of decimal places to include
// Returns: String representation with specified precision
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

// Converts string to lowercase using std::tolower
// Parameters: rIn - String to convert
// Returns: Lowercase version of the input string
inline std::string ToLower(const std::string& rIn)
{
	std::string out(rIn);
	std::transform(out.begin(), out.end(), out.begin(), [](unsigned char uc)	{ return static_cast<char>(std::tolower(uc)); });
	return out;
}

// Sanitizes file paths to be valid C++ variable names by removing special characters
// Removes: backslash, dot, space, brackets, hyphen, comma
// Parameters: rIn - Path string to sanitize
// Returns: Sanitized string suitable for use as a C++ variable name
inline std::string PathToCppVariable(const std::string& rIn)
{
	std::string out(rIn);
	out.erase(std::remove(out.begin(), out.end(), '\\'), out.end());
	out.erase(std::remove(out.begin(), out.end(), '.'), out.end());
	out.erase(std::remove(out.begin(), out.end(), ' '), out.end());
	out.erase(std::remove(out.begin(), out.end(), '['), out.end());
	out.erase(std::remove(out.begin(), out.end(), ']'), out.end());
	out.erase(std::remove(out.begin(), out.end(), '-'), out.end());
	out.erase(std::remove(out.begin(), out.end(), ','), out.end());
	return out;
}

// Helper function to get content from either a file path or string
template<typename T>
inline std::pair<bool, std::string> GetFileOrStringContent(const T& rSource)
{
	if constexpr (std::is_same_v<std::decay_t<T>, std::string>)
	{
		// Source is already a string
		return {true, rSource};
	}
	else if constexpr (std::is_same_v<std::decay_t<T>, std::filesystem::path>)
	{
		if (!std::filesystem::exists(rSource))
		{
			return {false, {}};
		}
		
		// Pre-allocate string based on file size
		size_t fileSize = std::filesystem::file_size(rSource);
		std::string fileContents;
		fileContents.resize(fileSize);
		
		// Read file into string
		std::fstream fileStream(rSource, std::ios::in | std::ios::binary);
		fileStream.read(fileContents.data(), fileSize);
		fileStream.close();
		
		return {true, std::move(fileContents)};
	}
	else
	{
		static_assert(false, "GetFileOrStringContent only supports std::string and std::filesystem::path");
	}
}

// Template function to compare file contents, supporting both file paths and std::string
template<typename T1, typename T2>
inline bool ContentsEqual(const T1& rOne, const T2& rTwo)
{
	// Attempt to get content for both sources
	auto [oneValid, oneContent] = GetFileOrStringContent(rOne);
	auto [twoValid, twoContent] = GetFileOrStringContent(rTwo);
	
	// If either does not exist, return false
	if (!oneValid || !twoValid)
	{
		return false;
	}
	
	// Return true if equal
	return oneContent == twoContent;
}

// Stream write helper for single objects - eliminates reinterpret_cast boilerplate
// Used throughout the codebase for binary serialization
// Parameters: rStream - Output stream to write to, rValue - Object to write
template<typename T>
inline void Write(std::ostream& rStream, const T& rValue)
{
	rStream.write(reinterpret_cast<const char*>(&rValue), sizeof(T));
}

// Stream read helper for single objects
// Used throughout the codebase for binary deserialization
// Parameters: rStream - Input stream to read from, rValue - Object to read into
template<typename T>
inline void Read(std::istream& rStream, T& rValue)
{
	rStream.read(reinterpret_cast<char*>(&rValue), sizeof(T));
}

// Stream write helper for containers (vectors)
// Parameters: rStream - Output stream to write to, rVector - Vector to write
template<typename T>
inline void Write(std::ostream& rStream, const std::vector<T>& rVector)
{
	rStream.write(reinterpret_cast<const char*>(rVector.data()), VectorByteSize(rVector));
}

// Stream read helper for containers (vectors)
// Parameters: rStream - Input stream to read from, rVector - Vector to read into
template<typename T>
inline void Read(std::istream& rStream, std::vector<T>& rVector)
{
	rStream.read(reinterpret_cast<char*>(rVector.data()), VectorByteSize(rVector));
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
