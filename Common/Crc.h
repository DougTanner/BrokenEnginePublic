#pragma once

namespace common
{

using crc_t = uint64_t;

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

// Compile-time integer-to-string conversion
// Used for generating compile-time CRC arrays with numbered suffixes
// Parameters: i - Integer to convert (must be positive)
// Returns: String representation of the integer
constexpr std::string IntToString(int64_t i)
{
	std::string string;

	do
	{
		int64_t iDigit = i % 10;
		i = i / 10;
		string.push_back(static_cast<char>(iDigit) + '0');
	}
	while (i > 0);

	std::reverse(string.begin(), string.end());
	return string;
}

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
// Parameters: pValues - Pointer to array to hash, iCount - Number of elements
// Returns: 64-bit hash value
template<typename T>
inline crc_t Crc(const T* pValues, int64_t iCount)
{
	static_assert(std::is_trivially_copyable_v<T>, "Type must be trivially copyable");
	return Crc(std::string_view(reinterpret_cast<const char*>(pValues), iCount * sizeof(T)));
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

// XORs a value's CRC into two checksums at once (used by Crcs() methods)
template<typename T> requires NotStringLike<T>
void Crc(const T& rValue, crc_t& rCrc, crc_t& rSharedCrc)
{
	crc_t c = Crc(rValue);
	rCrc ^= c;
	rSharedCrc ^= c;
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

} // namespace common
