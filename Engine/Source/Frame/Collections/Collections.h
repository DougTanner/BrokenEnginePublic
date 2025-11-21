#pragma once

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

// Global unique identifier with shared counter across all collections
// 0 = invalid/uninitialized, counter starts at 1
struct uuid_t
{
	uint64_t value = 0;

	// Global counter for unique ID generation (starts at 1; 0 is an invalid uuid_t)
	inline static uint64_t suiNextId = 1;

	// Construction
	constexpr uuid_t() = default;
	constexpr explicit uuid_t(uint64_t val) : value(val) {}

	// Generate next unique ID
	static uuid_t Generate()
	{
		return uuid_t{suiNextId++};
	}

	// Check validity
	constexpr bool IsValid() const { return value != 0; }

	// Explicit value access
	constexpr uint64_t Value() const { return value; }

	// Comparison operators
	constexpr bool operator==(const uuid_t& other) const = default;
	constexpr auto operator<=>(const uuid_t& other) const = default;

	// Serialization support
	void Write(std::ostream& stream) const { common::Write(stream, value); }
	void Read(std::istream& stream) { common::Read(stream, value); }
};

// Strong-typed ID wrapper preventing implicit conversions between different collection types
// Tag parameter ensures AreaLights::id_t cannot be mixed with Sounds::id_t
template <typename Tag>
struct id_t
{
	uuid_t uuid {};

	// Construction
	constexpr id_t() = default;
	constexpr explicit id_t(uuid_t u) : uuid(u) {}

	// Generate next unique ID
	static id_t Generate()
	{
		return id_t {uuid_t::Generate()};
	}

	// Check validity
	constexpr bool IsValid() const { return uuid.IsValid(); }

	// Explicit conversion to uuid_t for generic comparisons
	constexpr uuid_t ToUuid() const { return uuid; }

	// Comparison operators (only with same tag type)
	constexpr bool operator==(const id_t& other) const = default;
	constexpr auto operator<=>(const id_t& other) const = default;

	// Serialization support
	void Write(std::ostream& stream) const { uuid.Write(stream); }
	void Read(std::istream& stream) { uuid.Read(stream); }
};

} // namespace engine

// Hash specializations in std namespace for unordered_map support
namespace std
{

template <>
struct hash<engine::uuid_t>
{
	size_t operator()(const engine::uuid_t& id) const noexcept
	{
		return std::hash<uint64_t>{}(id.value);
	}
};

template <typename Tag>
struct hash<engine::id_t<Tag>>
{
	size_t operator()(const engine::id_t<Tag>& id) const noexcept
	{
		return std::hash<engine::uuid_t>{}(id.uuid);
	}
};

} // namespace std

