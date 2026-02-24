#pragma once

#include "Memory/MemoryManager.h"

namespace game
{

struct Frame;
struct FrameInput;
struct FrameInterpolate;
struct FramePostRender;

}

using VkDeviceSize = uint64_t;

namespace engine
{

struct FrameBase;
struct FramePostRenderBase;

class Buffer;
class BufferManager;
class CommandBufferManager;
class ModelPipeline;
class PipelineManager;
class SwapchainManager;
class TextureManager;

enum class CommandBufferFlags : uint8_t;

// Global unique identifier with counter stored in FramePostRenderBase
// 0 = invalid/uninitialized, counter starts at 1
struct uuid_t
{
	int64_t iValue = 0;

	// Construction
	constexpr uuid_t() = default;
	constexpr explicit uuid_t(int64_t iVal) : iValue(iVal) {}

	// Generate next unique ID (counter stored in FramePostRenderBase)
	static uuid_t Generate(FramePostRenderBase& rFramePostRender);

	// Check validity
	constexpr bool IsValid() const
	{
		return iValue != 0;
	}

	// Explicit value access
	constexpr int64_t Value() const
	{
		return iValue;
	}

	// Comparison operators
	constexpr bool operator==(const uuid_t& other) const = default;
	constexpr auto operator<=>(const uuid_t& other) const = default;

	// Serialization support
	void Write(std::ostream& stream) const { common::Write(stream, iValue); }
	void Read(std::istream& stream) { common::Read(stream, iValue); }
};

// Strong-typed ID wrapper preventing implicit conversions between different collection types
// Tag parameter ensures AreaLights::id_t cannot be mixed with Sounds::id_t
template <typename T>
struct id_t
{
	uuid_t uuid {};

	// Construction
	constexpr id_t() = default;
	constexpr explicit id_t(uuid_t u)
	: uuid(u)
	{
	}

