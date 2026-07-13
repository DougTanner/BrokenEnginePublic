#pragma once

namespace engine
{

// ============================================================================
// MEMBER-POINTER VISITOR
// ============================================================================
// Resolves the "member is a C-array-of-pointers T* pp[N] vs a single pointer T*" branch exactly once.
// Array members visit each element-pointer reference member[i] in order 0 .. N-1; single-pointer members
// invoke fn once on the member-pointer reference. Visitation order is CRC- and layout-load-bearing.
// fn receives a reference to the element pointer so assign/reset/swap callers can mutate it, carrying the
// member's const-ness. Where fn needs the element size, derive it as
// std::remove_pointer_t<std::remove_reference_t<decltype(elementPtrRef)>> — strip the reference *before*
// the pointer; remove_pointer_t on the reference type is a no-op and yields sizeof(pointer) (8).

template <typename TMember, typename TFn>
constexpr void ForEachMemberPointer(TMember& member, TFn&& fn)
{
	if constexpr (std::is_array_v<TMember>)
	{
		constexpr size_t N = std::extent_v<TMember>;
		for (size_t i = 0; i < N; ++i)
		{
			fn(member[i]);
		}
	}
	else
	{
		fn(member);
	}
}

// ============================================================================
// SIZE CALCULATION HELPER
// ============================================================================
// Calculate buffer size needed for a member (array or single pointer).

template <typename T>
constexpr int64_t CalculateBufferSize(int64_t iCapacity, const T& member)
{
	ASSERT(iCapacity >= 0);

	int64_t iBufferSize = 0;
	ForEachMemberPointer(member, [&](auto& elementPtrRef)
	{
		using ElementType = std::remove_pointer_t<std::remove_reference_t<decltype(elementPtrRef)>>;
		iBufferSize += common::RoundUp<int64_t, 64>(iCapacity * sizeof(ElementType));
	});
	return iBufferSize;
}

// ============================================================================
// LOW-LEVEL MEMORY ALIGNMENT HELPERS
// ============================================================================
// Internal building blocks for 64-byte pointer alignment used by higher-level allocation helpers.

// Aligns pointer to 64-byte boundary and advances current position. Used during initial allocation.
template <typename T>
void AssignAligned(T& member, int64_t iCapacity, std::byte*& rpCurrent)
{
	ForEachMemberPointer(member, [&](auto& elementPtrRef)
	{
		using ElementPtrType = std::remove_reference_t<decltype(elementPtrRef)>;
		using ElementType = std::remove_pointer_t<ElementPtrType>;
		rpCurrent = reinterpret_cast<std::byte*>(common::RoundUp<uintptr_t, 64>(reinterpret_cast<uintptr_t>(rpCurrent)));
		elementPtrRef = reinterpret_cast<ElementPtrType>(rpCurrent);
		rpCurrent += iCapacity * sizeof(ElementType);
	});
}

// Aligns pointer, copies existing data, and advances current position. Used during capacity growth.
template <typename T>
void AssignAndCopyAligned(T& member, int64_t iCapacity, int64_t iCount, std::byte*& rpCurrent)
{
	ForEachMemberPointer(member, [&](auto& elementPtrRef)
	{
		using ElementPtrType = std::remove_reference_t<decltype(elementPtrRef)>;
		using ElementType = std::remove_pointer_t<ElementPtrType>;
		rpCurrent = reinterpret_cast<std::byte*>(common::RoundUp<uintptr_t, 64>(reinterpret_cast<uintptr_t>(rpCurrent)));

		if (elementPtrRef != nullptr)
		{
			std::memcpy(rpCurrent, elementPtrRef, iCount * sizeof(ElementType));
		}

		elementPtrRef = reinterpret_cast<ElementPtrType>(rpCurrent);
		rpCurrent += iCapacity * sizeof(ElementType);
	});
}

// ============================================================================
// HIGH-LEVEL ALLOCATION & REALLOCATION HELPERS
// ============================================================================
// Orchestrate memory management for Structure-of-Arrays collections.

// Allocates single contiguous buffer and positions member array pointers within it. Used during initial allocation and deserialization.
// Reuses the existing buffer when it is already large enough (avoids reallocation when deserializing into an already-allocated
// collection, e.g. replay-load). iExistingLayoutCapacity is the collection's last-recorded capacity (rStruct.iCapacity captured
// before the metadata Read overwrote it with the stream value) — always <= the live buffer's physical layout, so it is a safe
// lower bound: comparing rStruct.iCapacity against itself would instead reuse an undersized buffer and overrun on the next MultiRead.
template <typename TStruct, typename TTuple>
void AllocateAndAssign(TStruct& rStruct, int64_t iCapacity, TTuple&& members, int64_t iExistingLayoutCapacity)
{
	if (iExistingLayoutCapacity >= iCapacity && rStruct.pData != nullptr)
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

	// Null each member (array or single pointer) via the shared member-pointer visitor.
	std::apply([&](auto&... memberPtrRefs)
	{
		(ForEachMemberPointer(memberPtrRefs, [](auto& elementPtrRef)
		{
			elementPtrRef = nullptr;
		}), ...);
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
		// Handle both arrays and single pointers via the shared member-pointer visitor.
		(ForEachMemberPointer(memberPtrRefs, [&](auto& elementPtrRef)
		{
			elementPtrRef[i] = elementPtrRef[rStruct.iCount - 1];
		}), ...);
	}, std::forward<TTuple>(members));
}

} // namespace engine
