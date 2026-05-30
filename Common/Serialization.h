#pragma once

namespace common
{

// Returns the total size in bytes of a vector's contents (size * sizeof(T))
// Used for calculating buffer sizes and memory usage
// Parameters: rVector - Vector to calculate size for
// Returns: Total size in bytes
template<typename T>
int64_t VectorByteSize(const std::vector<T>& rVector)
{
	return rVector.size() * sizeof(T);
}

// Stream read helper for single objects
// Used throughout the codebase for binary deserialization
// Excludes XMVECTOR so the dedicated Read(std::istream&, XMVECTOR&) overload below always wins —
// removing it becomes a compile error here, not a silent raw-__m128 read.
// Determinism contract: reads the raw object representation verbatim (native sizeof/endianness,
// x64-only); padding/float bytes round-trip as-is. Pass padding-free or zeroed POD types.
// Parameters: rStream - Input stream to read from, rValue - Object to read into
template<typename T> requires (!std::is_same_v<std::remove_cvref_t<T>, XMVECTOR>)
inline void Read(std::istream& rStream, T& rValue)
{
	static_assert(std::is_trivially_copyable_v<T>, "Type must be trivially copyable");
	rStream.read(reinterpret_cast<char*>(&rValue), sizeof(T));
}

template<typename T>
inline void Read(std::istream& rStream, T* pValues, int64_t iCount)
{
	static_assert(std::is_trivially_copyable_v<T>, "Type must be trivially copyable");
	rStream.read(reinterpret_cast<char*>(pValues), iCount * sizeof(T));
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
// Excludes XMVECTOR so the dedicated Write(std::ostream&, FXMVECTOR) overload below always wins —
// removing it becomes a compile error here, not a silent raw-__m128 write.
// Determinism contract: writes the raw object representation verbatim (native sizeof/endianness,
// x64-only); padding/float bytes are persisted as-is. Pass padding-free or zeroed POD types.
// Parameters: rStream - Output stream to write to, rValue - Object to write
template<typename T> requires (!std::is_same_v<std::remove_cvref_t<T>, XMVECTOR>)
inline void Write(std::ostream& rStream, const T& rValue)
{
	static_assert(std::is_trivially_copyable_v<T>, "Type must be trivially copyable");
	rStream.write(reinterpret_cast<const char*>(&rValue), sizeof(T));
}

template<typename T>
inline void Write(std::ostream& rStream, T* pValues, int64_t iCount)
{
	static_assert(std::is_trivially_copyable_v<T>, "Type must be trivially copyable");
	rStream.write(reinterpret_cast<const char*>(pValues), iCount * sizeof(T));
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

} // namespace common
