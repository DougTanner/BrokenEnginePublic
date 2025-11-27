#pragma once

namespace game
{

struct Frame;
struct FrameInput;
struct FrameInterpolate;
struct FramePostRender;

}

// VkDeviceSize is uint64_t in Vulkan - defined here to avoid including vulkan.h
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
	uint64_t uiValue = 0;

	// Construction
	constexpr uuid_t() = default;
	constexpr explicit uuid_t(uint64_t uiVal) : uiValue(uiVal) {}

	// Generate next unique ID (counter stored in FramePostRenderBase::uiNextUuid)
	template<typename T>
	static uuid_t Generate(T& rFramePostRender)
	{
		return uuid_t {rFramePostRender.uiNextUuid++};
	}

	// Check validity
	constexpr bool IsValid() const
	{
		return uiValue != 0;
	}

	// Explicit value access
	constexpr uint64_t Value() const
	{
		return uiValue;
	}

	// Comparison operators
	constexpr bool operator==(const uuid_t& other) const = default;
	constexpr auto operator<=>(const uuid_t& other) const = default;

	// Serialization support
	void Write(std::ostream& stream) const { common::Write(stream, uiValue); }
	void Read(std::istream& stream) { common::Read(stream, uiValue); }
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
		return std::hash<uint64_t>{}(id.uiValue);
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
constexpr uint64_t CalculateBufferSize(uint64_t uiCapacity, const T& member)
{
	if constexpr (std::is_array_v<T>)
	{
		// Array case: sum size for all array elements
		constexpr size_t N = std::extent_v<T>;
		using ElementPtrType = std::remove_extent_t<T>;
		using ElementType = std::remove_pointer_t<ElementPtrType>;

		return N * common::RoundUp<uint64_t, 64>(uiCapacity * sizeof(ElementType));
	}
	else
	{
		// Single pointer case
		using ElementType = std::remove_pointer_t<T>;
		return common::RoundUp<uint64_t, 64>(uiCapacity * sizeof(ElementType));
	}
}

// ============================================================================
// LOW-LEVEL MEMORY ALIGNMENT HELPERS
// ============================================================================
// Internal building blocks for 64-byte pointer alignment used by higher-level allocation helpers.

// Aligns pointer to 64-byte boundary and advances current position. Used during initial allocation.
template <typename T>
void AssignAligned(T& member, uint64_t uiCapacity, std::byte*& rpCurrent)
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
			rpCurrent += uiCapacity * sizeof(ElementType);
		}
	}
	else
	{
		// Single pointer case
		using ElementType = std::remove_pointer_t<T>;
		rpCurrent = reinterpret_cast<std::byte*>(common::RoundUp<uintptr_t, 64>(reinterpret_cast<uintptr_t>(rpCurrent)));
		member = reinterpret_cast<T>(rpCurrent);
		rpCurrent += uiCapacity * sizeof(ElementType);
	}
}

// Aligns pointer, copies existing data, and advances current position. Used during capacity growth.
template <typename T>
void AssignAndCopyAligned(T& member, uint64_t uiCapacity, uint64_t uiCount, std::byte*& rpCurrent)
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
				memcpy(rpCurrent, member[i], uiCount * sizeof(ElementType));
			}

			member[i] = reinterpret_cast<ElementPtrType>(rpCurrent);
			rpCurrent += uiCapacity * sizeof(ElementType);
		}
	}
	else
	{
		// Single pointer case
		using ElementType = std::remove_pointer_t<T>;
		rpCurrent = reinterpret_cast<std::byte*>(common::RoundUp<uintptr_t, 64>(reinterpret_cast<uintptr_t>(rpCurrent)));

		if (member != nullptr)
		{
			memcpy(rpCurrent, member, uiCount * sizeof(ElementType));
		}

		member = reinterpret_cast<T>(rpCurrent);
		rpCurrent += uiCapacity * sizeof(ElementType);
	}
}