namespace engine
{

// ============================================================================
// SIZE CALCULATION HELPER
// ============================================================================
// Calculate buffer size needed for a member (array or single pointer).

template <typename T>
constexpr uint64_t CalculateBufferSize(uint64_t iCapacity, const T& member)
{
	if constexpr (std::is_array_v<T>)
	{
		// Array case: sum size for all array elements
		constexpr size_t N = std::extent_v<T>;
		using ElementPtrType = std::remove_extent_t<T>;
		using ElementType = std::remove_pointer_t<ElementPtrType>;

		return N * common::RoundUp<uint64_t, 64>(iCapacity * sizeof(ElementType));
	}
	else
	{
		// Single pointer case
		using ElementType = std::remove_pointer_t<T>;
		return common::RoundUp<uint64_t, 64>(iCapacity * sizeof(ElementType));
	}
}

// ============================================================================
// LOW-LEVEL MEMORY ALIGNMENT HELPERS
// ============================================================================
// Internal building blocks for 64-byte pointer alignment used by higher-level allocation helpers.

// Aligns pointer to 64-byte boundary and advances current position. Used during initial allocation.
template <typename T>
void AssignAligned(T& member, uint64_t iCapacity, std::byte*& rpCurrent)
{
	if constexpr (std::is_array_v<T>)
	{
		// Array case: assign each array element
		constexpr size_t N = std::extent_v<T>;
		using ElementPtrType = std::remove_extent_t<T>;
		using ElementType = std::remove_pointer_t<ElementPtrType>;

		for (size_t i = 0; i < N; ++i)
		{
			rpCurrent = reinterpret_cast<std::byte*>(common::RoundUp<uintptr_t, 64>(reinterpret_cast<uintptr_t>(rpCurrent)));
			member[i] = reinterpret_cast<ElementPtrType>(rpCurrent);
			rpCurrent += iCapacity * sizeof(ElementType);
		}
	}
	else
	{
		// Single pointer case
		using ElementType = std::remove_pointer_t<T>;
		rpCurrent = reinterpret_cast<std::byte*>(common::RoundUp<uintptr_t, 64>(reinterpret_cast<uintptr_t>(rpCurrent)));
		member = reinterpret_cast<T>(rpCurrent);
		rpCurrent += iCapacity * sizeof(ElementType);
	}
}

// Aligns pointer, copies existing data, and advances current position. Used during capacity growth.
template <typename T>
void AssignAndCopyAligned(T& member, uint64_t iCapacity, uint64_t iCount, std::byte*& rpCurrent)
{
	if constexpr (std::is_array_v<T>)
	{
		// Array case: copy and assign each array element
		constexpr size_t N = std::extent_v<T>;
		using ElementPtrType = std::remove_extent_t<T>;
		using ElementType = std::remove_pointer_t<ElementPtrType>;

		for (size_t i = 0; i < N; ++i)
		{
			rpCurrent = reinterpret_cast<std::byte*>(common::RoundUp<uintptr_t, 64>(reinterpret_cast<uintptr_t>(rpCurrent)));

			if (member[i] != nullptr)
			{
				memcpy(rpCurrent, member[i], iCount * sizeof(ElementType));
			}

			member[i] = reinterpret_cast<ElementPtrType>(rpCurrent);
			rpCurrent += iCapacity * sizeof(ElementType);
		}
	}
	else
	{
		// Single pointer case
		using ElementType = std::remove_pointer_t<T>;
		rpCurrent = reinterpret_cast<std::byte*>(common::RoundUp<uintptr_t, 64>(reinterpret_cast<uintptr_t>(rpCurrent)));

		if (member != nullptr)
		{
			memcpy(rpCurrent, member, iCount * sizeof(ElementType));
		}

		member = reinterpret_cast<T>(rpCurrent);
		rpCurrent += iCapacity * sizeof(ElementType);
	}
}

// ============================================================================
// HIGH-LEVEL ALLOCATION & REALLOCATION HELPERS
// ============================================================================
// Orchestrate memory management for Structure-of-Arrays collections.

// Allocates single contiguous buffer and positions member array pointers within it. Used during initial allocation and deserialization.
template <typename TStruct, typename... TMemberPtrRefs>
void AllocateAndAssign(TStruct& rStruct, uint64_t iCapacity, TMemberPtrRefs&... memberPtrRefs)
{
	uint64_t iBufferSize = 0;
	((iBufferSize += CalculateBufferSize(iCapacity, memberPtrRefs)), ...);

	rStruct.iCapacity = iCapacity;
	rStruct.pData = common::MakeAligned<std::byte>(iBufferSize);

	std::byte* pCurrent = rStruct.pData.get();
	(AssignAligned(memberPtrRefs, iCapacity, pCurrent), ...);
}

// Resets collection to null state by releasing buffer and zeroing member pointers.
template <typename TStruct, typename... TMemberPtrRefs>
void ResetDataToNull(TStruct& rStruct, TMemberPtrRefs&... memberPtrRefs)
{
	rStruct.pData.reset();
	rStruct.iCapacity = 0;

	// Null each member (handle arrays with loop, single pointers directly)
	([&]() {
		if constexpr (std::is_array_v<TMemberPtrRefs>)
		{
			constexpr size_t N = std::extent_v<TMemberPtrRefs>;
			for (size_t i = 0; i < N; ++i)
			{
				memberPtrRefs[i] = nullptr;
			}
		}
		else
		{
			memberPtrRefs = nullptr;
		}
	}(), ...);
}

// Type trait to detect if a collection type has idToIndexMap member
template<typename T, typename = void>
struct HasIdToIndex : std::false_type {};

template<typename T>
struct HasIdToIndex<T, std::void_t<decltype(std::declval<T>().idToIndexMap)>> : std::true_type {};

template<typename T>
inline constexpr bool HasIdToIndex_v = HasIdToIndex<T>::value;

// Synchronizes current frame storage with previous frame capacity. Automatically copies indexable state.
// Returns false if previous data was null (signals early return from Update()), true otherwise.
template <typename TStruct, typename... TMemberPtrRefs>
bool ReallocateIfCapacityChanged(TStruct& rCurrent, const TStruct& rPrevious, TMemberPtrRefs&... memberPtrRefs)
{
	rCurrent.iCount = rPrevious.iCount;

	// Copy indexable state if applicable
	if constexpr (HasIdToIndex_v<TStruct>)
	{
		rCurrent.idToIndexMap = rPrevious.idToIndexMap;
	}

	if (rPrevious.pData == nullptr)
	{
		ResetDataToNull(rCurrent, memberPtrRefs...);
		return false;
	}

	const uint64_t iCapacity = rPrevious.iCapacity;
	if (rCurrent.iCapacity != iCapacity)
	{
		uint64_t iBufferSize = 0;
		((iBufferSize += CalculateBufferSize(iCapacity, memberPtrRefs)), ...);

		rCurrent.iCapacity = iCapacity;
		rCurrent.pData = common::MakeAligned<std::byte>(iBufferSize);

		std::byte* pCurrent = rCurrent.pData.get();
		(AssignAligned(memberPtrRefs, iCapacity, pCurrent), ...);

		ASSERT(rCurrent.iCount <= rCurrent.iCapacity);
	}

	return true;
}

// Copies metadata and reallocates buffer for AllocateAndCopy() phase. Does not return early on null data.
// Used in AllocateAndCopy() static methods to prepare collections before Update() phase.
template <typename TStruct, typename... TMemberPtrRefs>
void ReallocateAndCopyMetadata(TStruct& rCurrent, const TStruct& rPrevious, TMemberPtrRefs&... memberPtrRefs)
{
	rCurrent.iCount = rPrevious.iCount;

	// Copy indexable state if applicable
	if constexpr (HasIdToIndex_v<TStruct>)
	{
		rCurrent.idToIndexMap = rPrevious.idToIndexMap;
	}

	if (rPrevious.pData == nullptr)
	{
		ResetDataToNull(rCurrent, memberPtrRefs...);
		return;
	}

	const uint64_t iCapacity = rPrevious.iCapacity;
	if (rCurrent.iCapacity != iCapacity)
	{
		uint64_t iBufferSize = 0;
		((iBufferSize += CalculateBufferSize(iCapacity, memberPtrRefs)), ...);

		rCurrent.iCapacity = iCapacity;
		rCurrent.pData = common::MakeAligned<std::byte>(iBufferSize);

		std::byte* pCurrent = rCurrent.pData.get();
		(AssignAligned(memberPtrRefs, iCapacity, pCurrent), ...);

		ASSERT(rCurrent.iCount <= rCurrent.iCapacity);
	}
}

// Grows capacity while preserving existing data. Growth strategy: 2 * capacity + 1.
template <typename TStruct, typename... TMemberPtrRefs>
void GrowCapacityWithCopy(TStruct& rStruct, uint64_t iNewCapacity, uint64_t iCurrentCount, TMemberPtrRefs&... memberPtrRefs)
{
	uint64_t iBufferSize = 0;
	((iBufferSize += CalculateBufferSize(iNewCapacity, memberPtrRefs)), ...);

	common::AlignedUniquePtr<std::byte> pNewData = common::MakeAligned<std::byte>(iBufferSize);
	std::byte* pCurrent = pNewData.get();
	(AssignAndCopyAligned(memberPtrRefs, iNewCapacity, iCurrentCount, pCurrent), ...);
	rStruct.pData = std::move(pNewData);
	rStruct.iCapacity = iNewCapacity;
}

// Returns new capacity (2 * capacity + 1) if growth needed for spawning, otherwise 0.
template <typename TCollection>
inline uint64_t CalculateGrowthCapacity(const TCollection& rCollection)
{
	if (rCollection.iCount + 1 > rCollection.iCapacity)
	{
		return 2 * rCollection.iCapacity + 1;
	}
	return 0;
}

// Increments counts for paired Interpolate/PostRender collections and returns spawn index.
template <typename TInterpolate, typename TPostRender>
inline uint64_t IncrementCountsAndGetSpawnIndex(TInterpolate& rInterpolate, TPostRender& rPostRender)
{
	++rInterpolate.iCount;
	++rPostRender.iCount;
	uint64_t iSpawnIndex = rInterpolate.iCount - 1;
	ASSERT(iSpawnIndex < rInterpolate.iCapacity);
	return iSpawnIndex;
}

// ============================================================================
// ELEMENT MANIPULATION
// ============================================================================

// Swaps element at index i with last element for O(1) unordered removal.
// Does NOT decrement count or bounds-check - caller must handle count decrement and index re-checking.
template <typename TStruct, typename... TMemberPtrRefs>
void SwapElement(TStruct& rStruct, uint64_t i, TMemberPtrRefs&... memberPtrRefs)
{
	// Handle both arrays and single pointers
	([&]() {
		if constexpr (std::is_array_v<TMemberPtrRefs>)
		{
			constexpr size_t N = std::extent_v<TMemberPtrRefs>;
			for (size_t j = 0; j < N; ++j)
			{
				memberPtrRefs[j][i] = memberPtrRefs[j][rStruct.iCount - 1];
			}
		}
		else
		{
			memberPtrRefs[i] = memberPtrRefs[rStruct.iCount - 1];
		}
	}(), ...);
}

// ============================================================================
// MULTI-ARRAY SERIALIZATION HELPERS
// ============================================================================
// Synchronized operations on parallel arrays using fold expressions.

// Computes XOR'd CRC of multiple member arrays for deterministic replay validation.
template <typename... TMemberPtrRefs>
common::crc_t MultiCrc(uint64_t iCount, TMemberPtrRefs... memberPtrRefs)
{
	common::crc_t checksum = 0;
	if (iCount > 0)
	{
		// Handle both arrays and single pointers
		([&]() {
			if constexpr (std::is_array_v<TMemberPtrRefs>)
			{
				constexpr size_t N = std::extent_v<TMemberPtrRefs>;
				for (size_t i = 0; i < N; ++i)
				{
					checksum ^= common::Crc(memberPtrRefs[i], iCount);
				}
			}
			else
			{
				checksum ^= common::Crc(memberPtrRefs, iCount);
			}
		}(), ...);
	}
	return checksum;
}

// Serializes multiple member arrays to stream in order.
template <typename... TMemberPtrRefs>
void MultiWrite(std::ostream& rStream, uint64_t iCount, TMemberPtrRefs... memberPtrRefs)
{
	// Handle both arrays and single pointers
	([&]() {
		if constexpr (std::is_array_v<TMemberPtrRefs>)
		{
			constexpr size_t N = std::extent_v<TMemberPtrRefs>;
			for (size_t i = 0; i < N; ++i)
			{
				common::Write(rStream, memberPtrRefs[i], iCount);
			}
		}
		else
		{
			common::Write(rStream, memberPtrRefs, iCount);
		}
	}(), ...);
}

// Deserializes multiple member arrays from stream (must match write order). Arrays must already be allocated.
template <typename... TMemberPtrRefs>
void MultiRead(std::istream& rStream, uint64_t iCount, TMemberPtrRefs... memberPtrRefs)
{
	// Handle both arrays and single pointers
	([&]() {
		if constexpr (std::is_array_v<TMemberPtrRefs>)
		{
			constexpr size_t N = std::extent_v<TMemberPtrRefs>;
			for (size_t i = 0; i < N; ++i)
			{
				common::Read(rStream, memberPtrRefs[i], iCount);
			}
		}
		else
		{
			common::Read(rStream, memberPtrRefs, iCount);
		}
	}(), ...);
}

// Allocates collection storage and reads data from stream. Used internally by CollectionRead().
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
// Versioned collections with automatic version tracking and optional ID-to-index mapping.

// CRTP helper that increments FrameBase version counter during static initialization.
template <int64_t VERSION, typename T = FrameBase>
struct VersionIncrementor
{
	inline VersionIncrementor()
	{
		FrameBase::smiVersion += VERSION;
	}
};

// Non-indexable version (zero overhead)
template <typename DerivedCollection, bool HAS_ID_TO_INDEX>
struct OptionaldToIndex
{
};

// Indexable version with strong-typed id_t and ID-to-index mapping using CRTP pattern.
template <typename DerivedCollection>
struct OptionaldToIndex<DerivedCollection, true>
{
	using id_t = engine::id_t<DerivedCollection>;

