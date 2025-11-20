#pragma once

#include "Frame/Pools/PoolConfig.h"

namespace game
{

struct Frame;
struct FrameInput;
struct FrameInterpolate;
struct FramePostRender;

}

namespace engine
{

struct FrameBase;

using id_t = int64_t;

// ============================================================================
// LOW-LEVEL MEMORY ALIGNMENT HELPERS
// ============================================================================
// These functions handle pointer arithmetic and 64-byte alignment for SIMD optimization.
// They are internal building blocks used by higher-level allocation helpers.

// Aligns a pointer to a 64-byte boundary and advances the current position.
// Used during initial allocation to position member array pointers within a contiguous buffer.
// Template parameter T: Type of elements in the array being positioned.
// Parameters:
//   rpElements - Reference to pointer that will be assigned to aligned position
//   iCapacity - Number of elements to reserve space for
//   rpCurrent - Reference to current position in buffer (advanced after assignment)
template <typename T>
void AssignAligned(T*& rpElements, int64_t iCapacity, std::byte*& rpCurrent)
{
	rpCurrent = reinterpret_cast<std::byte*>(common::RoundUp<uintptr_t, 64>(reinterpret_cast<uintptr_t>(rpCurrent)));
	rpElements = reinterpret_cast<T*>(rpCurrent);
	rpCurrent += iCapacity * sizeof(rpElements[0]);
}

// Aligns a pointer, copies existing data, and advances the current position.
// Used during capacity growth to preserve existing elements while reallocating.
// Template parameter T: Type of elements in the array.
// Parameters:
//   rpElements - Reference to pointer with existing data (updated to new location)
//   iCapacity - New capacity (number of elements to reserve space for)
//   iCount - Number of existing elements to copy
//   rpCurrent - Reference to current position in new buffer (advanced after assignment)
template <typename T>
void AssignAndCopyAligned(T*& rpElements, int64_t iCapacity, int64_t iCount, std::byte*& rpCurrent)
{
	rpCurrent = reinterpret_cast<std::byte*>(common::RoundUp<uintptr_t, 64>(reinterpret_cast<uintptr_t>(rpCurrent)));
	if (rpElements != nullptr)
	{
		memcpy(rpCurrent, rpElements, iCount * sizeof(rpElements[0]));
	}
	rpElements = reinterpret_cast<T*>(rpCurrent);
	rpCurrent += iCapacity * sizeof(rpElements[0]);
}

// ============================================================================
// HIGH-LEVEL ALLOCATION & REALLOCATION HELPERS
// ============================================================================
// These functions orchestrate memory management for Structure-of-Arrays collections.
// They handle common patterns: initial allocation, null data handling, capacity changes,
// and growth with data preservation.

// Allocates a single contiguous buffer and positions multiple member array pointers within it.
// Used during initial allocation and deserialization to set up collection storage.
// Automatically calculates total buffer size using fold expressions over member pointer types.
// Template parameters:
//   TStruct - Collection structure type (must have iCapacity and pData members)
//   TMemberPtrRefs - Variadic types of member array pointers
// Parameters:
//   rStruct - Collection structure to allocate for
//   iCapacity - Number of elements to allocate space for
//   memberPtrRefs - Variadic member array pointers to position within buffer
template <typename TStruct, typename... TMemberPtrRefs>
void AllocateAndAssign(TStruct& rStruct, int64_t iCapacity, TMemberPtrRefs&... memberPtrRefs)
{
	int64_t iBufferSize = 0;
	((iBufferSize += common::RoundUp<int64_t, 64>(iCapacity * sizeof(memberPtrRefs[0]))), ...);

	rStruct.iCapacity = iCapacity;
	rStruct.pData = common::MakeAligned<std::byte>(iBufferSize);

	std::byte* pCurrent = rStruct.pData.get();
	(AssignAligned(memberPtrRefs, iCapacity, pCurrent), ...);
}

// Resets collection data to null state by releasing the buffer and zeroing member pointers.
// Used by ReallocateIfCapacityChanged when previous frame has null data.
// Template parameters:
//   TStruct - Collection structure type
//   TMemberPtrRefs - Variadic types of member array pointers
// Parameters:
//   rStruct - Collection structure to reset
//   memberPtrRefs - Variadic member array pointers to set to nullptr
template <typename TStruct, typename... TMemberPtrRefs>
void ResetDataToNull(TStruct& rStruct, TMemberPtrRefs&... memberPtrRefs)
{
	rStruct.pData.reset();
	rStruct.iCapacity = 0;
	((memberPtrRefs = nullptr), ...);
}

// Reallocates collection storage if capacity changed between frames.
// Used in Update() methods to synchronize current frame storage with previous frame.
// Handles two cases: null data (resets to null, returns false) and capacity change (reallocates, returns true).
// Return value signals whether Update() should continue processing.
// Template parameters:
//   TStruct - Collection structure type
//   TMemberPtrRefs - Variadic types of member array pointers
// Parameters:
//   rCurrent - Current frame collection (output, will be reallocated if needed)
//   rPrevious - Previous frame collection (input, defines target capacity)
//   memberPtrRefs - Variadic member array pointers from rCurrent
// Returns:
//   true if data exists and Update() should continue
//   false if previous data was null (signals early return from Update())
template <typename TStruct, typename... TMemberPtrRefs>
bool ReallocateIfCapacityChanged(TStruct& rCurrent, const TStruct& rPrevious, TMemberPtrRefs&... memberPtrRefs)
{
	rCurrent.iCount = rPrevious.iCount;

	if (rPrevious.pData == nullptr)
	{
		ResetDataToNull(rCurrent, memberPtrRefs...);
		return false;
	}

	const int64_t iCapacity = rPrevious.iCapacity;
	if (rCurrent.iCapacity != iCapacity)
	{
		int64_t iBufferSize = 0;
		((iBufferSize += common::RoundUp<int64_t, 64>(iCapacity * sizeof(memberPtrRefs[0]))), ...);

		rCurrent.iCapacity = iCapacity;
		rCurrent.pData = common::MakeAligned<std::byte>(iBufferSize);

		std::byte* pCurrent = rCurrent.pData.get();
		(AssignAligned(memberPtrRefs, iCapacity, pCurrent), ...);

		ASSERT(rCurrent.iCount <= rCurrent.iCapacity);
	}

	return true;
}

// Grows collection capacity while preserving existing data.
// Used in Spawn() methods when adding elements would exceed current capacity.
// Allocates new buffer with increased capacity and copies existing elements.
// Standard growth strategy: 2 * capacity + 1
// Template parameters:
//   TStruct - Collection structure type
//   TMemberPtrRefs - Variadic types of member array pointers
// Parameters:
//   rStruct - Collection structure to grow
//   iNewCapacity - Target capacity (typically 2 * old capacity + 1)
//   iCurrentCount - Number of existing elements to copy
//   memberPtrRefs - Variadic member array pointers to reallocate and copy
template <typename TStruct, typename... TMemberPtrRefs>
void GrowCapacityWithCopy(TStruct& rStruct, int64_t iNewCapacity, int64_t iCurrentCount, TMemberPtrRefs&... memberPtrRefs)
{
	int64_t iBufferSize = 0;
	((iBufferSize += common::RoundUp<int64_t, 64>(iNewCapacity * sizeof(memberPtrRefs[0]))), ...);

	common::AlignedUniquePtr<std::byte> pNewData = common::MakeAligned<std::byte>(iBufferSize);
	std::byte* pCurrent = pNewData.get();
	(AssignAndCopyAligned(memberPtrRefs, iNewCapacity, iCurrentCount, pCurrent), ...);
	rStruct.pData = std::move(pNewData);
	rStruct.iCapacity = iNewCapacity;
}

// Calculates new capacity if growth is needed for spawning.
// Used in Spawn() methods to determine if capacity expansion is required.
// Standard growth strategy: 2 * capacity + 1
// Template parameter TCollection: Collection structure type
// Parameter rCollection: Collection to check for growth
// Returns:
//   New capacity (2 * capacity + 1) if growth needed
//   0 if no growth needed
template <typename TCollection>
inline int64_t CalculateGrowthCapacity(const TCollection& rCollection)
{
	if (rCollection.iCount + 1 > rCollection.iCapacity)
	{
		return 2 * rCollection.iCapacity + 1;
	}
	return 0;
}

// Increments counts for paired collections and returns spawn index.
// Used in Spawn() methods after capacity growth (if needed).
// Template parameters:
//   TInterpolate - Interpolate collection structure type
//   TPostRender - PostRender collection structure type
// Parameters:
//   rInterpolate - Interpolate collection to increment
//   rPostRender - PostRender collection to increment
// Returns:
//   Index where new element should be initialized (rInterpolate.iCount - 1)
template <typename TInterpolate, typename TPostRender>
inline int64_t IncrementCountsAndGetSpawnIndex(TInterpolate& rInterpolate, TPostRender& rPostRender)
{
	++rInterpolate.iCount;
	++rPostRender.iCount;
	int64_t iSpawnIndex = rInterpolate.iCount - 1;
	ASSERT(iSpawnIndex < rInterpolate.iCapacity);
	return iSpawnIndex;
}

// ============================================================================
// ELEMENT MANIPULATION
// ============================================================================
// Functions for modifying individual elements within collections.

// Swaps element at index i with the last element in the collection.
// Used in Destroy() methods for O(1) unordered element removal.
// Does NOT decrement count or bounds-check - caller is responsible for:
//   1. Verifying i < rStruct.iCount - 1 before calling (otherwise last element swaps with itself)
//   2. Decrementing rStruct.iCount after swap
//   3. Re-checking index i if processing forward (since new element was swapped in)
// Template parameters:
//   TStruct - Collection structure type (must have iCount member)
//   TMemberPtrRefs - Variadic types of member array pointers
// Parameters:
//   rStruct - Collection structure containing the element
//   i - Index of element to swap with last element
//   memberPtrRefs - Variadic member array pointers to swap
template <typename TStruct, typename... TMemberPtrRefs>
void SwapElement(TStruct& rStruct, int64_t i, TMemberPtrRefs&... memberPtrRefs)
{
	((memberPtrRefs[i] = memberPtrRefs[rStruct.iCount - 1]), ...);
}

// ============================================================================
// MULTI-ARRAY SERIALIZATION HELPERS
// ============================================================================
// These functions operate on multiple parallel arrays simultaneously for CRC calculation,
// stream output, and stream input. They use fold expressions to process all member arrays
// in a single call, ensuring synchronized serialization.

// Computes combined CRC of multiple parallel arrays.
// XORs together CRCs from all member arrays for deterministic replay validation.
// Template parameter TMemberPtrRefs: Variadic types of member array pointers.
// Parameters:
//   iCount - Number of elements in each array
//   memberPtrRefs - Variadic member array pointers to checksum
// Returns:
//   Combined CRC (0 if iCount == 0)
template <typename... TMemberPtrRefs>
common::crc_t MultiCrc(int64_t iCount, TMemberPtrRefs... memberPtrRefs)
{
	common::crc_t checksum = 0;
	if (iCount > 0)
	{
		((checksum ^= common::Crc(memberPtrRefs, iCount)), ...);
	}
	return checksum;
}

// Writes multiple parallel arrays to output stream.
// Serializes all member arrays in order for deterministic file output.
// Template parameter TMemberPtrRefs: Variadic types of member array pointers.
// Parameters:
//   rStream - Output stream to write to
//   iCount - Number of elements in each array
//   memberPtrRefs - Variadic member array pointers to serialize
template <typename... TMemberPtrRefs>
void MultiWrite(std::ostream& rStream, int64_t iCount, TMemberPtrRefs... memberPtrRefs)
{
	((common::Write(rStream, memberPtrRefs, iCount)), ...);
}

// Reads multiple parallel arrays from input stream.
// Deserializes all member arrays in order (must match write order).
// Assumes arrays are already allocated with sufficient capacity.
// Template parameter TMemberPtrRefs: Variadic types of member array pointers.
// Parameters:
//   rStream - Input stream to read from
//   iCount - Number of elements to read into each array
//   memberPtrRefs - Variadic member array pointers to deserialize into
template <typename... TMemberPtrRefs>
void MultiRead(std::istream& rStream, int64_t iCount, TMemberPtrRefs... memberPtrRefs)
{
	((common::Read(rStream, memberPtrRefs, iCount)), ...);
}

// Allocates collection storage and reads data from stream.
// Combines allocation (if capacity > 0) with deserialization.
// Used by CollectionRead() to restore collection state from file.
// Template parameters:
//   TStruct - Collection structure type (iCapacity and iCount must already be set)
//   TMemberPtrRefs - Variadic types of member array pointers
// Parameters:
//   rStruct - Collection structure to allocate and populate
//   rStream - Input stream to read from
//   memberPtrRefs - Variadic member array pointers to allocate and deserialize
template <typename TStruct, typename... TMemberPtrRefs>
void AllocateAndRead(TStruct& rStruct, std::istream& rStream, TMemberPtrRefs&... memberPtrRefs)
{
	if (rStruct.iCapacity > 0)
	{
		AllocateAndAssign(rStruct, rStruct.iCapacity, memberPtrRefs...);
	}
	else
	{
		rStruct.pData = nullptr;
	}

	MultiRead(rStream, rStruct.iCount, memberPtrRefs...);
}

// ============================================================================
// COLLECTION BASE CLASS
// ============================================================================
// Base infrastructure for versioned collections with automatic version tracking
// and common metadata operations.

// CRTP helper that increments FrameBase version counter during static initialization.
// Each collection inherits from VersionIncrementor with a unique VERSION constant.
// This ensures the global frame version reflects all collection schema changes.
// Template parameter VERSION: Version increment contributed by this collection.
template <int64_t VERSION, typename T = FrameBase>
struct VersionIncrementor
{
	inline VersionIncrementor()
	{
		FrameBase::smiVersion += VERSION;
	}
};

// Base struct for collections with dynamic allocation and deterministic serialization.
// Provides common metadata (count, capacity, buffer) and helper methods for serialization.
// All game-specific collections inherit from Collection<VERSION> where VERSION is the
// collection's schema version for save file compatibility.
// Template parameter VERSION: Schema version increment for this collection type.

template <bool INDEXABLE, typename T>
struct OptionalIndexable
{
	OptionalIndexable() = default;
	virtual ~OptionalIndexable() = default;
};

template <typename T>
struct OptionalIndexable<true, typename T>
{
	OptionalIndexable() = default;
	virtual ~OptionalIndexable() = default;

