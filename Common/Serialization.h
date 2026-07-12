#pragma once

namespace common
{

// Thrown by deserialization readers when a count/size/capacity field from a trust boundary
// (save file, replay stream, network payload, .pack chunk) is implausible — negative, inverted
// (count > capacity), or larger than the stream/chunk could possibly back. Caught at each
// load/receive boundary (logged + payload skipped or load aborted) so malformed input degrades
// gracefully instead of overrunning a buffer or driving an unbounded allocation. The message is a
// static reader-name literal (no std::format on the throw path; std::runtime_error's own string copy is
// benign — every thrower runs under a load-path allocation-suppress scope).
class CorruptStreamException : public std::runtime_error
{
public:
	explicit CorruptStreamException(const char* pcReader)
		: std::runtime_error(pcReader)
	{
	}
};

// Generous absolute ceiling on a deserialized SOA capacity. Far above any plausible per-collection
// element count, far below an allocation that would exhaust memory — converts a hostile capacity into
// a clean reject-and-log instead of a bad_alloc.
inline constexpr int64_t kiMaxDeserializedCapacity = 1 << 24; // 16,777,216 elements

// Companion byte ceiling for a deserialized SOA capacity. kiMaxDeserializedCapacity bounds the element
// count, but a large per-element stride (multi-hundred-byte SOA row) can still reserve multi-GB before the
// count is even read. 256 MiB caps the reservation regardless of stride.
inline constexpr int64_t kiMaxDeserializedBytes = 256 << 20; // 268,435,456 bytes

// Bytes between the stream's current get position and its end. Requires a seekable stream — every
// save/replay file stream and the network istringstream are. Restores the get position.
inline int64_t StreamBytesRemaining(std::istream& rStream)
{
	const std::streampos posCurrent = rStream.tellg();
	rStream.seekg(0, std::ios::end);
	const std::streampos posEnd = rStream.tellg();
	rStream.seekg(posCurrent);
	return static_cast<int64_t>(posEnd - posCurrent);
}

// Trust-boundary guard for a deserialized element count whose elements are read sequentially.
// iElementBytes is the minimum bytes one element consumes in the stream (>= 1); the count is bounded
// against the stream's remaining length so a hostile value cannot drive a large vector resize or loop.
// Divides (never multiplies) against the remaining length so the bound itself cannot overflow.
inline void ValidateDeserializedCount(int64_t iCount, int64_t iElementBytes, std::istream& rStream, const char* pcReader)
{
	if (iCount < 0 || iElementBytes <= 0 || iCount > StreamBytesRemaining(rStream) / iElementBytes)
	{
		throw CorruptStreamException(pcReader);
	}
}

// As ValidateDeserializedCount, plus an SOA capacity (allocation size) and the count <= capacity
// invariant. iCount elements are read from the stream (so it is stream-length bounded); iCapacity
// sizes the buffer, so it is bounded by the absolute ceiling rather than the stream length.
inline void ValidateDeserializedCountCapacity(int64_t iCount, int64_t iCapacity, int64_t iElementBytes, std::istream& rStream, const char* pcReader)
{
	// The byte-ceiling clause divides (never multiplies) so the bound cannot overflow; iElementBytes <= 0 is
	// left to ValidateDeserializedCount below, which throws on it.
	if (iCount < 0 || iCapacity < 0 || iCount > iCapacity || iCapacity > kiMaxDeserializedCapacity
		|| (iElementBytes > 0 && iCapacity > kiMaxDeserializedBytes / iElementBytes))
	{
		throw CorruptStreamException(pcReader);
	}
	ValidateDeserializedCount(iCount, iElementBytes, rStream, pcReader);
}

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
	static_assert(std::is_trivially_copyable_v<T>, "Vector element type must be trivially copyable");
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
	static_assert(std::is_trivially_copyable_v<T>, "Vector element type must be trivially copyable");
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
