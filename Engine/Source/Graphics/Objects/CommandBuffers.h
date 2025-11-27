#pragma once

namespace engine
{

class CommandBuffers
{
public:

	CommandBuffers() = delete;
	CommandBuffers(int64_t iFramebuffer);
	~CommandBuffers();

	bool mbRecorded = false;
	bool mbExecuted = false;
	bool mbNeedsRerecord = false;
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
