#pragma once

namespace engine
{

struct FramePostRenderBase;

// ============================================================================
// SIZE CALCULATION HELPER
// ============================================================================
// Calculate buffer size needed for a member (array or single pointer).

template <typename T>
constexpr int64_t CalculateBufferSize(int64_t iCapacity, [[maybe_unused]] const T& member)
{
	ASSERT(iCapacity >= 0);

	if constexpr (std::is_array_v<T>)
	{
		// Array case: sum size for all array elements
		constexpr size_t N = std::extent_v<T>;
		using ElementPtrType = std::remove_extent_t<T>;
		using ElementType = std::remove_pointer_t<ElementPtrType>;

		return N * common::RoundUp<int64_t, 64>(iCapacity * sizeof(ElementType));
	}
	else
	{
		// Single pointer case
		using ElementType = std::remove_pointer_t<T>;
		return common::RoundUp<int64_t, 64>(iCapacity * sizeof(ElementType));
	}
}

// ============================================================================
// LOW-LEVEL MEMORY ALIGNMENT HELPERS
// ============================================================================
// Internal building blocks for 64-byte pointer alignment used by higher-level allocation helpers.

// Aligns pointer to 64-byte boundary and advances current position. Used during initial allocation.
template <typename T>
void AssignAligned(T& member, int64_t iCapacity, std::byte*& rpCurrent)
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
void AssignAndCopyAligned(T& member, int64_t iCapacity, int64_t iCount, std::byte*& rpCurrent)
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
				std::memcpy(rpCurrent, member[i], iCount * sizeof(ElementType));
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
			std::memcpy(rpCurrent, member, iCount * sizeof(ElementType));
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
// Reuses existing buffer if capacity is already sufficient (avoids reallocation for persistent render interpolates).
template <typename TStruct, typename TTuple>
void AllocateAndAssign(TStruct& rStruct, int64_t iCapacity, TTuple&& members)
{
	if (rStruct.iCapacity >= iCapacity && rStruct.pData != nullptr)
	{
		return;
	}

	// Heap: MakeAligned allocates the SOA data buffer, which must persist across frames and can be arbitrarily
	// large depending on entity count. Workbuffer is temporary (lost on Pop) and can't hold cross-frame state.
	ScopedSuppressAllocationTracking suppress;
	std::apply([&](auto&... memberPtrRefs)
	{
		int64_t iBufferSize = 0;
		((iBufferSize += CalculateBufferSize(iCapacity, memberPtrRefs)), ...);

		rStruct.iCapacity = iCapacity;
		rStruct.pData = common::MakeAligned<std::byte>(iBufferSize);

		std::byte* pCurrent = rStruct.pData.get();
		(AssignAligned(memberPtrRefs, iCapacity, pCurrent), ...);
	}, std::forward<TTuple>(members));
}

// Resets collection to null state by releasing buffer and zeroing member pointers.
template <typename TStruct, typename TTuple>
void ResetDataToNull(TStruct& rStruct, TTuple&& members)
{
	rStruct.pData.reset();
	rStruct.iCapacity = 0;

	// Null each member (handle arrays with loop, single pointers directly)
	std::apply([&](auto&... memberPtrRefs)
	{
		([&]()
		{
			if constexpr (std::is_array_v<std::remove_reference_t<decltype(memberPtrRefs)>>)
			{
				constexpr size_t N = std::extent_v<std::remove_reference_t<decltype(memberPtrRefs)>>;
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
	}, std::forward<TTuple>(members));
}

// Type trait to detect if a collection type has idToIndexMap member
template <typename T, typename = void>
struct HasIdToIndex : std::false_type {};

template <typename T>
struct HasIdToIndex<T, std::void_t<decltype(std::declval<T>().idToIndexMap)>> : std::true_type {};

template <typename T>
inline constexpr bool HasIdToIndex_v = HasIdToIndex<T>::value;

// Shared core for Allocate: copies metadata from the previous frame and
// reallocates the SOA buffer (positioning member pointers) when capacity changed. Returns false when
// previous-frame data was null, true otherwise — callers expose or discard that signal per their contract.
template <typename TStruct, typename TTuple>
bool AllocateCore(TStruct& rCurrent, const TStruct& rPrevious, TTuple&& members)
{
	// Heap: MakeAligned for the SOA buffer and unordered_map copy for idToIndexMap. Both persist across frames
	// with sizes that vary at runtime based on entity count, so neither workbuffer nor static arrays work.
	ScopedSuppressAllocationTracking suppress;
	rCurrent.iCount = rPrevious.iCount;

	// Copy indexable state if applicable
	if constexpr (HasIdToIndex_v<TStruct>)
	{
		rCurrent.idToIndexMap = rPrevious.idToIndexMap;
	}

	if (rPrevious.pData == nullptr)
	{
		ResetDataToNull(rCurrent, std::forward<TTuple>(members));
		return false;
	}

	const int64_t iCapacity = rPrevious.iCapacity;

	// Capacity-only guard: a null pData always implies zero capacity (ResetDataToNull zeroes both together,
	// and every allocating path sets both together), so a matching nonzero capacity guarantees pData is non-null.
	if (rCurrent.iCapacity != iCapacity)
	{
		std::apply([&](auto&... memberPtrRefs)
		{
			int64_t iBufferSize = 0;
			((iBufferSize += CalculateBufferSize(iCapacity, memberPtrRefs)), ...);

			rCurrent.iCapacity = iCapacity;
			rCurrent.pData = common::MakeAligned<std::byte>(iBufferSize);

			std::byte* pCurrent = rCurrent.pData.get();
			(AssignAligned(memberPtrRefs, iCapacity, pCurrent), ...);

			ASSERT(rCurrent.iCount <= rCurrent.iCapacity);
		}, std::forward<TTuple>(members));
	}

	return true;
}

// Copies metadata and reallocates buffer for AllocateAndCopy() phase. Does not return early on null data.
// Used in AllocateAndCopy() static methods to prepare collections before Update() phase.
template <typename TStruct, typename TTuple>
void Allocate(TStruct& rCurrent, const TStruct& rPrevious, TTuple&& members)
{
	AllocateCore(rCurrent, rPrevious, std::forward<TTuple>(members));
}

// Allocates and copies the ID array from previous frame. Used by PostRender collections
// whose only persistent member is puiIds.
template <typename TPostRender>
void AllocateAndCopyIds(TPostRender& rCurrent, const TPostRender& rPrevious)
{
	Allocate(rCurrent, rPrevious, rCurrent.Members());

	if (rCurrent.iCount > 0)
	{
		std::memcpy(rCurrent.puiIds, rPrevious.puiIds, rCurrent.iCount * sizeof(rCurrent.puiIds[0]));
	}
}

// Grows capacity while preserving existing data. Growth strategy: 2 * capacity + 1.
template <typename TStruct, typename TTuple>
void GrowCapacityWithCopy(TStruct& rStruct, int64_t iNewCapacity, int64_t iCurrentCount, TTuple&& members)
{
	// Heap: MakeAligned for a larger SOA buffer that replaces the old one. The buffer persists across frames
	// and grows with entity count, so workbuffer (lost on Pop) and static arrays (fixed size) don't work.
	ScopedSuppressAllocationTracking suppress;
	std::apply([&](auto&... memberPtrRefs)
	{
		int64_t iBufferSize = 0;
		((iBufferSize += CalculateBufferSize(iNewCapacity, memberPtrRefs)), ...);

		common::AlignedUniquePtr<std::byte> pNewData = common::MakeAligned<std::byte>(iBufferSize);
		std::byte* pCurrent = pNewData.get();
		(AssignAndCopyAligned(memberPtrRefs, iNewCapacity, iCurrentCount, pCurrent), ...);
		rStruct.pData = std::move(pNewData);
		rStruct.iCapacity = iNewCapacity;
	}, std::forward<TTuple>(members));
}

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

// ============================================================================
// INDEXABLE COLLECTION HELPERS
// ============================================================================
// High-level helpers for Add() and Remove() operations on indexable collections.

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

// ============================================================================
// ELEMENT MANIPULATION
// ============================================================================

// Swaps element at index i with last element for O(1) unordered removal.
// Does NOT decrement count or bounds-check - caller must handle count decrement and index re-checking.
template <typename TStruct, typename TTuple>
void SwapElement(TStruct& rStruct, int64_t i, TTuple&& members)
{
	std::apply([&](auto&... memberPtrRefs)
	{
		// Handle both arrays and single pointers
		([&]()
		{
			if constexpr (std::is_array_v<std::remove_reference_t<decltype(memberPtrRefs)>>)
			{
				constexpr size_t N = std::extent_v<std::remove_reference_t<decltype(memberPtrRefs)>>;
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
	}, std::forward<TTuple>(members));
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

} // namespace engine
