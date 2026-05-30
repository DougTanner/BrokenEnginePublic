#pragma once

namespace common
{

// Zero-allocation hex conversion. Writes "0x" + uppercase hex digits + null terminator.
// Returns buffer data pointer for convenience. Fixed-extent span enables compile-time size verification.
template<std::unsigned_integral T, size_t N>
char* ToHex(std::span<char, N> pcBuffer, T uiValue)
{
	static_assert(N >= 2 + sizeof(T) * 2 + 1, "Buffer too small for ToHex output");
	constexpr char kacDigits[] = "0123456789ABCDEF";
	pcBuffer[0] = '0';
	pcBuffer[1] = 'x';
	constexpr int64_t kiDigits = sizeof(T) * 2;
	for (int64_t i = kiDigits - 1; i >= 0; --i)
	{
		pcBuffer[2 + i] = kacDigits[uiValue & 0xF];
		uiValue >>= 4;
	}
	pcBuffer[2 + kiDigits] = '\0';
	return pcBuffer.data();
}

// Converts wide string (UTF-16) to UTF-8 narrow string using Win32 WideCharToMultiByte
// Parameters: wideChars - Wide string to convert
// Returns: UTF-8 encoded string
std::string ToString(std::wstring_view wideChars);

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

// Converts ASCII 'A'-'Z' to lowercase (locale-independent; all other bytes unchanged)
// Parameters: rIn - String to convert
// Returns: Lowercase version of the input string
std::string ToLower(std::string_view chars);

// Sanitizes file paths to be valid C++ variable names by removing special characters
// Removes: backslash, dot, space, brackets, hyphen, comma
// Parameters: rIn - Path string to sanitize
// Returns: Sanitized string suitable for use as a C++ variable name
std::string PathToCppVariable(std::string_view path);

} // namespace common
