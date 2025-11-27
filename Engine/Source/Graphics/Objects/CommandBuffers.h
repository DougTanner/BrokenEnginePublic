#pragma once

namespace engine
{

enum class CommandBufferFlags : uint8_t
{
	kRecorded = 0x01,
	kExecuted = 0x02,
};
using CommandBufferFlags_t = common::Flags<CommandBufferFlags>;

class CommandBuffers
{
public:

	CommandBuffers() = delete;
	CommandBuffers(int64_t iFramebuffer);
	~CommandBuffers();

	int64_t miFramebuffer = 0;
	CommandBufferFlags_t mFlags;
	VkCommandPool mVkCommandPool = VK_NULL_HANDLE;
	VkCommandBuffer mGlobalVkCommandBuffer = VK_NULL_HANDLE;
	VkCommandBuffer mImageVkCommandBuffer = VK_NULL_HANDLE;

	VkSemaphore mGlobalFinishedVkSemaphore = VK_NULL_HANDLE;
	VkSemaphore mImageFinishedVkSemaphore = VK_NULL_HANDLE;

	VkFence mVkFence = VK_NULL_HANDLE;
};

} // namespace engine