	inline bool operator==(const OptionalIndexable& rOther) const
	{
		bool bEqual = true;
		bEqual &= common::BreakOnNotEqual(uiNextId, rOther.uiNextId);
		bEqual &= common::BreakOnNotEqual(idToIndexMap.size(), rOther.idToIndexMap.size());

		for (const auto& [key, value] : idToIndexMap)
		{
			auto it = rOther.idToIndexMap.find(key);
			if (it == rOther.idToIndexMap.end())
			{
				bEqual = false;
				DEBUG_BREAK();
			}
			else
			{
				bEqual &= common::BreakOnNotEqual(value, it->second);
			}
		}

		return bEqual;
	}

	inline void Write(std::ostream& rStream) const
	{
		int64_t iSize = idToIndexMap.size();
		common::Write(rStream, iSize);
		common::Write(rStream, uiNextId);

		std::vector<T> vecKeys = GetSortedKeys();
		for (const T& key : vecKeys)
		{
			common::Write(rStream, key);
			common::Write(rStream, idToIndexMap.at(key));
		}
	}

	inline void Read(std::istream& rStream)
	{
		int64_t iSize = 0;
		common::Read(rStream, iSize);
		common::Read(rStream, uiNextId);
		idToIndexMap.clear();
		for (int64_t i = 0; i < iSize; ++i)
		{
			T key{}, value{};
			common::Read(rStream, key);
			common::Read(rStream, value);
			idToIndexMap[key] = value;
		}
	}

