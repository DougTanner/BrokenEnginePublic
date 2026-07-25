#pragma once

#if defined(BT_CLIENT)

namespace engine
{

enum class BufferFlags : uint64_t
{
	kIndexVertex                 = 0x01,
	kUniform                     = 0x02,
	kStorage                     = 0x04,

	kDeviceLocal                 = 0x08,
	kHostVisible                 = 0x10,

	kCopyToDeviceLocalEveryFrame = 0x20,
};
using BufferFlags_t = common::Flags<BufferFlags>;

enum class BufferBarrier
{
	kComputeRead,
	kComputeReadWrite,
	kUniformBufferRead,
	kStorageBufferRead,
	kShaderIndirectRead,

	kNone,
};

struct BarrierInfo
{
	BufferBarrier eSource = BufferBarrier::kNone;
	BufferBarrier eDestination = BufferBarrier::kNone;
	VkBuffer vkBuffer = VK_NULL_HANDLE;
};

struct BufferInfo
{
	std::string_view name;
	BufferFlags_t flags;

	int64_t iCount = 0;
	VkIndexType vkIndexType = VK_INDEX_TYPE_NONE_KHR;
	int64_t iVertexStride = 0;
	VkDeviceSize dataVkDeviceSize = 0;
	VkDeviceSize iElementSize = 0; // Size of each element for type-safe access
};

struct DeviceLocalBufferUpload
{
	const void* pData = nullptr;
	VkDeviceSize vkDestinationOffset = 0;
	VkDeviceSize vkSize = 0;
};

class Buffer
{
public:

	static void CreateBuffer(std::string_view name, VkDeviceSize vkDeviceSize, VkBufferUsageFlags vkBufferUsageFlags, VkMemoryPropertyFlags vkMemoryPropertyFlags, VkBuffer& rVkBuffer, VmaAllocation& rVmaAllocation, VmaAllocationInfo* pVmaAllocationInfo = nullptr);
	static void UploadToDeviceLocal(VkBuffer vkDeviceLocalBuffer, std::span<const DeviceLocalBufferUpload> uploads);
	static void RecordBarriers(VkCommandBuffer vkCommandBuffer, const BarrierInfo* pBarriers, int64_t iBarrierCount);

	Buffer() = default;
	Buffer(const Buffer&) = delete;
	Buffer& operator=(const Buffer&) = delete;
	Buffer(Buffer&& rOther) noexcept;
	Buffer& operator=(Buffer&& rOther) noexcept;
	Buffer(const BufferInfo& rInfo, const std::function<void(void*)>& rDataFunction = nullptr);
	~Buffer();

	void Create(const BufferInfo& rInfo, const std::function<void(void*)>& rDataFunction = nullptr);
	void Destroy() noexcept;

	VkBuffer GetBuffer();

	void RecordBindVertexBuffer(VkCommandBuffer vkCommandBuffer);
	void RecordCopy(VkCommandBuffer vkCommandBuffer, VkPipelineStageFlags stageFlags = VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);

	BufferInfo mInfo {};

	VkBuffer mHostVisibleVkBuffer = VK_NULL_HANDLE;
	VmaAllocation mHostVisibleVmaAllocation = VK_NULL_HANDLE;
	char* mpMappedMemory = nullptr;

	VkBuffer mDeviceLocalVkBuffer = VK_NULL_HANDLE;
	VmaAllocation mDeviceLocalVmaAllocation = VK_NULL_HANDLE;
};

struct StagingBuffer
{
	StagingBuffer(std::string_view name, VkDeviceSize vkDeviceSize, VkBufferUsageFlags vkBufferUsageFlags);
	StagingBuffer(const StagingBuffer&) = delete;
	StagingBuffer& operator=(const StagingBuffer&) = delete;
	~StagingBuffer();

	VkBuffer vkBuffer = VK_NULL_HANDLE;
	VmaAllocation vmaAllocation = VK_NULL_HANDLE;
	VmaAllocationInfo vmaAllocationInfo {};
};

} // namespace engine

#endif // defined(BT_CLIENT)