// ============================================================================
// HIGH-LEVEL ALLOCATION & REALLOCATION HELPERS
// ============================================================================
// Orchestrate memory management for Structure-of-Arrays collections.

// Allocates single contiguous buffer and positions member array pointers within it. Used during initial allocation and deserialization.
template <typename TStruct, typename... TMemberPtrRefs>
void AllocateAndAssign(TStruct& rStruct, uint64_t uiCapacity, TMemberPtrRefs&... memberPtrRefs)
{
	uint64_t uiBufferSize = 0;
	((uiBufferSize += CalculateBufferSize(uiCapacity, memberPtrRefs)), ...);

	rStruct.uiCapacity = uiCapacity;
	rStruct.pData = common::MakeAligned<std::byte>(uiBufferSize);

	std::byte* pCurrent = rStruct.pData.get();
	(AssignAligned(memberPtrRefs, uiCapacity, pCurrent), ...);
}

// Resets collection to null state by releasing buffer and zeroing member pointers.
template <typename TStruct, typename... TMemberPtrRefs>
void ResetDataToNull(TStruct& rStruct, TMemberPtrRefs&... memberPtrRefs)
{
	rStruct.pData.reset();
	rStruct.uiCapacity = 0;

	// Null each member (handle arrays with loop, single pointers directly)
	([&]() {
		if constexpr (std::is_array_v<std::remove_reference_t<TMemberPtrRefs>>)
		{
			constexpr size_t N = std::extent_v<std::remove_reference_t<TMemberPtrRefs>>;
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
	rCurrent.uiCount = rPrevious.uiCount;

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

	const uint64_t uiCapacity = rPrevious.uiCapacity;
	if (rCurrent.uiCapacity != uiCapacity)
	{
		uint64_t uiBufferSize = 0;
		((uiBufferSize += CalculateBufferSize(uiCapacity, memberPtrRefs)), ...);

		rCurrent.uiCapacity = uiCapacity;
		rCurrent.pData = common::MakeAligned<std::byte>(uiBufferSize);

		std::byte* pCurrent = rCurrent.pData.get();
		(AssignAligned(memberPtrRefs, uiCapacity, pCurrent), ...);

		ASSERT(rCurrent.uiCount <= rCurrent.uiCapacity);
	}

	return true;
}

// Copies metadata and reallocates buffer for AllocateAndCopy() phase. Does not return early on null data.
// Used in AllocateAndCopy() static methods to prepare collections before Update() phase.
template <typename TStruct, typename... TMemberPtrRefs>
void ReallocateAndCopyMetadata(TStruct& rCurrent, const TStruct& rPrevious, TMemberPtrRefs&... memberPtrRefs)
{
	rCurrent.uiCount = rPrevious.uiCount;

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

	const uint64_t uiCapacity = rPrevious.uiCapacity;
	if (rCurrent.uiCapacity != uiCapacity)
	{
		uint64_t uiBufferSize = 0;
		((uiBufferSize += CalculateBufferSize(uiCapacity, memberPtrRefs)), ...);

		rCurrent.uiCapacity = uiCapacity;
		rCurrent.pData = common::MakeAligned<std::byte>(uiBufferSize);

		std::byte* pCurrent = rCurrent.pData.get();
		(AssignAligned(memberPtrRefs, uiCapacity, pCurrent), ...);

		ASSERT(rCurrent.uiCount <= rCurrent.uiCapacity);
	}
}

// Grows capacity while preserving existing data. Growth strategy: 2 * capacity + 1.
template <typename TStruct, typename... TMemberPtrRefs>
void GrowCapacityWithCopy(TStruct& rStruct, uint64_t uiNewCapacity, uint64_t uiCurrentCount, TMemberPtrRefs&... memberPtrRefs)
{
	uint64_t uiBufferSize = 0;
	((uiBufferSize += CalculateBufferSize(uiNewCapacity, memberPtrRefs)), ...);

	common::AlignedUniquePtr<std::byte> pNewData = common::MakeAligned<std::byte>(uiBufferSize);
	std::byte* pCurrent = pNewData.get();
	(AssignAndCopyAligned(memberPtrRefs, uiNewCapacity, uiCurrentCount, pCurrent), ...);
	rStruct.pData = std::move(pNewData);
	rStruct.uiCapacity = uiNewCapacity;
}

// Returns new capacity (2 * capacity + 1) if growth needed for spawning, otherwise 0.
template <typename TCollection>
inline uint64_t CalculateGrowthCapacity(const TCollection& rCollection)
{
	if (rCollection.uiCount + 1 > rCollection.uiCapacity)
	{
		return 2 * rCollection.uiCapacity + 1;
	}
	return 0;
}

// Increments counts for paired Interpolate/PostRender collections and returns spawn index.
template <typename TInterpolate, typename TPostRender>
inline uint64_t IncrementCountsAndGetSpawnIndex(TInterpolate& rInterpolate, TPostRender& rPostRender)
{
	++rInterpolate.uiCount;
	++rPostRender.uiCount;
	uint64_t uiSpawnIndex = rInterpolate.uiCount - 1;
	ASSERT(uiSpawnIndex < rInterpolate.uiCapacity);
	return uiSpawnIndex;
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
		if constexpr (std::is_array_v<std::remove_reference_t<TMemberPtrRefs>>)
		{
			constexpr size_t N = std::extent_v<std::remove_reference_t<TMemberPtrRefs>>;
			for (size_t j = 0; j < N; ++j)
			{
				memberPtrRefs[j][i] = memberPtrRefs[j][rStruct.uiCount - 1];
			}
		}
		else
		{
			memberPtrRefs[i] = memberPtrRefs[rStruct.uiCount - 1];
		}
	}(), ...);
}

// ============================================================================
// MULTI-ARRAY SERIALIZATION HELPERS
// ============================================================================
// Synchronized operations on parallel arrays using fold expressions.

// Computes XOR'd CRC of multiple member arrays for deterministic replay validation.
template <typename... TMemberPtrRefs>
common::crc_t MultiCrc(uint64_t uiCount, TMemberPtrRefs&... memberPtrRefs)
{
	common::crc_t checksum = 0;
	if (uiCount > 0)
	{
		// Handle both arrays and single pointers
		([&]() {
			if constexpr (std::is_array_v<std::remove_reference_t<TMemberPtrRefs>>)
			{
				constexpr size_t N = std::extent_v<std::remove_reference_t<TMemberPtrRefs>>;
				for (size_t i = 0; i < N; ++i)
				{
					checksum ^= common::Crc(memberPtrRefs[i], uiCount);
				}
			}
			else
			{
				checksum ^= common::Crc(memberPtrRefs, uiCount);
			}
		}(), ...);
	}
	return checksum;
}

// Serializes multiple member arrays to stream in order.
template <typename... TMemberPtrRefs>
void MultiWrite(std::ostream& rStream, uint64_t uiCount, TMemberPtrRefs&... memberPtrRefs)
{
	// Handle both arrays and single pointers
	([&]() {
		if constexpr (std::is_array_v<std::remove_reference_t<TMemberPtrRefs>>)
		{
			constexpr size_t N = std::extent_v<std::remove_reference_t<TMemberPtrRefs>>;
			for (size_t i = 0; i < N; ++i)
			{
				common::Write(rStream, memberPtrRefs[i], uiCount);
			}
		}
		else
		{
			common::Write(rStream, memberPtrRefs, uiCount);
		}
	}(), ...);
}

// Deserializes multiple member arrays from stream (must match write order). Arrays must already be allocated.
template <typename... TMemberPtrRefs>
void MultiRead(std::istream& rStream, uint64_t uiCount, TMemberPtrRefs&... memberPtrRefs)
{
	// Handle both arrays and single pointers
	([&]() {
		if constexpr (std::is_array_v<std::remove_reference_t<TMemberPtrRefs>>)
		{
			constexpr size_t N = std::extent_v<std::remove_reference_t<TMemberPtrRefs>>;
			for (size_t i = 0; i < N; ++i)
			{
				common::Read(rStream, memberPtrRefs[i], uiCount);
			}
		}
		else
		{
			common::Read(rStream, memberPtrRefs, uiCount);
		}
	}(), ...);
}

// Allocates collection storage and reads data from stream. Used internally by CollectionRead().
template <typename TStruct, typename... TMemberPtrRefs>
void AllocateAndRead(TStruct& rStruct, std::istream& rStream, TMemberPtrRefs&... memberPtrRefs)
{
	if (rStruct.uiCapacity > 0)
	{
		AllocateAndAssign(rStruct, rStruct.uiCapacity, memberPtrRefs...);
	}
	else
	{
		ResetDataToNull(rStruct, memberPtrRefs...);
	}

	MultiRead(rStream, rStruct.uiCount, memberPtrRefs...);
}

// ============================================================================
// COLLECTION BASE CLASS
// ============================================================================

// Collection configuration flags
enum class CollectionFlags : uint32_t
{
	kNone = 0,
	kIdToIndex = 1 << 0,   // Enable ID-to-index mapping
};

enum class RenderableFlags : uint32_t
{
	kNone = 0,
	kShadow = 1 << 0,   // Create shadow pipeline in addition to main pipeline
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
		bEqual &= common::BreakOnNotEqual(uiCount, rOther.uiCount);
		bEqual &= common::BreakOnNotEqual(uiCapacity, rOther.uiCapacity);
		return bEqual;
	}

	inline void Write(std::ostream& rStream) const
	{
		common::Write(rStream, uiCount);
		common::Write(rStream, uiCapacity);
		if constexpr (FLAGS & CollectionFlags::kIdToIndex)
		{
			static_cast<const OptionaldToIndex<T, FLAGS>&>(*this).Write(rStream);
		}
	}

	inline void Read(std::istream& rStream)
	{
		common::Read(rStream, uiCount);
		common::Read(rStream, uiCapacity);
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
		checksum ^= common::Crc(uiCount);
		checksum ^= common::Crc(uiCapacity);
		return checksum;
	}

	uint64_t uiCount = 0;
	uint64_t uiCapacity = 0;
	common::AlignedUniquePtr<std::byte> pData;
};

