#pragma once

#if defined(BT_CLIENT)

namespace engine
{

struct Framebuffer
{
	Framebuffer() = default;
	Framebuffer(const Framebuffer&) = delete;
	Framebuffer& operator=(const Framebuffer&) = delete;
	// Movable (not copyable): std::vector<Framebuffer>::resize instantiates the relocation path at compile
	// time. Handles are freed by ~SwapchainManager's loop, not this struct, so a defaulted move (which only
	// copies the handle values) is safe — the moved-from element's destruction frees nothing.
	Framebuffer(Framebuffer&&) = default;
	Framebuffer& operator=(Framebuffer&&) = default;

	VkImage presentVkImage = VK_NULL_HANDLE;
	VkImageView presentVkImageView = VK_NULL_HANDLE;
	VkFramebuffer presentVkFramebuffer = VK_NULL_HANDLE;
};

class SwapchainManager
{
public:

	SwapchainManager(VkSwapchainKHR oldSwapchain = VK_NULL_HANDLE);
	~SwapchainManager();

	void AcquireNextImage();
	void Present(int64_t iFramebufferIndex);

	// Hands the live swapchain handle to the caller (returns it, nulls the member) so the old swapchain
	// survives across recreation — single named home for the ownership transfer in Graphics::Destroy.
	VkSwapchainKHR ReleaseHandleForRecreation();

	float mfAspectRatio = 1.0f;

	Texture mDepthTexture;
	Texture mMultisamplingTexture;

	// HDR intermediate: the scene renders into this F16 target (mHdrVkRenderPass / mHdrVkFramebuffer);
	// the fullscreen HDR-resolve pass samples it and writes the swapchain via mVkRenderPass. Single
	// framebuffer (no per-swapchain-image attachment among the HDR pass's color/MSAA/depth).
	Texture mHdrTexture;

	std::vector<Framebuffer> mFramebuffers;
	int64_t miFramebufferIndex = 0;

	VkSemaphore mImageAvailableVkSemaphore = VK_NULL_HANDLE;

	VkRenderPass mVkRenderPass = VK_NULL_HANDLE;
	VkRenderPass mHdrVkRenderPass = VK_NULL_HANDLE;
	VkFramebuffer mHdrVkFramebuffer = VK_NULL_HANDLE;
	VkSwapchainKHR mVkSwapchainKHR = VK_NULL_HANDLE;

	common::PersistentWorker mPresent;

private:

	// Construction phases (called once from the ctor, in order).
	void CreateRenderPass();
	void CreateSwapchain(VkSwapchainKHR oldSwapchain);
	void CreateFramebuffers();
	void CreateSyncObjects();

	// Round-robin acquire sync objects, advanced by the GetNext* accessors below.
	std::vector<VkSemaphore> mImageAvailableSemaphores;
	int64_t miImageAvailableIndex = 0;
	std::vector<VkFence> mImageAvailableFences;
	int64_t miFenceAvailableIndex = 0;

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

	void PresentToQueue(int64_t iFramebufferIndex);

	VkFence mCurrentImageAvailableVkFence = VK_NULL_HANDLE;
};

inline SwapchainManager* gpSwapchainManager = nullptr;

} // namespace engine

#endif // defined(BT_CLIENT)