	std::unordered_map<id_t, uint64_t> idToIndexMap;

	inline uint64_t IdToIndex(id_t id) const
	{
		return idToIndexMap.at(id);
	}

	inline bool operator==(const OptionaldToIndex& rOther) const
	{
		bool bEqual = true;
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
		uint64_t uiSize = idToIndexMap.size();
		common::Write(rStream, uiSize);
		uint64_t uiGlobalNextId = uuid_t::suiNextId;
		common::Write(rStream, uiGlobalNextId);

		std::vector<id_t> vecKeys = GetSortedKeys();
		for (const id_t& key : vecKeys)
		{
			key.Write(rStream);
			common::Write(rStream, idToIndexMap.at(key));
		}
	}

	inline void Read(std::istream& rStream)
	{
		uint64_t uiSize = 0;
		common::Read(rStream, uiSize);
		uint64_t uiGlobalNextId = 0;
		common::Read(rStream, uiGlobalNextId);
		uuid_t::suiNextId = uiGlobalNextId;
		idToIndexMap.clear();
		idToIndexMap.reserve(uiSize);
		for (uint64_t i = 0; i < uiSize; ++i)
		{
			id_t key{};
			uint64_t value{};
			key.Read(rStream);
			common::Read(rStream, value);
			idToIndexMap[key] = value;
		}
	}