// ============================================================================
// RENDERABLE MIXIN
// ============================================================================
// Provides dynamic buffer management for collections that render to GPU via glTF pipelines.
// Template parameters provide explicit configuration instead of requiring derived class constants.
// FLAGS controls optional features: kShadow enables shadow pipeline creation.

template <typename T, VkDeviceSize LAYOUT_SIZE, common::crc_t GLTF_CRC, common::crc_t GLTF_MODEL_CRC, common::Flags<RenderableFlags> FLAGS = RenderableFlags::kShadow>
struct Renderable
{
	static constexpr common::crc_t kCrc = common::Crc(T::kpcName);
	static constexpr VkDeviceSize kLayoutSize = LAYOUT_SIZE;
	static constexpr common::crc_t kGltfCrc = GLTF_CRC;
	static constexpr common::crc_t kGltfModelCrc = GLTF_MODEL_CRC;
	static constexpr common::Flags<RenderableFlags> kFlags = FLAGS;

	// Creates dynamic storage buffer with minimal initial size.
	// Called from derived class AllocateGraphicsResources().
	// Returns pointer to buffer array for pipeline creation.
	static inline Buffer* AllocateDynamicBuffer()
	{
		return gpBufferManager->CreateDynamicBuffer(common::Crc(T::kpcName), T::kpcName, kLayoutSize);
	}

