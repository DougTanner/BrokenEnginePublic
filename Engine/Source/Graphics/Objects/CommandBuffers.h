#pragma once

namespace engine
{

enum class CommandBufferFlags : uint8_t
{
	kRecorded      = 0x01,
	kExecuted      = 0x02,
	kNeedsRerecord = 0x04,
};
using CommandBufferFlags_t = common::Flags<CommandBufferFlags>;

class CommandBuffers
{
public:

	CommandBuffers() = delete;
	CommandBuffers(int64_t iFramebuffer);
	~CommandBuffers();

	void RerecordImageIfNeeded();

	int64_t miFramebuffer = 0;
	CommandBufferFlags_t mFlags;
	VkCommandPool mCommandPool = VK_NULL_HANDLE;
	VkCommandBuffer mGlobalCommandBuffer = VK_NULL_HANDLE;
	VkCommandBuffer mImageCommandBuffer = VK_NULL_HANDLE;
	VkCommandBuffer mSceneSecondaryBuffer = VK_NULL_HANDLE;
	VkCommandBuffer mUiSecondaryBuffer = VK_NULL_HANDLE;

	VkSemaphore mGlobalFinishedVkSemaphore = VK_NULL_HANDLE;
	VkSemaphore mImageFinishedVkSemaphore = VK_NULL_HANDLE;

	VkFence mVkFence = VK_NULL_HANDLE;
};

} // namespace engine
