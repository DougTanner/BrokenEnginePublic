#pragma once

#include "Graphics/Objects/Texture.h"

namespace engine
{

struct UiState;

class DeviceManager;

struct Framebuffer
{
	VkImage presentVkImage = VK_NULL_HANDLE;
	VkImageView presentVkImageView = VK_NULL_HANDLE;
	VkFramebuffer presentVkFramebuffer = VK_NULL_HANDLE;
};

class SwapchainManager
{
public:

	SwapchainManager();
	~SwapchainManager();

	void AcquireNextImage();
	void Present();
	void ReduceInputLag();

	float mfAspectRatio = 1.0f;

	Texture mDepthTexture;
	Texture mMultisamplingTexture;

	std::vector<Framebuffer> mFramebuffers;
	int64_t miFramebufferIndex = 0;

	std::vector<VkSemaphore> mImageAvailableSemaphores;
	VkSemaphore mImageAvailableVkSemaphore = VK_NULL_HANDLE;
	int64_t miImageAvailableIndex = 0;
	std::vector<VkFence> mImageAvailableFences;
	int64_t miFenceAvailableIndex = 0;

	VkRenderPass mVkRenderPass = VK_NULL_HANDLE;

#if defined(ENABLE_RENDER_THREAD)
	std::future<void> mPresent;
#endif

private:

	inline VkSemaphore GetNextImageAvailableSemaphore()
	{
		VkSemaphore vkSemaphore = mImageAvailableSemaphores.at(miImageAvailableIndex);

		++miImageAvailableIndex;
		if (miImageAvailableIndex == static_cast<int64_t>(mImageAvailableSemaphores.size()))
		{
			miImageAvailableIndex = 0;
		}

		return vkSemaphore;
	}

	inline VkFence GetNextImageAvailableFence()
	{
		VkFence vkFence = mImageAvailableFences.at(miFenceAvailableIndex);

		++miFenceAvailableIndex;
		if (miFenceAvailableIndex == static_cast<int64_t>(mImageAvailableFences.size()))
		{
			miFenceAvailableIndex = 0;
		}

		return vkFence;
	}

	VkSwapchainKHR mVkSwapchainKHR = VK_NULL_HANDLE;
	VkFence mCurrentImageAvailableVkFence = VK_NULL_HANDLE;
};

inline SwapchainManager* gpSwapchainManager = nullptr;

} // namespace engine