	inline common::crc_t Crc() const
	{
		common::crc_t checksum = 0;
		checksum ^= common::Crc(static_cast<int64_t>(idToIndexMap.size()));
		checksum ^= common::Crc(uuid_t::suiNextId);

		std::vector<id_t> vecKeys = GetSortedKeys();
		for (const id_t& key : vecKeys)
		{
			checksum ^= common::Crc(key.ToUuid().Value());
			checksum ^= common::Crc(idToIndexMap.at(key));
		}

		return checksum;
	}

private:
	// Returns sorted keys for deterministic serialization ordering.
	std::vector<id_t> GetSortedKeys() const
	{
		std::vector<id_t> vecKeys;
		vecKeys.reserve(idToIndexMap.size());
		for (const auto& [key, value] : idToIndexMap)
		{
			vecKeys.push_back(key);
		}
		std::sort(vecKeys.begin(), vecKeys.end());
		return vecKeys;
	}
};

template <typename DerivedCollection, int64_t VERSION, bool HAS_ID_TO_INDEX = false>
struct Collection : public VersionIncrementor<VERSION>, public OptionaldToIndex<DerivedCollection, HAS_ID_TO_INDEX>
{
	inline bool operator==(const Collection& rOther) const
	{
		bool bEqual = true;
		if constexpr (HAS_ID_TO_INDEX)
		{
			bEqual &= common::BreakOnNotEqual(static_cast<const OptionaldToIndex<DerivedCollection, HAS_ID_TO_INDEX>&>(*this), static_cast<const OptionaldToIndex<DerivedCollection, HAS_ID_TO_INDEX>&>(rOther));
		}
		bEqual &= common::BreakOnNotEqual(iCount, rOther.iCount);
		bEqual &= common::BreakOnNotEqual(iCapacity, rOther.iCapacity);
		return bEqual;
	}