	// Generate next unique ID (counter stored in FramePostRenderBase)
	static id_t Generate(FramePostRenderBase& rFramePostRender)
	{
		return id_t {uuid_t::Generate(rFramePostRender)};
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
		return std::hash<int64_t>{}(id.iValue);
	}
};

template <typename T>
struct hash<engine::id_t<T>>
{
	size_t operator()(const engine::id_t<T>& id) const noexcept
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
constexpr int64_t CalculateBufferSize(int64_t iCapacity, [[maybe_unused]] const T& member)
{
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
	ScopedSuppressAllocationTracking suppressAllocationTracking;
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

// Synchronizes current frame storage with previous frame capacity. Automatically copies indexable state.
// Returns false if previous data was null (signals early return from Update()), true otherwise.
template <typename TStruct, typename TTuple>
bool ReallocateIfCapacityChanged(TStruct& rCurrent, const TStruct& rPrevious, TTuple&& members)
{
	// Heap: MakeAligned for the SOA buffer and unordered_map copy for idToIndexMap. Both persist across frames
	// with sizes that vary at runtime based on entity count, so neither workbuffer nor static arrays work.
	ScopedSuppressAllocationTracking suppressAllocationTracking;
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
	// Heap: MakeAligned for the SOA buffer and unordered_map copy for idToIndexMap. Both persist across frames
	// with sizes that vary at runtime based on entity count, so neither workbuffer nor static arrays work.
	ScopedSuppressAllocationTracking suppressAllocationTracking;
	rCurrent.iCount = rPrevious.iCount;

	// Copy indexable state if applicable
	if constexpr (HasIdToIndex_v<TStruct>)
	{
		rCurrent.idToIndexMap = rPrevious.idToIndexMap;
	}

	if (rPrevious.pData == nullptr)
	{
		ResetDataToNull(rCurrent, std::forward<TTuple>(members));
		return;
	}

	int64_t iCapacity = rPrevious.iCapacity;
	if (rCurrent.iCapacity != iCapacity || rCurrent.pData == nullptr)
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
}

// Grows capacity while preserving existing data. Growth strategy: 2 * capacity + 1.
template <typename TStruct, typename TTuple>
void GrowCapacityWithCopy(TStruct& rStruct, int64_t iNewCapacity, int64_t iCurrentCount, TTuple&& members)
{
	// Heap: MakeAligned for a larger SOA buffer that replaces the old one. The buffer persists across frames
	// and grows with entity count, so workbuffer (lost on Pop) and static arrays (fixed size) don't work.
	ScopedSuppressAllocationTracking suppressAllocationTracking;
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
	ScopedSuppressAllocationTracking suppressAllocationTracking;
	int64_t iSpawnIndex = AddElement(rInterpolate, rPostRender);

	using id_t = typename TInterpolate::id_t;
	id_t newId = id_t::Generate(rFramePostRender);
	rInterpolate.idToIndexMap[newId] = iSpawnIndex;

	return {iSpawnIndex, newId};
}

// Increments counts, reuses an existing ID, and updates idToIndexMap for indexable collections.
// Returns tuple of (spawnIndex, existingId).
// Usage: auto [uiIndex, id] = AddIndexableElementWithId(rInterpolate, rPostRender, existingId);
template <typename TInterpolate, typename TPostRender>
std::tuple<int64_t, typename TInterpolate::id_t> AddIndexableElementWithId(TInterpolate& rInterpolate, TPostRender& rPostRender, typename TInterpolate::id_t existingId)
{
	// Heap: unordered_map::operator[] may allocate a new bucket or node for the ID-to-index entry.
	// The map must persist across frames for stable ID lookups, so workbuffer and static arrays are not viable.
	ScopedSuppressAllocationTracking suppressAllocationTracking;
	int64_t iSpawnIndex = AddElement(rInterpolate, rPostRender);
	rInterpolate.idToIndexMap[existingId] = iSpawnIndex;
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

		rInterpolate.idToIndexMap[lastId] = iIndex;
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

// ============================================================================
// MULTI-ARRAY SERIALIZATION HELPERS
// ============================================================================
// Synchronized operations on parallel arrays using fold expressions.

// Computes XOR'd CRC of multiple member arrays for deterministic replay validation.
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
					constexpr size_t N = std::extent_v<std::remove_reference_t<decltype(memberPtrRefs)>>;
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
				constexpr size_t N = std::extent_v<std::remove_reference_t<decltype(memberPtrRefs)>>;
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
				constexpr size_t N = std::extent_v<std::remove_reference_t<decltype(memberPtrRefs)>>;
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
template <typename TStruct, typename TTuple>
void AllocateAndRead(TStruct& rStruct, std::istream& rStream, TTuple&& members)
{
	if (rStruct.iCapacity > 0)
	{
		AllocateAndAssign(rStruct, rStruct.iCapacity, members);
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

// ============================================================================
// CONTROLLER TYPES FOR KEYFRAME ANIMATION
// ============================================================================
// Reusable keyframe animation system for collections that need time-based property interpolation.

inline constexpr int64_t kMaxControllerKeyframes = 4;
inline constexpr uint8_t kuiInvalidControllerType = 0xFF;

// Keyframe state with lerp-able properties for light animation
struct ControllerKeyframe
{
	float fVisibleArea = 0.0f;
	float fVisibleIntensity = 0.0f;
	float fLightingArea = 0.0f;
	float fLightingIntensity = 0.0f;
	float fRotation = 0.0f;

	static ControllerKeyframe Lerp(const ControllerKeyframe& rA, const ControllerKeyframe& rB, float fPercent)
	{
		return
		{
			.fVisibleArea = std::lerp(rA.fVisibleArea, rB.fVisibleArea, fPercent),
			.fVisibleIntensity = std::lerp(rA.fVisibleIntensity, rB.fVisibleIntensity, fPercent),
			.fLightingArea = std::lerp(rA.fLightingArea, rB.fLightingArea, fPercent),
			.fLightingIntensity = std::lerp(rA.fLightingIntensity, rB.fLightingIntensity, fPercent),
			.fRotation = std::lerp(rA.fRotation, rB.fRotation, fPercent),
		};
	}

	bool operator==(const ControllerKeyframe& rOther) const = default;
};

// Controller type defining animation behavior
struct ControllerType
{
	uint8_t uiBaseTypeIndex = 0;                                // Base Type for color/texture
	uint8_t uiKeyframeCount = 2;                                // Actual keyframes used (2-4)
	bool bDestroysSelf = true;                                  // Auto-remove when animation ends
	float pfTimes[kMaxControllerKeyframes] {};                  // Keyframe times (relative to start)
	ControllerKeyframe keyframes[kMaxControllerKeyframes] {};   // Keyframe states

	bool operator==(const ControllerType& rOther) const = default;
};

// Interpolates between keyframes based on elapsed time
inline ControllerKeyframe InterpolateKeyframes(const ControllerType& rController, float fElapsedTime)
{
	int64_t iKeyframeCount = rController.uiKeyframeCount;

	if (fElapsedTime <= rController.pfTimes[0])
	{
		return rController.keyframes[0];
	}
	if (fElapsedTime >= rController.pfTimes[iKeyframeCount - 1])
	{
		return rController.keyframes[iKeyframeCount - 1];
	}

	for (int64_t j = 1; j < iKeyframeCount; ++j)
	{
		if (fElapsedTime < rController.pfTimes[j])
		{
			float fPreviousTime = rController.pfTimes[j - 1];
			float fPercent = (fElapsedTime - fPreviousTime) / (rController.pfTimes[j] - fPreviousTime);
			return ControllerKeyframe::Lerp(rController.keyframes[j - 1], rController.keyframes[j], fPercent);
		}
	}

	return rController.keyframes[iKeyframeCount - 1];
}

// Mixin providing static controller type registry for collections with keyframe animation.
// TControllerType defaults to ControllerType for standard keyframe animation (PointLights).
// Collections with custom keyframes (Puffs) can specify their own controller type.
template <typename TCollection, typename TControllerType = ControllerType>
struct ControllerTypeRegistry
{
	static inline std::vector<TControllerType> sControllerTypes;

	static void RegisterControllerType(uint8_t& ruiIndex, const TControllerType& rType)
	{
		ASSERT(ruiIndex == 0xFF);
		ruiIndex = static_cast<uint8_t>(sControllerTypes.size());
		sControllerTypes.push_back(rType);
	}

	static const TControllerType& GetControllerType(uint8_t uiIndex)
	{
		return sControllerTypes.at(uiIndex);
	}
};

// Request lazy-load of a texture chunk by CRC (implemented in FileManager.cpp)
void RequestTextureChunkLoad(common::crc_t crc);

// Mixin providing static type registry for collections with type-based configuration sharing.
// Type is passed as template parameter (must be defined before collection).
template <typename TType>
struct TypeRegistry
{
	using Type = TType;
	static inline std::vector<TType> sTypes;

	static void RegisterType(uint8_t& ruiIndex, const TType& rType)
	{
		ASSERT(ruiIndex == 0xFF);
		ruiIndex = static_cast<uint8_t>(sTypes.size());
		sTypes.push_back(rType);

		if constexpr (requires { rType.crc; })
		{
			if (rType.crc != 0)
			{
				RequestTextureChunkLoad(rType.crc);
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
struct OptionaldToIndex
{
};

// Indexable version with strong-typed id_t and ID-to-index mapping using CRTP pattern.
template <typename T, common::Flags<CollectionFlags> FLAGS>
	requires (FLAGS & CollectionFlags::kIdToIndex)
struct OptionaldToIndex<T, FLAGS>
{
	using id_t = engine::id_t<T>;

	std::unordered_map<id_t, int64_t> idToIndexMap;

	inline int64_t IdToIndex(id_t id) const
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
		// Heap: GetSortedKeys() builds a temporary vector of all map keys for deterministic write ordering.
		// Could use workbuffer, but save/replay is infrequent so the simplicity of std::vector wins here.
		ScopedSuppressAllocationTracking suppressAllocationTracking;
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
		ScopedSuppressAllocationTracking suppressAllocationTracking;
		int64_t iSize = 0;
		common::Read(rStream, iSize);
		idToIndexMap.clear();
		idToIndexMap.reserve(iSize);
		for (int64_t i = 0; i < iSize; ++i)
		{
			id_t key{};
			int64_t value{};
			key.Read(rStream);
			common::Read(rStream, value);
			idToIndexMap[key] = value;
		}
	}

	inline common::crc_t Crc() const
	{
		// Heap: GetSortedKeys() builds a temporary vector of all map keys for deterministic CRC ordering.
		// Could use workbuffer, but replay validation is infrequent so the simplicity of std::vector wins here.
		ScopedSuppressAllocationTracking suppressAllocationTracking;
		common::crc_t checksum = 0;
		checksum ^= common::Crc(static_cast<int64_t>(idToIndexMap.size()));

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

template <typename T, common::Flags<CollectionFlags> FLAGS = {}>
struct Collection : public OptionaldToIndex<T, FLAGS>
{
	inline bool operator==(const Collection& rOther) const
	{
		bool bEqual = true;
		if constexpr (FLAGS & CollectionFlags::kIdToIndex)
		{
			bEqual &= common::BreakOnNotEqual(static_cast<const OptionaldToIndex<T, FLAGS>&>(*this), static_cast<const OptionaldToIndex<T, FLAGS>&>(rOther));
		}
		bEqual &= common::BreakOnNotEqual(iCount, rOther.iCount);
		bEqual &= common::BreakOnNotEqual(iCapacity, rOther.iCapacity);
		return bEqual;
	}

	inline void Write(std::ostream& rStream) const
	{
		common::Write(rStream, iCount);
		common::Write(rStream, iCapacity);
		if constexpr (FLAGS & CollectionFlags::kIdToIndex)
		{
			static_cast<const OptionaldToIndex<T, FLAGS>&>(*this).Write(rStream);
		}
	}

	inline void Read(std::istream& rStream)
	{
		common::Read(rStream, iCount);
		common::Read(rStream, iCapacity);
		if constexpr (FLAGS & CollectionFlags::kIdToIndex)
		{
			static_cast<OptionaldToIndex<T, FLAGS>&>(*this).Read(rStream);
		}
	}

	inline common::crc_t Crc() const
	{
		common::crc_t checksum = 0;
		if constexpr (FLAGS & CollectionFlags::kIdToIndex)
		{
			checksum ^= static_cast<const OptionaldToIndex<T, FLAGS>&>(*this).Crc();
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
// High-level API functions for complete collection operations.

// Computes complete CRC of collection (metadata + all member arrays) for deterministic replay validation.
template <typename TStruct, typename TTuple>
inline common::crc_t CollectionCrc(const TStruct& rCurrent, TTuple&& members)
{
	common::crc_t checksum = rCurrent.Crc();
	checksum ^= engine::MultiCrc(rCurrent.iCount, std::forward<TTuple>(members));
	return checksum;
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
	rCurrent.Read(rStream);
	engine::AllocateAndRead(rCurrent, rStream, std::forward<TTuple>(members));
	return rStream;
}

} // namespace engine
