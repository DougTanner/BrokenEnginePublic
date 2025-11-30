#pragma once

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
class GltfPipeline;
class PipelineManager;
class SwapchainManager;
class TextureManager;

extern BufferManager* gpBufferManager;
extern CommandBufferManager* gpCommandBufferManager;
extern PipelineManager* gpPipelineManager;
extern SwapchainManager* gpSwapchainManager;
extern TextureManager* gpTextureManager;

enum class CommandBufferFlags : uint8_t;

// Global unique identifier with counter stored in FramePostRenderBase
// 0 = invalid/uninitialized, counter starts at 1
struct uuid_t
{
	int64_t iValue = 0;

	// Construction
	constexpr uuid_t() = default;
	constexpr explicit uuid_t(int64_t iVal) : iValue(iVal) {}

	// Generate next unique ID (counter stored in FramePostRenderBase::uiNextUuid)
	template<typename T>
	static uuid_t Generate(T& rFramePostRender)
	{
		return uuid_t {rFramePostRender.iNextUuid++};
	}

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
template <typename Tag>
struct id_t
{
	uuid_t uuid {};

	// Construction
	constexpr id_t() = default;
	constexpr explicit id_t(uuid_t u)
	: uuid(u)
	{
	}

	// Generate next unique ID (counter stored in FramePostRenderBase::uiNextUuid)
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
constexpr int64_t CalculateBufferSize(int64_t iCapacity, const T& member)
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
template <typename TStruct, typename TTuple>
void AllocateAndAssign(TStruct& rStruct, int64_t iCapacity, TTuple&& members)
{
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
template<typename T, typename = void>
struct HasIdToIndex : std::false_type {};

template<typename T>
struct HasIdToIndex<T, std::void_t<decltype(std::declval<T>().idToIndexMap)>> : std::true_type {};

template<typename T>
inline constexpr bool HasIdToIndex_v = HasIdToIndex<T>::value;

// Synchronizes current frame storage with previous frame capacity. Automatically copies indexable state.
// Returns false if previous data was null (signals early return from Update()), true otherwise.
template <typename TStruct, typename TTuple>
bool ReallocateIfCapacityChanged(TStruct& rCurrent, const TStruct& rPrevious, TTuple&& members)
{
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
void ReallocateAndCopyMetadata(TStruct& rCurrent, const TStruct& rPrevious, TTuple&& members)
{
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
}

// Grows capacity while preserving existing data. Growth strategy: 2 * capacity + 1.
template <typename TStruct, typename TTuple>
void GrowCapacityWithCopy(TStruct& rStruct, int64_t iNewCapacity, int64_t iCurrentCount, TTuple&& members)
{
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
template <typename TInterpolate, typename TPostRender, typename TFramePostRender>
std::tuple<int64_t, typename TInterpolate::id_t> AddIndexableElement(TInterpolate& rInterpolate, TPostRender& rPostRender, TFramePostRender& rFramePostRender)
{
	int64_t iSpawnIndex = AddElement(rInterpolate, rPostRender);

	using id_t = typename TInterpolate::id_t;
	id_t newId = id_t::Generate(rFramePostRender);
	rInterpolate.idToIndexMap[newId] = iSpawnIndex;

	return {iSpawnIndex, newId};
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
		auto lastId = rPostRender.puiIds[rInterpolate.iCount - 1];

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
		AllocateAndAssign(rStruct, rStruct.iCapacity, std::forward<TTuple>(members));
	}
	else
	{
		ResetDataToNull(rStruct, std::forward<TTuple>(members));
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

// Layout sizes for Renderable mixin (must match shaders::QuadLayout, shaders::GltfLayout, shaders::VisibleLightQuadLayout, shaders::AxisAlignedQuadLayout)
inline constexpr VkDeviceSize kQuadLayoutSize = 160;
inline constexpr VkDeviceSize kGltfLayoutSize = 128;
inline constexpr VkDeviceSize kVisibleLightQuadLayoutSize = 176;
inline constexpr VkDeviceSize kAxisAlignedQuadLayoutSize = 64;

enum class RenderableFlags : uint32_t
{
	kGltf                 = 0x0001,   // glTF mode (implies GltfLayout)
	kGltfShadow           = 0x0002,   // glTF mode with shadow pipeline (implies GltfLayout)
	kLighting             = 0x0004,   // Lighting mode (implies QuadLayout)
	kVisibleLights        = 0x0008,   // Lighting mode: create visible lights pipeline
	kAxisAlignedLighting  = 0x0010,   // Axis-aligned lighting mode (implies AxisAlignedQuadLayout)
};
using RenderableFlags_t = common::Flags<RenderableFlags>;

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
// RENDERABLE MIXIN
// ============================================================================
// Provides dynamic buffer management for collections that render to GPU via pipelines.
// Supports both glTF pipelines (default) and lighting pipelines (via kLighting flag).
// Template parameters provide explicit configuration instead of requiring derived class constants.
// FLAGS controls mode and features: kGltf/kGltfShadow (glTF), kLighting + kVisibleLights (lighting).

template <typename T, common::Flags<RenderableFlags> FLAGS, common::crc_t GLTF_CRC = 0, common::crc_t GLTF_MODEL_CRC = 0>
struct Renderable
{
	static constexpr common::crc_t kCrc = common::Crc(T::kpcName);
	static constexpr VkDeviceSize kLayoutSize =
		(FLAGS & RenderableFlags::kAxisAlignedLighting) ? kAxisAlignedQuadLayoutSize :
		(FLAGS & RenderableFlags::kLighting) ? kQuadLayoutSize : kGltfLayoutSize;
	static constexpr common::crc_t kGltfCrc = GLTF_CRC;
	static constexpr common::crc_t kGltfModelCrc = GLTF_MODEL_CRC;
	static constexpr common::Flags<RenderableFlags> kFlags = FLAGS;

	// Creates dynamic storage buffer with minimal initial size.
	// Called from derived class AllocateGraphicsResources().
	// Returns pointer to buffer array for pipeline creation.
	static inline Buffer* AllocateDynamicBuffer()
	{
		return gpBufferManager->CreateDynamicBuffer(kCrc, T::kpcName, kLayoutSize);
	}

	// Creates dynamic storage buffer and pipelines based on mode.
	// glTF mode: Creates glTF pipeline + optional shadow pipeline.
	// Lighting mode: Creates lighting pipeline + optional visible lights pipeline.
	// Axis-aligned lighting mode: Creates axis-aligned lighting pipeline.
	// Called from derived class AllocateGraphicsResources().
	static inline void AllocatePipelines()
	{
		Buffer* pStorageBuffers = AllocateDynamicBuffer();
		if constexpr (kFlags & RenderableFlags::kAxisAlignedLighting)
		{
			engine::gpPipelineManager->CreateDynamicPipelineAxisAlignedLighting(kCrc, T::kpcName, kLayoutSize);
			if constexpr (kFlags & RenderableFlags::kVisibleLights)
			{
				Buffer* pVisibleLightsBuffers = gpBufferManager->CreateDynamicVisibleLightsBuffer(kCrc, T::kpcName, kVisibleLightQuadLayoutSize);
				engine::gpPipelineManager->CreateDynamicPipelineVisibleLights(kCrc, T::kpcName, pVisibleLightsBuffers);
			}
		}
		else if constexpr (kFlags & RenderableFlags::kLighting)
		{
			engine::gpPipelineManager->CreateDynamicPipelineLighting(kCrc, T::kpcName, kLayoutSize);
			if constexpr (kFlags & RenderableFlags::kVisibleLights)
			{
				Buffer* pVisibleLightsBuffers = gpBufferManager->CreateDynamicVisibleLightsBuffer(kCrc, T::kpcName, kVisibleLightQuadLayoutSize);
				engine::gpPipelineManager->CreateDynamicPipelineVisibleLights(kCrc, T::kpcName, pVisibleLightsBuffers);
			}
		}
		else if constexpr (kFlags & RenderableFlags::kGltf)
		{
			engine::gpPipelineManager->CreateDynamicGltfPipeline(kCrc, T::kpcName, kGltfCrc, kGltfModelCrc, pStorageBuffers);
			if constexpr (kFlags & RenderableFlags::kGltfShadow)
			{
				engine::gpPipelineManager->CreateDynamicGltfPipelineShadow(kCrc, T::kpcName, kGltfCrc, kGltfModelCrc, pStorageBuffers);
			}
		}
	}

	// Backward-compatible alias for glTF mode.
	// Called from derived class AllocateGraphicsResources().
	static inline void AllocateGltfPipelines()
	{
		static_assert(!(kFlags & RenderableFlags::kLighting), "Use AllocatePipelines() for lighting mode");
		AllocatePipelines();
	}

	// Checks if buffer resize needed and updates descriptor sets.
	// Uses VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT so no command buffer re-recording needed.
	// Called from derived class Render() method.
	static inline void ResizeBufferUpdateDescriptor(const T& rCollection, int64_t iCommandBuffer)
	{
		VkDeviceSize requiredSize = kLayoutSize * rCollection.iCapacity;
		Buffer& rBuffer = gpBufferManager->mDynamicStorageBuffers.at(kCrc).at(iCommandBuffer);
		if (rBuffer.mInfo.dataVkDeviceSize >= requiredSize)
		{
			return;
		}

		gpBufferManager->ResizeDynamicBuffer(kCrc, T::kpcName, requiredSize, iCommandBuffer);

		int64_t iFramebuffer = iCommandBuffer;

		if constexpr (kFlags & RenderableFlags::kAxisAlignedLighting)
		{
			// Axis-aligned lighting pipeline has storage buffer at binding 1 (binding 2 is sampler)
			gpPipelineManager->mDynamicPipelinesAxisAlignedLightingMap.at(kCrc)->UpdateStorageBufferDescriptor(iFramebuffer, 1, &rBuffer);
			if constexpr (kFlags & RenderableFlags::kVisibleLights)
			{
				// Visible lights pipeline has storage buffer at binding 2
				VkDeviceSize visibleLightsRequiredSize = kVisibleLightQuadLayoutSize * rCollection.iCapacity;
				Buffer& rVisibleLightsBuffer = gpBufferManager->mDynamicVisibleLightsStorageBuffers.at(kCrc).at(iCommandBuffer);
				if (rVisibleLightsBuffer.mInfo.dataVkDeviceSize < visibleLightsRequiredSize)
				{
					gpBufferManager->ResizeDynamicVisibleLightsBuffer(kCrc, T::kpcName, visibleLightsRequiredSize, iCommandBuffer);
					gpPipelineManager->mDynamicPipelinesVisibleLightsMap.at(kCrc)->UpdateStorageBufferDescriptor(iFramebuffer, 2, &rVisibleLightsBuffer);
				}
			}
		}
		else if constexpr (kFlags & RenderableFlags::kLighting)
		{
			// Lighting pipeline has storage buffer at binding 1 (binding 2 is sampler)
			gpPipelineManager->mDynamicPipelinesLightingMap.at(kCrc)->UpdateStorageBufferDescriptor(iFramebuffer, 1, &rBuffer);
			if constexpr (kFlags & RenderableFlags::kVisibleLights)
			{
				// Visible lights pipeline has storage buffer at binding 2
				VkDeviceSize visibleLightsRequiredSize = kVisibleLightQuadLayoutSize * rCollection.iCapacity;
				Buffer& rVisibleLightsBuffer = gpBufferManager->mDynamicVisibleLightsStorageBuffers.at(kCrc).at(iCommandBuffer);
				if (rVisibleLightsBuffer.mInfo.dataVkDeviceSize < visibleLightsRequiredSize)
				{
					gpBufferManager->ResizeDynamicVisibleLightsBuffer(kCrc, T::kpcName, visibleLightsRequiredSize, iCommandBuffer);
					gpPipelineManager->mDynamicPipelinesVisibleLightsMap.at(kCrc)->UpdateStorageBufferDescriptor(iFramebuffer, 2, &rVisibleLightsBuffer);
				}
			}
		}
		else if constexpr (kFlags & RenderableFlags::kGltf)
		{
			gpPipelineManager->mDynamicGltfPipelineMap.at(kCrc)->UpdateStorageBufferDescriptors(iFramebuffer, 2, &rBuffer);
			if constexpr (kFlags & RenderableFlags::kGltfShadow)
			{
				gpPipelineManager->mDynamicGltfPipelineShadowMap.at(kCrc)->UpdateStorageBufferDescriptors(iFramebuffer, 2, &rBuffer);
			}
		}
	}

	// Writes indirect buffer counts to all pipelines.
	// Called from derived class Render() method.
	static inline void WritePipelineIndirectBuffers(int64_t iCommandBuffer, int64_t iCount)
	{
		if constexpr (kFlags & RenderableFlags::kAxisAlignedLighting)
		{
			gpPipelineManager->mDynamicPipelinesAxisAlignedLightingMap.at(kCrc)->WriteIndirectBuffer(iCommandBuffer, iCount);
			if constexpr (kFlags & RenderableFlags::kVisibleLights)
			{
				gpPipelineManager->mDynamicPipelinesVisibleLightsMap.at(kCrc)->WriteIndirectBuffer(iCommandBuffer, iCount);
			}
		}
		else if constexpr (kFlags & RenderableFlags::kLighting)
		{
			gpPipelineManager->mDynamicPipelinesLightingMap.at(kCrc)->WriteIndirectBuffer(iCommandBuffer, iCount);
			if constexpr (kFlags & RenderableFlags::kVisibleLights)
			{
				gpPipelineManager->mDynamicPipelinesVisibleLightsMap.at(kCrc)->WriteIndirectBuffer(iCommandBuffer, iCount);
			}
		}
		else if constexpr (kFlags & RenderableFlags::kGltf)
		{
			gpPipelineManager->mDynamicGltfPipelineMap.at(kCrc)->WriteIndirectBuffer(iCommandBuffer, iCount);
			if constexpr (kFlags & RenderableFlags::kGltfShadow)
			{
				gpPipelineManager->mDynamicGltfPipelineShadowMap.at(kCrc)->WriteIndirectBuffer(iCommandBuffer, iCount);
			}
		}
	}
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
