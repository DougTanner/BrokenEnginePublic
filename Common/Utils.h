#pragma once

namespace common
{

// Empty struct for use with [[no_unique_address]] and std::conditional_t
// Allows conditional member elimination at compile time with zero storage overhead
struct Empty {};

// Compile-time string wrapper for use as non-type template parameter (C++20 NTTP)
// Enables passing string literals directly as template arguments
// Template parameter: N - Size of the string including null terminator
template<size_t N>
struct FixedString
{
	char data[N] {};

	constexpr FixedString(const char (&str)[N])
	{
		for (size_t i = 0; i < N; ++i)
		{
			data[i] = str[i];
		}
	}

	constexpr operator const char*() const { return data; }
};


// Debug verification helper that compares two values for equality and triggers a debug breakpoint if they differ (when kbVerifyFrame is enabled)
// Used for frame-to-frame state validation to detect inconsistencies
// Parameters: one, two - Values to compare for equality
// Returns: true if values are equal, false otherwise
// IMPORTANT: Uses byte-level comparison for floating-point types to match Crc() behavior
// (floating-point == treats -0.0f == +0.0f, but their byte representations differ)
inline constexpr bool kbVerifyFrame = true;

extern void DebugBreak();

template<typename T>
inline bool BreakOnNotEqual(const T& rOne, const T& rTwo)
{
	bool bEqual = false;
	if constexpr (std::is_same_v<T, XMFLOAT2> || std::is_same_v<T, XMFLOAT3> || std::is_same_v<T, XMFLOAT4> || std::is_same_v<T, XMFLOAT4A>)
	{
		// Byte-level comparison to match Crc() behavior (floating-point == treats -0.0f == +0.0f)
		bEqual = std::memcmp(&rOne, &rTwo, sizeof(T)) == 0;
	}
	else
	{
		bEqual = rOne == rTwo;
	}

	if constexpr (kbVerifyFrame)
	{
		if (!bEqual) [[unlikely]]
		{
			common::DebugBreak();
		}
	}
	return bEqual;
}

// XMVECTOR overload - stores to XMFLOAT4 for byte-level comparison to match Crc() behavior
inline bool XM_CALLCONV BreakOnNotEqual(FXMVECTOR rOne, FXMVECTOR rTwo)
{
	XMFLOAT4 f4One, f4Two;
	XMStoreFloat4(&f4One, rOne);
	XMStoreFloat4(&f4Two, rTwo);
	bool bEqual = std::memcmp(&f4One, &f4Two, sizeof(XMFLOAT4)) == 0;

	if constexpr (kbVerifyFrame)
	{
		if (!bEqual) [[unlikely]]
		{
			common::DebugBreak();
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
#pragma warning(suppress: 26497) // consteval is stricter than constexpr
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

// Compile-time only CRC hash function - forces compile-time evaluation
// Causes compiler error if used with runtime values
// Parameters: pData - String to hash (must be a compile-time constant)
// Returns: 64-bit hash value
#pragma warning(suppress: 26497) // consteval is stricter than constexpr
consteval crc_t CrcConsteval(std::string_view pData)
{
	crc_t crc = 0xabcdef123456789a;
	for (const char& rC : pData)
	{
		crc = (crc ^ rC) * 0x123456789abcdef1;
	}
	return crc;
}
static_assert(Crc("test") == CrcConsteval("test"), "CRC functions must produce identical results");

// Template overload for hashing arrays (pointer + count)
// Parameters: pValues - Pointer to array to hash, uiCount - Number of elements
// Returns: 64-bit hash value
template<typename T>
inline crc_t Crc(const T* pValues, int64_t uiCount)
{
	static_assert(std::is_trivially_copyable_v<T>, "Type must be trivially copyable");
	return Crc(std::string_view(reinterpret_cast<const char*>(pValues), uiCount * sizeof(T)));
}

// Concept to exclude string-like types from template Crc
// Prevents ambiguous overload resolution by excluding types convertible to string_view
template<typename T>
concept NotStringLike = !std::is_convertible_v<T, std::string_view>;

// Generic hash function for trivially copyable types by reinterpreting bytes
// Excludes string-like types to avoid ambiguous overload with Crc(std::string_view)
// Parameters: rIn - Trivially copyable object to hash
// Returns: 64-bit hash value
template<typename T> requires NotStringLike<T>
inline crc_t XM_CALLCONV Crc(const T& rIn)
{
	static_assert(std::is_trivially_copyable_v<T>, "Type must be trivially copyable to hash by byte reinterpretation");
	return Crc(std::string_view(reinterpret_cast<const char*>(&rIn), sizeof(rIn)));
}

// XMVECTOR overload - stores to XMFLOAT4 for consistent hashing
inline crc_t XM_CALLCONV Crc(FXMVECTOR vecIn)
{
	XMFLOAT4 f4Temp;
	XMStoreFloat4(&f4Temp, vecIn);
	return Crc(f4Temp);
}

// Converts wide string (UTF-16) to UTF-8 narrow string using standard library codecvt
// Parameters: wideChars - Wide string to convert
// Returns: UTF-8 encoded string
inline std::string ToString(std::wstring_view wideChars)
{
	std::wstring_convert<std::codecvt_utf8<wchar_t>, wchar_t> convert;
	return convert.to_bytes(wideChars.data());
}

// Converts UTF-32 string to UTF-8 with special handling for empty strings (returns "null")
// Parameters: unicodeChars - UTF-32 string to convert
// Returns: UTF-8 encoded string or "null" if input is empty
inline std::string ToString(std::u32string_view unicodeChars)
{
	if (unicodeChars.size() == 0)
	{
		return "null";
	}

	std::wstring_convert<std::codecvt_utf8<char32_t>, char32_t> convert;
	return convert.to_bytes(unicodeChars.data());
}

// Converts narrow string to wide string
// Note: Simple character-by-character conversion, not proper UTF-8 to UTF-16
// Parameters: chars - Narrow string to convert
// Returns: Wide string
inline std::wstring ToWstring(std::string_view chars)
{
	return std::wstring(chars.begin(), chars.end());
}

// Converts narrow string to UTF-32 string
// Note: Simple character-by-character conversion, not proper UTF-8 to UTF-32
// Parameters: chars - Narrow string to convert
// Returns: UTF-32 string
inline std::u32string ToU32string(std::string_view chars)
{
	return std::u32string(chars.begin(), chars.end());
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
			common::DebugBreak();
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

	consteval ConstexprCrcArray(const char* pcPrefix, const char* pcSuffix)
	{
		for (int64_t i = 0; i < SIZE; ++i)
		{
			mArray[i] = CrcConsteval(std::string(pcPrefix) + IntToString(i) + std::string(pcSuffix));
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

// Reads a string value from the Windows registry (HKEY_LOCAL_MACHINE)
// https://stackoverflow.com/a/50821858
std::wstring GetStringValueFromHKLM(const std::wstring& rRegSubKey, const std::wstring& rRegValue);

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
	auto [oneValid, oneContent] = GetFileOrStringContent(rOne);
	auto [twoValid, twoContent] = GetFileOrStringContent(rTwo);

	// Return false if either source does not exist or could not be read
	if (!oneValid || !twoValid)
	{
		return false;
	}
	
	// Return true if equal
	return oneContent == twoContent;
}

// Stream read helper for single objects
// Used throughout the codebase for binary deserialization
// Parameters: rStream - Input stream to read from, rValue - Object to read into
template<typename T>
inline void Read(std::istream& rStream, T& rValue)
{
	static_assert(std::is_trivially_copyable_v<T>, "Type must be trivially copyable");
	rStream.read(reinterpret_cast<char*>(&rValue), sizeof(T));
}

template<typename T>
inline void Read(std::istream& rStream, T* pValues, uint64_t uiCount)
{
	static_assert(std::is_trivially_copyable_v<T>, "Type must be trivially copyable");
	rStream.read(reinterpret_cast<char*>(pValues), uiCount * sizeof(T));
}

// Stream read helper for containers (vectors)
// Parameters: rStream - Input stream to read from, rVector - Vector to read into
template<typename T>
inline void Read(std::istream& rStream, std::vector<T>& rVector)
{
	rStream.read(reinterpret_cast<char*>(rVector.data()), VectorByteSize(rVector));
}

// Stream write helper for single objects - eliminates reinterpret_cast boilerplate
// Used throughout the codebase for binary serialization
// Parameters: rStream - Output stream to write to, rValue - Object to write
template<typename T>
inline void Write(std::ostream& rStream, const T& rValue)
{
	static_assert(std::is_trivially_copyable_v<T>, "Type must be trivially copyable");
	rStream.write(reinterpret_cast<const char*>(&rValue), sizeof(T));
}

template<typename T>
inline void Write(std::ostream& rStream, T* pValues, uint64_t uiCount)
{
	static_assert(std::is_trivially_copyable_v<T>, "Type must be trivially copyable");
	rStream.write(reinterpret_cast<const char*>(pValues), uiCount * sizeof(T));
}

// Stream write helper for containers (vectors)
// Parameters: rStream - Output stream to write to, rVector - Vector to write
template<typename T>
inline void Write(std::ostream& rStream, const std::vector<T>& rVector)
{
	rStream.write(reinterpret_cast<const char*>(rVector.data()), VectorByteSize(rVector));
}

// XMVECTOR overloads - store/load via XMFLOAT4 for consistent serialization
inline void XM_CALLCONV Write(std::ostream& rStream, FXMVECTOR vecValue)
{
	XMFLOAT4 f4Temp;
	XMStoreFloat4(&f4Temp, vecValue);
	Write(rStream, f4Temp);
}

inline void Read(std::istream& rStream, XMVECTOR& rVecValue)
{
	XMFLOAT4 f4Temp;
	Read(rStream, f4Temp);
	rVecValue = XMLoadFloat4(&f4Temp);
}

// Custom deleter for aligned memory allocated with _aligned_malloc
// Automatically calls _aligned_free when std::unique_ptr is destroyed
// Used with std::unique_ptr to provide RAII for aligned memory allocations
struct AlignedDeleter
{
	void operator()(void* p) const noexcept
	{
		_aligned_free(p);
	}
};

// Type alias for std::unique_ptr with aligned memory management
// Provides RAII semantics for 64-byte aligned allocations required for SIMD operations
// Template parameter: T - Element type for the array
template<typename T>
using AlignedUniquePtr = std::unique_ptr<T[], AlignedDeleter>;

// Factory function for creating aligned memory with custom alignment
// Allocates memory aligned to specified boundary using _aligned_malloc
// Throws std::bad_alloc if allocation fails
// Parameters: uiCount - Number of elements to allocate, iAlignment - Alignment boundary in bytes
// Returns: AlignedUniquePtr managing the allocated memory
template<typename T>
AlignedUniquePtr<T> MakeAligned(int64_t uiCount)
{
	return AlignedUniquePtr<T>(static_cast<T*>(_aligned_malloc(uiCount * sizeof(T), 64)));
}

} // namespace common

#include "DataFile.h"
#include "ErrorUtils.h"
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
