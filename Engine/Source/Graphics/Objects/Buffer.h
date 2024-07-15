#pragma once

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
	kComputeWrite,
	kShaderUniformRead,
	kShaderIndirectRead,
};

struct BufferInfo
{
	std::string_view pcName;
	BufferFlags_t flags;

	int64_t iCount = 0;
	VkIndexType vkIndexType = VK_INDEX_TYPE_NONE_KHR;
	int64_t iVertexStride = 0;
	VkDeviceSize dataVkDeviceSize = 0;
};

class Buffer
{
public:

	static void CreateBuffer(std::string_view pcName, VkDeviceSize vkDeviceSize, VkBufferUsageFlags vkBufferUsageFlags, VkMemoryPropertyFlags vkMemoryPropertyFlags, VkBuffer& rVkBuffer, VkDeviceMemory& rVkDeviceMemory);
	static void RecordBarrier(VkCommandBuffer vkCommandBuffer, BufferBarrier eSource, BufferBarrier eDestination, VkBuffer vkBuffer);

	Buffer() = default;
	Buffer(const Buffer&) = delete;
	Buffer(Buffer&&) noexcept = default;
	Buffer(const BufferInfo& rInfo, std::function<void(void*)> dataFunction = nullptr);
	~Buffer();

	void Create(const BufferInfo& rInfo, std::function<void(void*)> dataFunction = nullptr);
	void Destroy() noexcept;

	VkBuffer GetBuffer();

	void RecordBindVertexBuffer(VkCommandBuffer vkCommandBuffer);
	void RecordCopy(VkCommandBuffer vkCommandBuffer);

	BufferInfo mInfo {};

	VkBuffer mHostVisibleVkBuffer = VK_NULL_HANDLE;
	VkDeviceMemory mHostVisibleVkDeviceMemory = VK_NULL_HANDLE;
	char* mpMappedMemory = nullptr;

	VkBuffer mDeviceLocalVkBuffer = VK_NULL_HANDLE;
	VkDeviceMemory mDeviceLocalVkDeviceMemory = VK_NULL_HANDLE;
};

} // namespace engine
