#pragma once

#include "CollectionController.h"
#include "CollectionId.h"
#include "CollectionMemory.h"

namespace game
{

struct Frame;
struct FrameInput;
struct FrameInterpolate;
struct FramePostRender;

}

namespace engine
{

struct FramePostRenderBase;
struct GridCoord;

// ============================================================================
// MULTI-ARRAY SERIALIZATION HELPERS
// ============================================================================
// Synchronized operations on parallel arrays using fold expressions.

// Computes ordered-fold CRC of multiple member arrays for deterministic replay validation.
template <typename TTuple>
common::crc_t MultiCrc(int64_t iCount, TTuple&& members)
{
	common::crc_t checksum = 0;
	if (iCount > 0)
	{
		std::apply([&](auto&... memberPtrRefs)
		{
			// Handle both arrays and single pointers
			([&]()
			{
				if constexpr (std::is_array_v<std::remove_reference_t<decltype(memberPtrRefs)>>)
				{
					static constexpr size_t N = std::extent_v<std::remove_reference_t<decltype(memberPtrRefs)>>;
					for (size_t i = 0; i < N; ++i)
					{
						checksum = (checksum ^ common::Crc(memberPtrRefs[i], iCount)) * common::kCrcMultiplier;
					}
				}
				else
				{
					checksum = (checksum ^ common::Crc(memberPtrRefs, iCount)) * common::kCrcMultiplier;
				}
			}(), ...);
		}, std::forward<TTuple>(members));
	}
	return checksum;
}

// Serializes multiple member arrays to stream in order.
template <typename TTuple>
void MultiWrite(std::ostream& rStream, int64_t iCount, TTuple&& members)
{
	std::apply([&](auto&... memberPtrRefs)
	{
		// Handle both arrays and single pointers
		([&]()
		{
			if constexpr (std::is_array_v<std::remove_reference_t<decltype(memberPtrRefs)>>)
			{
				static constexpr size_t N = std::extent_v<std::remove_reference_t<decltype(memberPtrRefs)>>;
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
	}, std::forward<TTuple>(members));
}

// Deserializes multiple member arrays from stream (must match write order). Arrays must already be allocated.
template <typename TTuple>
void MultiRead(std::istream& rStream, int64_t iCount, TTuple&& members)
{
	std::apply([&](auto&... memberPtrRefs)
	{
		// Handle both arrays and single pointers
		([&]()
		{
			if constexpr (std::is_array_v<std::remove_reference_t<decltype(memberPtrRefs)>>)
			{
				static constexpr size_t N = std::extent_v<std::remove_reference_t<decltype(memberPtrRefs)>>;
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
	}, std::forward<TTuple>(members));
}

// Allocates collection storage and reads data from stream. Used internally by CollectionRead().
// iExistingLayoutCapacity: the collection's last-recorded capacity (CollectionRead captures it before Read clobbers iCapacity).
template <typename TStruct, typename TTuple>
void AllocateAndRead(TStruct& rStruct, std::istream& rStream, TTuple&& members, int64_t iExistingLayoutCapacity)
{
	if (rStruct.iCapacity > 0)
	{
		AllocateAndAssign(rStruct, rStruct.iCapacity, members, iExistingLayoutCapacity);
	}
	else
	{
		ResetDataToNull(rStruct, members);
	}

	MultiRead(rStream, rStruct.iCount, std::forward<TTuple>(members));
}

// ============================================================================
// COLLECTION BASE CLASS
// ============================================================================

// Collection configuration flags
enum class CollectionFlags : uint32_t
{
	kIdToIndex = 0x0001,   // Enable ID-to-index mapping
};
using CollectionFlags_t = common::Flags<CollectionFlags>;

// Request lazy-load of a texture chunk by CRC (implemented in FileManager.cpp)
void RequestTextureChunkLoad(common::crc_t crc);

#if defined(BT_CLIENT)
// Register a CRC for pre-blur (implemented in TextureManager.cpp)
void RegisterLightingTextureCrc(common::crc_t crc);
#endif

// Mixin providing static type registry for collections with type-based configuration sharing.
// Type is passed as template parameter (must be defined before collection).
// Threading contract: registration is startup-only (single-threaded, before Dispatch() workers fan out);
// sTypes is immutable afterward, so parallel frame-tick .at() reads need no synchronization.
template <typename TType>
struct TypeRegistry
{
	using Type = TType;
	static inline std::vector<TType> sTypes;

	static void RegisterType(uint8_t& ruiIndex, const TType& rType)
	{
		ASSERT(ruiIndex == 0xFF);
		ASSERT(sTypes.size() < 0xFF);
		ruiIndex = static_cast<uint8_t>(sTypes.size());
		sTypes.push_back(rType);

		if constexpr (requires { rType.crc; })
		{
			if (rType.crc != 0)
			{
#if defined(BT_CLIENT)
				RequestTextureChunkLoad(rType.crc);
				RegisterLightingTextureCrc(rType.crc);
#endif
			}
		}

		if constexpr (requires { rType.particleCrc; })
		{
			if (rType.particleCrc != 0)
			{
#if defined(BT_CLIENT)
				RequestTextureChunkLoad(rType.particleCrc);
				RegisterLightingTextureCrc(rType.particleCrc);
#endif
			}
		}
	}

	static const TType& GetType(uint8_t uiIndex)
	{
		return sTypes.at(uiIndex);
	}
};

// Non-indexable version (zero overhead)
template <typename T, common::Flags<CollectionFlags> FLAGS>
struct OptionalIdToIndex
{
};

// Indexable version with strong-typed id_t and ID-to-index mapping using CRTP pattern.
template <typename T, common::Flags<CollectionFlags> FLAGS>
	requires (FLAGS & CollectionFlags::kIdToIndex)
struct OptionalIdToIndex<T, FLAGS>
{
	using id_t = engine::id_t<T>;

	std::unordered_map<id_t, int64_t> idToIndexMap;

	inline int64_t IdToIndex(id_t id) const
	{
		return idToIndexMap.at(id);
	}

	inline void Write(std::ostream& rStream) const
	{
		// Heap: GetSortedKeys() builds a temporary vector of all map keys for deterministic write ordering.
		// Could use workbuffer, but save/replay is infrequent so the simplicity of std::vector wins here.
		ScopedSuppressAllocationTracking suppress;
		int64_t iSize = idToIndexMap.size();
		common::Write(rStream, iSize);

		std::vector<id_t> vecKeys = GetSortedKeys();
		for (const id_t& key : vecKeys)
		{
			key.Write(rStream);
			common::Write(rStream, idToIndexMap.at(key));
		}
	}

	inline void Read(std::istream& rStream)
	{
		// Heap: unordered_map::reserve and operator[] allocate buckets and nodes to rebuild the map from file.
		// The map must persist across frames for stable ID lookups, so workbuffer and static arrays are not viable.
		ScopedSuppressAllocationTracking suppress;
		int64_t iSize = 0;
		common::Read(rStream, iSize);
		// Trust boundary: a hostile map size would make reserve() an unbounded allocation. Each entry
		// serializes at least an int64 value, so bound the count against the stream's remaining length.
		common::ValidateDeserializedCount(iSize, sizeof(int64_t), rStream, "OptionalIdToIndex::Read");
		idToIndexMap.clear();
		idToIndexMap.reserve(iSize);
		for (int64_t i = 0; i < iSize; ++i)
		{
			id_t key {};
			int64_t iValue = 0;
			key.Read(rStream);
			common::Read(rStream, iValue);
			idToIndexMap.insert_or_assign(key, iValue);
		}
	}

	inline common::crc_t Crc() const
	{
		common::crc_t checksum = 0;
		checksum = (checksum ^ common::Crc(static_cast<int64_t>(idToIndexMap.size()))) * common::kCrcMultiplier;

		int64_t iKeyCount = static_cast<int64_t>(idToIndexMap.size());
		auto pKeysAlloc = common::gpThreadLocal->mWorkbuffer.PushBuffer<id_t*>(iKeyCount * sizeof(id_t));
		id_t* pKeys = static_cast<id_t*>(pKeysAlloc);
		int64_t i = 0;
		for (const auto& [key, value] : idToIndexMap)
		{
			pKeys[i++] = key;
		}
		std::sort(pKeys, pKeys + iKeyCount);

		for (int64_t j = 0; j < iKeyCount; ++j)
		{
			checksum = (checksum ^ common::Crc(pKeys[j].ToUuid().Value())) * common::kCrcMultiplier;
			checksum = (checksum ^ common::Crc(idToIndexMap.at(pKeys[j]))) * common::kCrcMultiplier;
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

template <typename T, common::Flags<CollectionFlags> FLAGS = {}>
struct Collection : public OptionalIdToIndex<T, FLAGS>
{
	inline bool LogDifferences(const Collection& rOther) const
	{
		bool bEqual = true;
		if constexpr (FLAGS & CollectionFlags::kIdToIndex)
		{
			bEqual &= common::LogDifference<"idToIndexMap.size">(
				static_cast<int64_t>(this->idToIndexMap.size()),
				static_cast<int64_t>(rOther.idToIndexMap.size()));
		}
		bEqual &= common::LogDifference<"iCount">(iCount, rOther.iCount);
		bEqual &= common::LogDifference<"iCapacity">(iCapacity, rOther.iCapacity);
		return bEqual;
	}

	inline void Write(std::ostream& rStream) const
	{
		common::Write(rStream, iCount);
		common::Write(rStream, iCapacity);
		if constexpr (FLAGS & CollectionFlags::kIdToIndex)
		{
			static_cast<const OptionalIdToIndex<T, FLAGS>&>(*this).Write(rStream);
		}
	}

	inline void Read(std::istream& rStream)
	{
		common::Read(rStream, iCount);
		common::Read(rStream, iCapacity);
		// Trust boundary (save / replay / network full-state): reject an inverted or oversized
		// count/capacity before MultiRead writes iCount elements into the iCapacity-sized buffer or
		// MakeAligned allocates iCapacity. Member stride is unknown here, so bound iCount with the
		// minimal stride of 1; the buffer-overrun and unbounded-alloc cases are covered by
		// iCount <= iCapacity <= kiMaxDeserializedCapacity.
		common::ValidateDeserializedCountCapacity(iCount, iCapacity, 1, rStream, "Collection::Read");
		if constexpr (FLAGS & CollectionFlags::kIdToIndex)
		{
			static_cast<OptionalIdToIndex<T, FLAGS>&>(*this).Read(rStream);
			// Indexable collections keep exactly one map entry per live element (Add/Remove helpers).
			if (static_cast<int64_t>(this->idToIndexMap.size()) != iCount)
			{
				throw common::CorruptStreamException("Collection::Read idToIndexMap size != iCount");
			}
		}
	}

	inline common::crc_t Crc() const
	{
		common::crc_t checksum = 0;
		if constexpr (FLAGS & CollectionFlags::kIdToIndex)
		{
			checksum = (checksum ^ static_cast<const OptionalIdToIndex<T, FLAGS>&>(*this).Crc()) * common::kCrcMultiplier;
		}
		checksum = (checksum ^ common::Crc(iCount)) * common::kCrcMultiplier;
		checksum = (checksum ^ common::Crc(iCapacity)) * common::kCrcMultiplier;
		return checksum;
	}

	int64_t iCount = 0;
	int64_t iCapacity = 0;
	common::AlignedUniquePtr<std::byte> pData;
};

// ============================================================================
// COLLECTION-LEVEL PATTERN HELPERS
// ============================================================================
// High-level API functions for complete collection operations.

// Computes complete CRC of collection (metadata + all member arrays) for deterministic replay validation.
template <typename TStruct, typename TTuple>
inline common::crc_t CollectionCrc(const TStruct& rCurrent, TTuple&& members)
{
	common::crc_t checksum = 0;
	checksum = (checksum ^ rCurrent.Crc()) * common::kCrcMultiplier;
	checksum = (checksum ^ engine::MultiCrc(rCurrent.iCount, std::forward<TTuple>(members))) * common::kCrcMultiplier;
	return checksum;
}

template <typename T>
concept HasSharedMembers = requires(const T t) { t.SharedMembers(); };

// Server-build wire/CRC parity: the server broadcasts collections by walking Members() while clients
// deserialize SharedMembers() (SharedCollectionRead below), so a shared collection's server-build
// Members() must be the identical tuple. There is no separate wire serializer — the broadcast streams
// the save-format Write walk (CollectionWrite with cols.Members() in FrameBase.cpp / Frame.cpp).
template <typename TStruct>
inline constexpr bool kbServerMembersParity = std::is_same_v<
	decltype(std::declval<const TStruct&>().Members()),
	decltype(std::declval<const TStruct&>().SharedMembers())>;

// True when every entry of rSubMembers refers to one of rFullMembers' member arrays. Compared by
// address — element types repeat across members, so a type-level check cannot express containment.
template <typename TSubTuple, typename TFullTuple>
inline bool IsMemberTupleSubset(const TSubTuple& rSubMembers, const TFullTuple& rFullMembers)
{
	return std::apply([&](const auto&... subMemberPtrRefs)
	{
		return ([&](const auto& rSubMemberPtrRef)
		{
			return std::apply([&](const auto&... fullMemberPtrRefs)
			{
				// Compare via uintptr_t: clang rejects static_cast<const void*> on &(T* __restrict) as casting away __restrict
				return ((reinterpret_cast<uintptr_t>(&rSubMemberPtrRef) == reinterpret_cast<uintptr_t>(&fullMemberPtrRefs)) || ...);
			}, rFullMembers);
		}(subMemberPtrRefs) && ...);
	}, rSubMembers);
}

template <typename TStruct>
inline common::crc_t SharedCollectionCrc(const TStruct& rCurrent)
{
	if constexpr (HasSharedMembers<TStruct>)
	{
#if defined(BT_SERVER)
		static_assert(kbServerMembersParity<TStruct>, "Server-build Members() must be identical to SharedMembers() — wire format / CRC parity");
#endif
		return CollectionCrc(rCurrent, rCurrent.SharedMembers());
	}
	else
	{
		return CollectionCrc(rCurrent, rCurrent.Members());
	}
}

// Reads collection from a server-format stream. Allocates full Members() (zero-initialized) so client-only
// pointers are valid, then reads only SharedMembers() from the stream to match what the server wrote.
template <typename TStruct>
inline std::istream& SharedCollectionRead(std::istream& rStream, TStruct& rCurrent)
{
	// Capture the last-recorded capacity before Read overwrites iCapacity with the stream value (see AllocateAndAssign).
	int64_t iExistingLayoutCapacity = rCurrent.iCapacity;
	rCurrent.Read(rStream);

	if (rCurrent.iCapacity > 0)
	{
		decltype(rCurrent.Members()) fullMembers = rCurrent.Members();
		AllocateAndAssign(rCurrent, rCurrent.iCapacity, fullMembers, iExistingLayoutCapacity);

		// Zero the buffer so client-only fields default to 0 (invalid IDs, null references). Size from the physical-layout
		// capacity: on the AllocateAndAssign reuse branch the buffer stays strided for the larger iExistingLayoutCapacity
		// while rCurrent.iCapacity holds the smaller stream value, so sizing from the latter would leave the tail un-zeroed.
		int64_t iBufferSize = 0;
		std::apply([&](const auto&... memberPtrRefs)
		{
			((iBufferSize += CalculateBufferSize(std::max(iExistingLayoutCapacity, rCurrent.iCapacity), memberPtrRefs)), ...);
		}, fullMembers);
		std::memset(rCurrent.pData.get(), 0, iBufferSize);
	}
	else
	{
		ResetDataToNull(rCurrent, rCurrent.Members());
	}

	if constexpr (HasSharedMembers<TStruct>)
	{
#if defined(BT_SERVER)
		static_assert(kbServerMembersParity<TStruct>, "Server-build Members() must be identical to SharedMembers() — wire format / CRC parity");
#endif
		// A shared member missing from Members() would have no allocated storage to read into
		ASSERT(IsMemberTupleSubset(rCurrent.SharedMembers(), rCurrent.Members()));
		MultiRead(rStream, rCurrent.iCount, rCurrent.SharedMembers());
	}
	else
	{
		MultiRead(rStream, rCurrent.iCount, rCurrent.Members());
	}

	return rStream;
}

// Writes complete collection to stream (metadata + all member arrays) for save file serialization.
template <typename TStruct, typename TTuple>
inline std::ostream& CollectionWrite(std::ostream& rStream, const TStruct& rCurrent, TTuple&& members)
{
	rCurrent.Write(rStream);
	engine::MultiWrite(rStream, rCurrent.iCount, std::forward<TTuple>(members));
	return rStream;
}

// Reads complete collection from stream (metadata + all member arrays) to restore from save files.
template <typename TStruct, typename TTuple>
inline std::istream& CollectionRead(std::istream& rStream, TStruct& rCurrent, TTuple&& members)
{
	// Capture the last-recorded capacity before Read overwrites iCapacity with the stream value, so AllocateAndAssign decides
	// buffer reuse against the existing capacity instead of self-comparing the just-read value.
	int64_t iExistingLayoutCapacity = rCurrent.iCapacity;
	rCurrent.Read(rStream);
	engine::AllocateAndRead(rCurrent, rStream, std::forward<TTuple>(members), iExistingLayoutCapacity);
	return rStream;
}

// ============================================================================
// INDEXABLE COLLECTION HELPERS
// ============================================================================
// Add / remove lifecycle for paired Interpolate/PostRender collections. Relies on
// GrowCapacityWithCopy / SwapElement from CollectionMemory.h (included above).

// Increments counts for paired Interpolate/PostRender collections and returns spawn index.
template <typename TInterpolate, typename TPostRender>
inline int64_t AddElement(TInterpolate& rInterpolate, TPostRender& rPostRender)
{
	++rInterpolate.iCount;
	++rPostRender.iCount;
	int64_t iSpawnIndex = rInterpolate.iCount - 1;
	ASSERT(iSpawnIndex < rInterpolate.iCapacity);
	return iSpawnIndex;
}

// Grows paired Interpolate/PostRender collections if capacity is insufficient for spawning.
// Returns true if growth occurred, false otherwise.
// Usage: GrowPairedCollections(rInterpolate, rPostRender, rInterpolate.Members(), rPostRender.Members());
template <typename TInterpolate, typename TPostRender, typename TInterpolateTuple, typename TPostRenderTuple>
bool GrowPairedCollections(TInterpolate& rInterpolate, TPostRender& rPostRender, TInterpolateTuple&& interpolateTuple, TPostRenderTuple&& postRenderTuple)
{
	if (rInterpolate.iCount + 1 <= rInterpolate.iCapacity)
	{
		return false;
	}

	int64_t iNewCapacity = 2 * rInterpolate.iCapacity + 1;

	ASSERT(rInterpolate.iCount == rPostRender.iCount);
	GrowCapacityWithCopy(rInterpolate, iNewCapacity, rInterpolate.iCount, std::forward<TInterpolateTuple>(interpolateTuple));
	GrowCapacityWithCopy(rPostRender, iNewCapacity, rPostRender.iCount, std::forward<TPostRenderTuple>(postRenderTuple));

	return true;
}

// Increments counts, generates unique ID, and updates idToIndexMap for indexable collections.
// Returns tuple of (spawnIndex, newId).
// Usage: auto [uiIndex, newId] = AddIndexableElement(rInterpolate, rPostRender, rFramePostRender);
template <typename TInterpolate, typename TPostRender>
std::tuple<int64_t, typename TInterpolate::id_t> AddIndexableElement(TInterpolate& rInterpolate, TPostRender& rPostRender, FramePostRenderBase& rFramePostRender)
{
	// Heap: unordered_map::operator[] may allocate a new bucket or node for the ID-to-index entry.
	// The map must persist across frames for stable ID lookups, so workbuffer and static arrays are not viable.
	ScopedSuppressAllocationTracking suppress;
	int64_t iSpawnIndex = AddElement(rInterpolate, rPostRender);

	using id_t = typename TInterpolate::id_t;
	id_t newId = id_t::Generate(rFramePostRender);
	rInterpolate.idToIndexMap.insert_or_assign(newId, iSpawnIndex);

	return {iSpawnIndex, newId};
}

// Increments counts, generates visual unique ID, and updates idToIndexMap for visual-only collections.
// Uses GenerateVisualUuid() so visual object creation does not perturb the main UUID sequence.
#if defined(BT_CLIENT)
template <typename TInterpolate, typename TPostRender>
std::tuple<int64_t, typename TInterpolate::id_t> AddVisualIndexableElement(TInterpolate& rInterpolate, TPostRender& rPostRender, FramePostRenderBase& rFramePostRender)
{
	// Heap: unordered_map::operator[] may allocate a new bucket or node for the ID-to-index entry.
	// The map must persist across frames for stable ID lookups, so workbuffer and static arrays are not viable.
	ScopedSuppressAllocationTracking suppress;
	int64_t iSpawnIndex = AddElement(rInterpolate, rPostRender);

	using id_t = typename TInterpolate::id_t;
	id_t newId = id_t::GenerateVisual(rFramePostRender);
	rInterpolate.idToIndexMap.insert_or_assign(newId, iSpawnIndex);

	return {iSpawnIndex, newId};
}
#endif // BT_CLIENT

// Increments counts, reuses an existing ID, and updates idToIndexMap for indexable collections.
// Returns tuple of (spawnIndex, existingId).
// Usage: auto [uiIndex, id] = AddIndexableElementWithId(rInterpolate, rPostRender, existingId);
template <typename TInterpolate, typename TPostRender>
std::tuple<int64_t, typename TInterpolate::id_t> AddIndexableElementWithId(TInterpolate& rInterpolate, TPostRender& rPostRender, typename TInterpolate::id_t existingId)
{
	// Heap: unordered_map::operator[] may allocate a new bucket or node for the ID-to-index entry.
	// The map must persist across frames for stable ID lookups, so workbuffer and static arrays are not viable.
	ScopedSuppressAllocationTracking suppress;
	int64_t iSpawnIndex = AddElement(rInterpolate, rPostRender);
	rInterpolate.idToIndexMap.insert_or_assign(existingId, iSpawnIndex);
	return {iSpawnIndex, existingId};
}

// Removes element by ID from paired indexable collections using swap-and-pop.
// Handles SwapElement on both collections, idToIndexMap update, and count decrement.
// Requires: TPostRender must have puiIds member storing element IDs.
// Usage: RemoveIndexableElement(rInterpolate, rPostRender, id, rInterpolate.Members(), rPostRender.Members());
template <typename TInterpolate, typename TPostRender, typename TInterpolateTuple, typename TPostRenderTuple>
void RemoveIndexableElement(TInterpolate& rInterpolate, TPostRender& rPostRender, typename TInterpolate::id_t id, TInterpolateTuple&& interpolateTuple, TPostRenderTuple&& postRenderTuple)
{
	ASSERT(rInterpolate.iCount > 0);
	int64_t iIndex = rInterpolate.idToIndexMap.at(id);

	if (rInterpolate.iCount - 1 > iIndex) [[likely]]
	{
		typename TInterpolate::id_t lastId = rPostRender.puiIds[rInterpolate.iCount - 1];

		SwapElement(rInterpolate, iIndex, std::forward<TInterpolateTuple>(interpolateTuple));
		SwapElement(rPostRender, iIndex, std::forward<TPostRenderTuple>(postRenderTuple));

		rInterpolate.idToIndexMap.insert_or_assign(lastId, iIndex);
	}

	--rInterpolate.iCount;
	--rPostRender.iCount;

	rInterpolate.idToIndexMap.erase(id);
}

// Removes element at index from paired collections using swap-and-pop.
// Handles SwapElement on both collections, count decrement, and loop index adjustment.
template <typename TInterpolate, typename TPostRender, typename TInterpolateTuple, typename TPostRenderTuple>
void DestroyElement(TInterpolate& rInterpolate, TPostRender& rPostRender, int64_t& i, TInterpolateTuple&& interpolateTuple, TPostRenderTuple&& postRenderTuple)
{
	if (rInterpolate.iCount - 1 > i) [[likely]]
	{
		SwapElement(rInterpolate, i, std::forward<TInterpolateTuple>(interpolateTuple));
		SwapElement(rPostRender, i, std::forward<TPostRenderTuple>(postRenderTuple));
		--i;
	}
	--rInterpolate.iCount;
	--rPostRender.iCount;
}

#if defined(BT_CLIENT)
// Accumulates total capacity across all active coords for a collection's BeginRender phase.
// TAccessor: callable returning a const reference to the collection from a FrameInterpolate.
template <typename TAccessor>
int64_t AccumulateRenderCapacity(const std::unordered_map<GridCoord, game::FrameInterpolate>& rRenderInterpolates, const std::vector<GridCoord>& rActiveCoords, TAccessor accessor)
{
	int64_t iTotalCapacity = 0;
	for (const GridCoord& rCoord : rActiveCoords)
	{
		auto it = rRenderInterpolates.find(rCoord);
		if (it != rRenderInterpolates.end())
		{
			iTotalCapacity += accessor(it->second).iCapacity;
		}
	}
	return iTotalCapacity;
}

// Erases entries from a render state map whose IDs are no longer present in any active collection.
// TAccessor: callable returning the collection's idToIndexMap from a FrameInterpolate reference.
template <typename TMapType, typename TAccessor>
void EraseStaleRenderState(TMapType& rRenderStateMap, const std::unordered_map<GridCoord, game::FrameInterpolate>& rRenderInterpolates, const std::vector<GridCoord>& rActiveCoords, TAccessor accessor)
{
	// Heap: unordered_map erase for stale render state entries
	ScopedSuppressAllocationTracking suppress;
	std::erase_if(rRenderStateMap, [&rRenderInterpolates, &rActiveCoords, &accessor](const auto& pair)
	{
		for (const GridCoord& rCoord : rActiveCoords)
		{
			auto it = rRenderInterpolates.find(rCoord);
			if (it != rRenderInterpolates.end() && accessor(it->second).contains(pair.first))
			{
				return false;
			}
		}
		return true;
	});
}
#endif

} // namespace engine
