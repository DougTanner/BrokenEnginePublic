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
struct GridCoord;

class Buffer;
class BufferManager;
class CommandBufferManager;
class ModelPipeline;
class PipelineManager;
class SwapchainManager;
class TextureManager;

enum class CommandBufferFlags : uint8_t;

// Stable entity identity across transfers and reconnects
// Assigned by server at first spawn, carried in TransferData
struct global_id_t
{
	int64_t iValue = 0;
	constexpr bool IsValid() const { return iValue != 0; }
	bool operator==(const global_id_t&) const = default;
};

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
#if defined(BT_CLIENT)
	static uuid_t GenerateVisual(FramePostRenderBase& rFramePostRender);
#endif

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

#if defined(BT_CLIENT)
	static id_t GenerateVisual(FramePostRenderBase& rFramePostRender)
	{
		return id_t {uuid_t::GenerateVisual(rFramePostRender)};
	}
#endif

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

#include "CollectionMemory.h"

namespace engine
{

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
					static constexpr size_t N = std::extent_v<std::remove_reference_t<decltype(memberPtrRefs)>>;
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
template <typename TControllerType>
inline auto InterpolateKeyframes(const TControllerType& rController, float fElapsedTime)
	-> std::remove_extent_t<decltype(TControllerType::keyframes)>
{
	using KeyframeType = std::remove_extent_t<decltype(TControllerType::keyframes)>;
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
			return KeyframeType::Lerp(rController.keyframes[j - 1], rController.keyframes[j], fPercent);
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

#if defined(BT_CLIENT)
// Register a CRC for pre-blur (implemented in TextureManager.cpp)
void RegisterLightingTextureCrc(common::crc_t crc);
#endif

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
#if defined(BT_CLIENT)
				RegisterLightingTextureCrc(rType.crc);
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
		checksum ^= common::Crc(static_cast<int64_t>(idToIndexMap.size()));

		int64_t iKeyCount = static_cast<int64_t>(idToIndexMap.size());
		id_t* pKeys = common::gpThreadLocal->mWorkbuffer.PushBuffer<id_t*>(iKeyCount * sizeof(id_t));
		int64_t i = 0;
		for (const auto& [key, value] : idToIndexMap)
		{
			pKeys[i++] = key;
		}
		std::sort(pKeys, pKeys + iKeyCount);

		for (int64_t j = 0; j < iKeyCount; ++j)
		{
			checksum ^= common::Crc(pKeys[j].ToUuid().Value());
			checksum ^= common::Crc(idToIndexMap.at(pKeys[j]));
		}
		common::gpThreadLocal->mWorkbuffer.Pop();

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
		if constexpr (FLAGS & CollectionFlags::kIdToIndex)
		{
			static_cast<OptionalIdToIndex<T, FLAGS>&>(*this).Read(rStream);
		}
	}

	inline common::crc_t Crc() const
	{
		common::crc_t checksum = 0;
		if constexpr (FLAGS & CollectionFlags::kIdToIndex)
		{
			checksum ^= static_cast<const OptionalIdToIndex<T, FLAGS>&>(*this).Crc();
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

// Computes XOR'd CRC of a single element across all member arrays for per-element desync diagnosis.
template <typename TTuple>
common::crc_t MultiElementCrc(int64_t iIndex, TTuple&& members)
{
	common::crc_t checksum = 0;
	std::apply([&](auto&... memberPtrRefs)
	{
		([&]()
		{
			if constexpr (std::is_array_v<std::remove_reference_t<decltype(memberPtrRefs)>>)
			{
				static constexpr size_t N = std::extent_v<std::remove_reference_t<decltype(memberPtrRefs)>>;
				for (size_t i = 0; i < N; ++i)
				{
					checksum ^= common::Crc(memberPtrRefs[i][iIndex]);
				}
			}
			else
			{
				checksum ^= common::Crc(memberPtrRefs[iIndex]);
			}
		}(), ...);
	}, std::forward<TTuple>(members));
	return checksum;
}

template <typename T>
concept HasSharedMembers = requires(const T t) { t.SharedMembers(); };

template <typename TStruct>
inline common::crc_t SharedCollectionCrc(const TStruct& rCurrent)
{
	if constexpr (HasSharedMembers<TStruct>)
		return CollectionCrc(rCurrent, rCurrent.SharedMembers());
	else
		return CollectionCrc(rCurrent, rCurrent.Members());
}

template <typename TStruct>
inline common::crc_t SharedCollectionElementCrc(const TStruct& rCurrent, int64_t iIndex)
{
	if constexpr (HasSharedMembers<TStruct>)
		return engine::MultiElementCrc(iIndex, rCurrent.SharedMembers());
	else
		return engine::MultiElementCrc(iIndex, rCurrent.Members());
}

// Reads collection from a server-format stream. Allocates full Members() (zero-initialized) so client-only
// pointers are valid, then reads only SharedMembers() from the stream to match what the server wrote.
template <typename TStruct>
inline std::istream& SharedCollectionRead(std::istream& rStream, TStruct& rCurrent)
{
	rCurrent.Read(rStream);

	if (rCurrent.iCapacity > 0)
	{
		decltype(rCurrent.Members()) fullMembers = rCurrent.Members();
		AllocateAndAssign(rCurrent, rCurrent.iCapacity, fullMembers);

		// Zero the buffer so client-only fields default to 0 (invalid IDs, null references)
		int64_t iBufferSize = 0;
		std::apply([&](const auto&... memberPtrRefs)
		{
			((iBufferSize += CalculateBufferSize(rCurrent.iCapacity, memberPtrRefs)), ...);
		}, fullMembers);
		std::memset(rCurrent.pData.get(), 0, iBufferSize);
	}
	else
	{
		ResetDataToNull(rCurrent, rCurrent.Members());
	}

	if constexpr (HasSharedMembers<TStruct>)
		MultiRead(rStream, rCurrent.iCount, rCurrent.SharedMembers());
	else
		MultiRead(rStream, rCurrent.iCount, rCurrent.Members());

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
	rCurrent.Read(rStream);
	engine::AllocateAndRead(rCurrent, rStream, std::forward<TTuple>(members));
	return rStream;
}

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
	ScopedSuppressAllocationTracking scopedSuppressAllocationTracking;
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

// Removes controlled elements whose keyframe animation has expired.
// removeFn signature: void(TInterpolate&, TPostRender&, int64_t& i)
template <typename TInterpolate, typename TPostRender, typename TRemoveFn>
void DestroyExpiredControlled(TInterpolate& rInterpolate, TPostRender& rPostRender, float fCurrentTime, TRemoveFn removeFn)
{
	for (int64_t i = 0; i < rInterpolate.iCount; ++i)
	{
		uint8_t uiControllerTypeIndex = rInterpolate.puiControllerTypeIndices[i];
		if (uiControllerTypeIndex == kuiInvalidControllerType)
			continue;

		const auto& rController = TInterpolate::GetControllerType(uiControllerTypeIndex);
		if (!rController.bDestroysSelf)
			continue;

		float fElapsedTime = fCurrentTime - rInterpolate.pfStartTimes[i];
		if (fElapsedTime > rController.pfTimes[rController.uiKeyframeCount - 1]) [[unlikely]]
		{
			removeFn(rInterpolate, rPostRender, i);
		}
	}
}

} // namespace engine