	inline common::crc_t Crc() const
	{
		common::crc_t checksum = 0;
		checksum ^= common::Crc(static_cast<int64_t>(idToIndexMap.size()));
		checksum ^= common::Crc(uiNextId);

		std::vector<T> vecKeys = GetSortedKeys();
		for (const T& key : vecKeys)
		{
			checksum ^= common::Crc(key);
			checksum ^= common::Crc(idToIndexMap.at(key));
		}

		return checksum;
	}

	T uiNextId = 0;
	std::unordered_map<T, T> idToIndexMap;

private:
	std::vector<T> GetSortedKeys() const
	{
		std::vector<T> vecKeys;
		vecKeys.reserve(idToIndexMap.size());
		for (const auto& [key, value] : idToIndexMap)
		{
			vecKeys.push_back(key);
		}
		std::sort(vecKeys.begin(), vecKeys.end());
		return vecKeys;
	}
};

template <int64_t VERSION, bool INDEXABLE = false, typename T = uint8_t>
struct Collection : public VersionIncrementor<VERSION>, public OptionalIndexable<INDEXABLE, T>
{
	Collection() = default;
	virtual ~Collection() = default;

	inline bool operator==(const Collection& rOther) const
	{
		bool bEqual = true;
		if constexpr (INDEXABLE)
		{
			bEqual &= common::BreakOnNotEqual(static_cast<const OptionalIndexable<INDEXABLE, T>&>(*this), static_cast<const OptionalIndexable<INDEXABLE, T>&>(rOther));
		}
		bEqual &= common::BreakOnNotEqual(iCount, rOther.iCount);
		bEqual &= common::BreakOnNotEqual(iCapacity, rOther.iCapacity);
		return bEqual;
	}