	inline void Write(std::ostream& rStream) const
	{
		if constexpr (HAS_ID_TO_INDEX)
		{
			static_cast<const OptionaldToIndex<DerivedCollection, HAS_ID_TO_INDEX>&>(*this).Write(rStream);
		}
		common::Write(rStream, iCount);
		common::Write(rStream, iCapacity);
	}

	inline void Read(std::istream& rStream)
	{
		if constexpr (HAS_ID_TO_INDEX)
		{
			static_cast<OptionaldToIndex<DerivedCollection, HAS_ID_TO_INDEX>&>(*this).Read(rStream);
		}
		common::Read(rStream, iCount);
		common::Read(rStream, iCapacity);
	}

	inline common::crc_t Crc() const
	{
		common::crc_t checksum = 0;
		if constexpr (HAS_ID_TO_INDEX)
		{
			checksum ^= static_cast<const OptionaldToIndex<DerivedCollection, HAS_ID_TO_INDEX>&>(*this).Crc();
		}
		checksum ^= common::Crc(iCount);
		checksum ^= common::Crc(iCapacity);
		return checksum;
	}

	uint64_t iCount = 0;
	uint64_t iCapacity = 0;
	common::AlignedUniquePtr<std::byte> pData;
};

// ============================================================================
// COLLECTION-LEVEL PATTERN HELPERS
// ============================================================================
// High-level API functions for complete collection operations.

// Computes complete CRC of collection (metadata + all member arrays) for deterministic replay validation.
template <typename TStruct, typename... TMemberPtrRefs>
inline common::crc_t CollectionCrc(const TStruct& rCurrent, TMemberPtrRefs... memberPtrRefs)
{
	common::crc_t checksum = rCurrent.Crc();
	checksum ^= engine::MultiCrc(rCurrent.iCount, memberPtrRefs...);
	return checksum;
}

// Writes complete collection to stream (metadata + all member arrays) for save file serialization.
template <typename TStruct, typename... TMemberPtrRefs>
inline std::ostream& CollectionWrite(std::ostream& rStream, const TStruct& rCurrent, TMemberPtrRefs... memberPtrRefs)
{
	rCurrent.Write(rStream);
	engine::MultiWrite(rStream, rCurrent.iCount, memberPtrRefs...);
	return rStream;
}

// Reads complete collection from stream (metadata + all member arrays) to restore from save files.
template <typename TStruct, typename... TMemberPtrRefs>
inline std::istream& CollectionRead(std::istream& rStream, TStruct& rCurrent, TMemberPtrRefs&... memberPtrRefs)
{
	rCurrent.Read(rStream);
	engine::AllocateAndRead(rCurrent, rStream, memberPtrRefs...);
	return rStream;
}

} // namespace engine
