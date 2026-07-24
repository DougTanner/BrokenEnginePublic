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

// Raw summed byte size of one member tuple laid out at iCapacity. Unclamped: allocation callers apply their own
// max(size, 1) so a zero-member layout still backs a positive capacity, while the deserialize zero-fill wants the
// exact physical-layout sum.
template <typename TTuple>
int64_t MemberTupleBufferSize(int64_t iCapacity, const TTuple& members)
{
	int64_t iBufferSize = 0;
	std::apply([&](const auto&... memberPtrRefs)
	{
		((iBufferSize += CalculateBufferSize(iCapacity, memberPtrRefs)), ...);
	}, members);
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

// Resets collection to null state by releasing buffer and zeroing member pointers.
template <typename TStruct, typename TTuple>
void ResetDataToNull(TStruct& rStruct, TTuple&& members)
{
	rStruct.pData.reset();
	rStruct.iCapacity = 0;
	rStruct.iPhysicalLayoutCapacity = 0;

	std::apply([&](auto&... memberPtrRefs)
	{
		(ForEachMemberPointer(memberPtrRefs, [](auto& elementPtrRef)
		{
			elementPtrRef = nullptr;
		}), ...);
	}, std::forward<TTuple>(members));
}

// Allocates single contiguous buffer and positions member array pointers within it. Used during initial allocation and deserialization.
// Reuses the existing buffer when its physical layout already spans iCapacity (avoids reallocation when deserializing into an
// already-allocated collection, e.g. replay-load). iPhysicalLayoutCapacity is the true stride of the installed buffer and
// survives a shrink-reuse even as iCapacity drops to a smaller stream value, so it is the correct reuse bound; comparing the
// just-read iCapacity against itself would instead reuse an undersized buffer and overrun on the next MultiRead.
template <typename TStruct, typename TTuple>
void AllocateAndAssign(TStruct& rStruct, int64_t iCapacity, TTuple&& members)
{
	if (rStruct.iPhysicalLayoutCapacity >= iCapacity && rStruct.pData != nullptr)
	{
		return;
	}

	// Heap: MakeAligned allocates the SOA data buffer, which must persist across frames and can be arbitrarily
	// large depending on entity count. Workbuffer is temporary (lost on Pop) and can't hold cross-frame state.
	ScopedSuppressAllocationTracking suppress;
	int64_t iBufferSize = std::max<int64_t>(MemberTupleBufferSize(iCapacity, members), 1);
	if (iBufferSize > common::kiMaxDeserializedBytes)
	{
		ResetDataToNull(rStruct, members);
		throw common::CorruptStreamException("AllocateAndAssign");
	}

	common::AlignedUniquePtr<std::byte> pNewData = common::MakeAligned<std::byte>(iBufferSize);
	if (pNewData == nullptr)
	{
		ResetDataToNull(rStruct, members);
		throw common::CorruptStreamException("AllocateAndAssign");
	}

	// Publish the capacity and physical layout only after a successful install.
	rStruct.iCapacity = iCapacity;
	rStruct.iPhysicalLayoutCapacity = iCapacity;
	rStruct.pData = std::move(pNewData);

	std::byte* pCurrent = rStruct.pData.get();
	std::apply([&](auto&... memberPtrRefs)
	{
		(AssignAligned(memberPtrRefs, iCapacity, pCurrent), ...);
	}, std::forward<TTuple>(members));
}

// Detects whether a collection type has an idToIndexMap member.
template <typename T>
concept HasIdToIndex = requires(T& rStruct)
{
	rStruct.idToIndexMap;
};

template <typename T>
concept HasPersistentMembers = requires(const T& rStruct)
{
	rStruct.PersistentMembers();
};

// True when every entry of rSubsetMembers refers to one of rFullMembers' member arrays. Compared by
// address — element types repeat across members, so a type-level check cannot express containment.
template <typename TSubsetTuple, typename TFullTuple>
inline bool IsMemberTupleSubset(const TSubsetTuple& rSubsetMembers, const TFullTuple& rFullMembers)
{
	return std::apply([&](const auto&... rSubsetMemberPointers)
	{
		return ([&](const auto& rSubsetMemberPointer)
		{
			return std::apply([&](const auto&... rFullMemberPointers)
			{
				// Compare via uintptr_t: clang rejects static_cast<const void*> on &(T* __restrict) as casting away __restrict
				return ((reinterpret_cast<uintptr_t>(&rSubsetMemberPointer) == reinterpret_cast<uintptr_t>(&rFullMemberPointers)) || ...);
			}, rFullMembers);
		}(rSubsetMemberPointers) && ...);
	}, rSubsetMembers);
}

// Copies metadata and reallocates buffer for AllocateAndCopy() phase. Resets null previous-frame data.
// Used in AllocateAndCopy() static methods to prepare collections before Update() phase.
template <typename TStruct, typename TTuple>
void Allocate(TStruct& rCurrent, const TStruct& rPrevious, TTuple&& members)
{
	// Heap: MakeAligned for the SOA buffer and unordered_map copy for idToIndexMap. Both persist across frames
	// with sizes that vary at runtime based on entity count, so neither workbuffer nor static arrays work.
	ScopedSuppressAllocationTracking suppress;
	rCurrent.iCount = rPrevious.iCount;

	// Copy indexable state if applicable
	if constexpr (HasIdToIndex<TStruct>)
	{
		rCurrent.idToIndexMap = rPrevious.idToIndexMap;
	}

	if (rPrevious.pData == nullptr)
	{
		ResetDataToNull(rCurrent, std::forward<TTuple>(members));
		return;
	}

	const int64_t iCapacity = rPrevious.iCapacity;

	// Capacity-only guard: a null pData always implies zero capacity (ResetDataToNull zeroes both together,
	// and every allocating path sets both together), so a matching nonzero capacity guarantees pData is non-null.
	// The equal-capacity else path reuses rCurrent's existing buffer as-is, preserving its iPhysicalLayoutCapacity.
	if (rCurrent.iCapacity != iCapacity)
	{
		int64_t iBufferSize = std::max<int64_t>(MemberTupleBufferSize(iCapacity, members), 1);

		common::AlignedUniquePtr<std::byte> pNewData = common::MakeAligned<std::byte>(iBufferSize);
		if (pNewData == nullptr)
		{
			throw std::bad_alloc();
		}

		rCurrent.iCapacity = iCapacity;
		rCurrent.iPhysicalLayoutCapacity = iCapacity;
		rCurrent.pData = std::move(pNewData);

		std::byte* pCurrent = rCurrent.pData.get();
		std::apply([&](auto&... memberPtrRefs)
		{
			(AssignAligned(memberPtrRefs, iCapacity, pCurrent), ...);
		}, std::forward<TTuple>(members));

		ASSERT(rCurrent.iCount <= rCurrent.iCapacity);
	}
}

// Copies one corresponding member array for the live row count.
template <typename TCurrentPointer, typename TPreviousPointer>
void CopyMemberPointerRows(int64_t iCount, TCurrentPointer& rCurrentPointer, const TPreviousPointer& rPreviousPointer)
{
	using CurrentPointer = std::remove_reference_t<decltype(rCurrentPointer)>;
	using PreviousPointer = std::remove_reference_t<decltype(rPreviousPointer)>;
	static_assert(std::is_pointer_v<CurrentPointer> && std::is_pointer_v<PreviousPointer>, "Collection members must be pointers");

	using CurrentElement = std::remove_pointer_t<CurrentPointer>;
	using PreviousElement = std::remove_pointer_t<PreviousPointer>;
	static_assert(std::is_same_v<CurrentElement, PreviousElement>, "Corresponding collection member element types must match");

	std::memcpy(rCurrentPointer, rPreviousPointer, iCount * sizeof(CurrentElement));
}

// Copies one corresponding Members() tuple entry. C arrays of member pointers are copied in index order.
template <typename TCurrentMember, typename TPreviousMember>
void CopyMemberEntryRows(int64_t iCount, TCurrentMember& rCurrentMember, const TPreviousMember& rPreviousMember)
{
	constexpr bool kbCurrentIsArray = std::is_array_v<TCurrentMember>;
	constexpr bool kbPreviousIsArray = std::is_array_v<TPreviousMember>;
	static_assert(kbCurrentIsArray == kbPreviousIsArray, "Corresponding collection member shapes must match");

	if constexpr (kbCurrentIsArray && kbPreviousIsArray)
	{
		constexpr size_t kuiCurrentExtent = std::extent_v<TCurrentMember>;
		constexpr size_t kuiPreviousExtent = std::extent_v<TPreviousMember>;
		static_assert(kuiCurrentExtent == kuiPreviousExtent, "Corresponding collection member array extents must match");
		for (size_t i = 0; i < kuiCurrentExtent; ++i)
		{
			CopyMemberPointerRows(iCount, rCurrentMember[i], rPreviousMember[i]);
		}
	}
	else
	{
		CopyMemberPointerRows(iCount, rCurrentMember, rPreviousMember);
	}
}

template <typename TCurrentTuple, typename TPreviousTuple, size_t... INDICES>
void CopyMemberRows(int64_t iCount, TCurrentTuple&& currentMembers, TPreviousTuple&& previousMembers, std::index_sequence<INDICES...>)
{
	(CopyMemberEntryRows(iCount, std::get<INDICES>(currentMembers), std::get<INDICES>(previousMembers)), ...);
}

// Copies corresponding member arrays in stable tuple order, and array entries in stable index order.
template <typename TCurrentTuple, typename TPreviousTuple>
void CopyMemberRows(int64_t iCount, TCurrentTuple&& currentMembers, TPreviousTuple&& previousMembers)
{
	using CurrentTuple = std::remove_reference_t<TCurrentTuple>;
	using PreviousTuple = std::remove_reference_t<TPreviousTuple>;
	constexpr size_t kuiCurrentSize = std::tuple_size_v<CurrentTuple>;
	constexpr size_t kuiPreviousSize = std::tuple_size_v<PreviousTuple>;
	static_assert(kuiCurrentSize == kuiPreviousSize, "Corresponding collection member tuples must have matching arity");

	if constexpr (kuiCurrentSize == kuiPreviousSize)
	{
		if (iCount > 0)
		{
			CopyMemberRows(iCount, std::forward<TCurrentTuple>(currentMembers), std::forward<TPreviousTuple>(previousMembers), std::make_index_sequence<kuiCurrentSize> {});
		}
	}
}

// Allocates the exact previous-frame capacity, then copies either PersistentMembers() or all Members().
template <typename TStruct>
void AllocateAndCopyMembers(TStruct& rCurrent, const TStruct& rPrevious)
{
	Allocate(rCurrent, rPrevious, rCurrent.Members());

	if constexpr (HasPersistentMembers<TStruct>)
	{
		ASSERT(IsMemberTupleSubset(rCurrent.PersistentMembers(), rCurrent.Members()));
		CopyMemberRows(rCurrent.iCount, rCurrent.PersistentMembers(), rPrevious.PersistentMembers());
	}
	else
	{
		CopyMemberRows(rCurrent.iCount, rCurrent.Members(), rPrevious.Members());
	}
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

// Grows capacity while preserving existing data.
template <typename TStruct, typename TTuple>
void GrowCapacityWithCopy(TStruct& rStruct, int64_t iNewCapacity, int64_t iCurrentCount, TTuple&& members)
{
	// Heap: MakeAligned for a larger SOA buffer that replaces the old one. The buffer persists across frames
	// and grows with entity count, so workbuffer (lost on Pop) and static arrays (fixed size) don't work.
	ScopedSuppressAllocationTracking suppress;
	int64_t iBufferSize = std::max<int64_t>(MemberTupleBufferSize(iNewCapacity, members), 1);

	common::AlignedUniquePtr<std::byte> pNewData = common::MakeAligned<std::byte>(iBufferSize);
	if (pNewData == nullptr)
	{
		throw std::bad_alloc();
	}

	std::byte* pCurrent = pNewData.get();
	std::apply([&](auto&... memberPtrRefs)
	{
		(AssignAndCopyAligned(memberPtrRefs, iNewCapacity, iCurrentCount, pCurrent), ...);
	}, std::forward<TTuple>(members));
	rStruct.pData = std::move(pNewData);
	rStruct.iCapacity = iNewCapacity;
	rStruct.iPhysicalLayoutCapacity = iNewCapacity;
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
		(ForEachMemberPointer(memberPtrRefs, [&](auto& elementPtrRef)
		{
			elementPtrRef[i] = elementPtrRef[rStruct.iCount - 1];
		}), ...);
	}, std::forward<TTuple>(members));
}

// Value-initializes every Members() entry at one row before collection-specific defaults are applied.
template <typename TTuple>
void ZeroMemberRow(int64_t i, TTuple&& members)
{
	std::apply([&](auto&... rMemberPointers)
	{
		(ForEachMemberPointer(rMemberPointers, [&](auto& rElementPointer)
		{
			rElementPointer[i] = {};
		}), ...);
	}, std::forward<TTuple>(members));
}

} // namespace engine