	inline void Write(std::ostream& rStream) const
	{
		if constexpr (INDEXABLE)
		{
			static_cast<const OptionalIndexable<INDEXABLE, T>&>(*this).Write(rStream);
		}
		common::Write(rStream, iCount);
		common::Write(rStream, iCapacity);
	}

	inline void Read(std::istream& rStream)
	{
		if constexpr (INDEXABLE)
		{
			static_cast<OptionalIndexable<INDEXABLE, T>&>(*this).Read(rStream);
		}
		common::Read(rStream, iCount);
		common::Read(rStream, iCapacity);
	}

	inline common::crc_t Crc() const
	{
		common::crc_t checksum = 0;
		if constexpr (INDEXABLE)
		{
			checksum ^= static_cast<const OptionalIndexable<INDEXABLE, T>&>(*this).Crc();
		}
		checksum ^= common::Crc(iCount);
		checksum ^= common::Crc(iCapacity);
		return checksum;
	}

	int64_t iCount = 0;
	int64_t iCapacity = 0;
	common::AlignedUniquePtr<std::byte> pData;
};

// ============================================================================
// COLLECTION-LEVEL PATTERN HELPERS
// ============================================================================
// High-level API functions that combine base class methods with multi-array operations.
// These are the primary functions called by user code for CRC validation, serialization,
// and deserialization of complete collections.

// Computes complete CRC of a collection (metadata + all member arrays).
// Used in static Crc() methods for deterministic replay validation.
// Combines Checksum() with MultiCrc() of member data.
// Template parameters:
//   TStruct - Collection structure type (must inherit from Collection)
//   TMemberPtrRefs - Variadic types of member array pointers
// Parameters:
//   rCurrent - Collection to compute CRC for
//   memberPtrRefs - Variadic member array pointers to include in CRC
// Returns:
//   Combined CRC of metadata and all member arrays
template <typename TStruct, typename... TMemberPtrRefs>
inline common::crc_t CollectionCrc(const TStruct& rCurrent, TMemberPtrRefs... memberPtrRefs)
{
	common::crc_t checksum = rCurrent.Crc();
	checksum ^= engine::MultiCrc(rCurrent.iCount, memberPtrRefs...);
	return checksum;
}

// Writes complete collection to output stream (metadata + all member arrays).
// Used in operator<< overloads for deterministic save file serialization.
// Combines Write() with MultiWrite() of member data.
// Template parameters:
//   TStruct - Collection structure type (must inherit from Collection)
//   TMemberPtrRefs - Variadic types of member array pointers
// Parameters:
//   rStream - Output stream to write to
//   rCurrent - Collection to serialize
//   memberPtrRefs - Variadic member array pointers to serialize
// Returns:
//   Reference to rStream for chaining
template <typename TStruct, typename... TMemberPtrRefs>
inline std::ostream& CollectionWrite(std::ostream& rStream, const TStruct& rCurrent, TMemberPtrRefs... memberPtrRefs)
{
	rCurrent.Write(rStream);
	engine::MultiWrite(rStream, rCurrent.iCount, memberPtrRefs...);
	return rStream;
}

// Reads complete collection from input stream (metadata + all member arrays).
// Used in operator>> overloads to restore collection state from save files.
// Combines Read() with AllocateAndRead() of member data.
// Template parameters:
//   TStruct - Collection structure type (must inherit from Collection)
//   TMemberPtrRefs - Variadic types of member array pointers
// Parameters:
//   rStream - Input stream to read from
//   rCurrent - Collection to deserialize into
//   memberPtrRefs - Variadic member array pointers to allocate and deserialize
// Returns:
//   Reference to rStream for chaining
template <typename TStruct, typename... TMemberPtrRefs>
inline std::istream& CollectionRead(std::istream& rStream, TStruct& rCurrent, TMemberPtrRefs&... memberPtrRefs)
{
	rCurrent.Read(rStream);
	engine::AllocateAndRead(rCurrent, rStream, memberPtrRefs...);
	return rStream;
}

} // namespace engine