	// Creates dynamic storage buffer and glTF pipelines.
	// Shadow pipeline created only if kShadow flag is set.
	// Called from derived class AllocateGraphicsResources().
	static inline void AllocateGltfPipelines()
	{
		Buffer* pStorageBuffers = AllocateDynamicBuffer();
		gpPipelineManager->CreateDynamicGltfPipeline(kCrc, T::kpcName, kGltfCrc, kGltfModelCrc, pStorageBuffers);
		if constexpr (kFlags & RenderableFlags::kShadow)
		{
			gpPipelineManager->CreateDynamicGltfPipelineShadow(kCrc, T::kpcName, kGltfCrc, kGltfModelCrc, pStorageBuffers);
		}
	}

	// Checks if buffer resize needed based on collection capacity.
	// Returns true if resize occurred (caller should update descriptors and re-record).
	// Called from derived class Render() method.
	static inline bool CheckAndResizeBuffer(const T& rCollection, int64_t iCommandBuffer)
	{
		VkDeviceSize requiredSize = kLayoutSize * rCollection.uiCapacity;
		Buffer& rBuffer = gpBufferManager->mDynamicStorageBuffers.at(kCrc).at(iCommandBuffer);
		if (rBuffer.mInfo.dataVkDeviceSize < requiredSize)
		{
			gpBufferManager->ResizeDynamicBuffer(kCrc, T::kpcName, requiredSize, iCommandBuffer);
			return true;
		}
		return false;
	}

	// Checks if buffer resize needed and handles all post-resize updates.
	// Updates descriptor sets, re-records secondary command buffers, sets rerecord flag.
	// Shadow pipeline updated only if kShadow flag is set.
	// Called from derived class Render() method.
	static inline void ResizeAndUpdatePipelines(const T& rCollection, int64_t iCommandBuffer)
	{
		if (!CheckAndResizeBuffer(rCollection, iCommandBuffer))
		{
			return;
		}

		int64_t iFramebuffer = iCommandBuffer;
		Buffer& rBuffer = gpBufferManager->mDynamicStorageBuffers.at(kCrc).at(iCommandBuffer);

		gpPipelineManager->mDynamicGltfPipelineMap.at(kCrc)->UpdateStorageBufferDescriptors(iFramebuffer, 2, &rBuffer);
		gpPipelineManager->mDynamicGltfPipelineMap.at(kCrc)->RerecordSecondary(iFramebuffer, gpSwapchainManager->mVkRenderPass, gpSwapchainManager->mFramebuffers.at(iFramebuffer).presentVkFramebuffer);

		if constexpr (kFlags & RenderableFlags::kShadow)
		{
			gpPipelineManager->mDynamicGltfPipelineShadowMap.at(kCrc)->UpdateStorageBufferDescriptors(iFramebuffer, 2, &rBuffer);
			gpPipelineManager->mDynamicGltfPipelineShadowMap.at(kCrc)->RerecordSecondary(iFramebuffer, gpTextureManager->mObjectShadowsTexture.mVkRenderPass, gpTextureManager->mObjectShadowsTexture.mVkFramebuffer, {0.0f, 2.0f, 0.0f, 0.0f});
		}

		gpCommandBufferManager->mPerFramebufferCommandBuffers.at(iFramebuffer).mFlags |= CommandBufferFlags::kNeedsRerecord;
	}
};

// ============================================================================
// COLLECTION-LEVEL PATTERN HELPERS
// ============================================================================
// High-level API functions for complete collection operations.

// Computes complete CRC of collection (metadata + all member arrays) for deterministic replay validation.
template <typename TStruct, typename... TMemberPtrRefs>
inline common::crc_t CollectionCrc(const TStruct& rCurrent, TMemberPtrRefs&... memberPtrRefs)
{
	common::crc_t checksum = rCurrent.Crc();
	checksum ^= engine::MultiCrc(rCurrent.uiCount, memberPtrRefs...);
	return checksum;
}

// Writes complete collection to stream (metadata + all member arrays) for save file serialization.
template <typename TStruct, typename... TMemberPtrRefs>
inline std::ostream& CollectionWrite(std::ostream& rStream, const TStruct& rCurrent, TMemberPtrRefs&... memberPtrRefs)
{
	rCurrent.Write(rStream);
	engine::MultiWrite(rStream, rCurrent.uiCount, memberPtrRefs...);
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
