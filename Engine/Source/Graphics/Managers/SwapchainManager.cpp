#include "SwapchainManager.h"

#include "Graphics/Graphics.h"
#include "Profile/ProfileManager.h"
#include "Ui/Wrapper.h"

namespace engine
{

SwapchainManager::SwapchainManager()
{
	gpSwapchainManager = this;

	SCOPED_BOOT_TIMER(kBootTimerSwapchainManager);

	// Based on https://github.com/Overv/VulkanTutorial
	// The render pass attachment description will specify how many color and depth buffers there will be, how many samples to use for each of them and how their contents should be handled throughout the rendering operations
	VkAttachmentDescription pVkAttachmentDescriptions[]
	{
		// Present framebuffer
		VkAttachmentDescription
		{
			.flags = 0,
			.format = gpInstanceManager->mFramebufferVkFormat,
			.samples = VK_SAMPLE_COUNT_1_BIT,
		#if defined(ENABLE_FRAMEBUFFER_CLEAR_COLOR)
			.loadOp = gMultisampling.Get<bool>() ? VK_ATTACHMENT_LOAD_OP_DONT_CARE : VK_ATTACHMENT_LOAD_OP_CLEAR,
		#else
			.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
		#endif
			.storeOp = VK_ATTACHMENT_STORE_OP_STORE,
			.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
			.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
			.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
			.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
		},
		// Depth
		VkAttachmentDescription
		{
			.flags = 0,
			.format = gpInstanceManager->mDepthVkFormat,
			.samples = gMultisampling.Get<bool>() ? gSampleCount.Get<VkSampleCountFlagBits>() : VK_SAMPLE_COUNT_1_BIT,
			.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
			.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
			.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
			.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
			.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
			.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
		},
		// Multisample framebuffer
		VkAttachmentDescription
		{
			.flags = 0,
			.format = gpInstanceManager->mFramebufferVkFormat,
			.samples = gSampleCount.Get<VkSampleCountFlagBits>(),
		#if defined(ENABLE_FRAMEBUFFER_CLEAR_COLOR)
			.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
		#else
			.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
		#endif
			.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
			.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
			.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
			.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
			.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
		}
	};

	VkAttachmentReference presentVkAttachmentReference
	{
		.attachment = 0,
		.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
	};
	VkAttachmentReference depthVkAttachmentReference
	{
		.attachment = 1,
		.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
	};
	VkAttachmentReference multisamplingVkAttachmentReference
	{
		.attachment = 2,
		.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
	};
	VkSubpassDescription vkSubpassDescription
	{
		vkSubpassDescription.flags = 0,
		vkSubpassDescription.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
		vkSubpassDescription.inputAttachmentCount = 0,
		vkSubpassDescription.pInputAttachments = nullptr,
		vkSubpassDescription.colorAttachmentCount = 1,
		vkSubpassDescription.pColorAttachments = gMultisampling.Get<bool>() ? &multisamplingVkAttachmentReference : &presentVkAttachmentReference,
		vkSubpassDescription.pResolveAttachments = gMultisampling.Get<bool>() ? &presentVkAttachmentReference : nullptr,
		vkSubpassDescription.pDepthStencilAttachment = &depthVkAttachmentReference,
		vkSubpassDescription.preserveAttachmentCount = 0,
		vkSubpassDescription.pPreserveAttachments = nullptr,
	};

	// There are two built-in dependencies that take care of the transition at the start of the render pass and at the end of the render pass, but the former does not occur at the right time
	// It assumes that the transition occurs at the start of the pipeline, but we haven't acquired the image yet at that point!
	// There are two ways to deal with this problem. We could change the waitStages for the imageAvailableSemaphore to VK_PIPELINE_STAGE_TOP_OF_PIPELINE_BIT to ensure that the render passes don't begin until the image is available
	// or we can make the render pass wait for the VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT stage
	// The vkPipelineStageFlags and pWaitSemaphores specify which semaphores to wait on before execution begins and in which stages of the pipeline to wait
	// We want to wait with writing colors to the image until it's available, so we're specifying the stage of the graphics pipeline that writes to the color attachment
	// That means that theoretically the implementation can already start executing our vertex shader and such while the image is not available yet
	//
	// Only need a dependency coming in to ensure that the first layout transition happens at the right time.
	// Second external dependency is implied by having a different finalLayout and subpass layout.
	VkSubpassDependency vkSubpassDependency
	{
		vkSubpassDependency.srcSubpass = VK_SUBPASS_EXTERNAL,
		vkSubpassDependency.dstSubpass = 0,
		vkSubpassDependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
		vkSubpassDependency.dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
		vkSubpassDependency.srcAccessMask = 0,
		vkSubpassDependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
		vkSubpassDependency.dependencyFlags = 0,
	};

	// Create the render pass from the attachments description and subpasses
	VkRenderPassCreateInfo vkRenderPassCreateInfo
	{
		.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
		.pNext = nullptr,
		.flags = 0,
		.attachmentCount = gMultisampling.Get<bool>() ? 3u : 2u,
		.pAttachments = pVkAttachmentDescriptions,
		.subpassCount = 1,
		.pSubpasses = &vkSubpassDescription,
		.dependencyCount = 1,
		.pDependencies = &vkSubpassDependency,
	};
	CHECK_VK(vkCreateRenderPass(gpDeviceManager->mVkDevice, &vkRenderPassCreateInfo, nullptr, &mVkRenderPass));
	VK_NAME(VK_OBJECT_TYPE_RENDER_PASS, mVkRenderPass, "SwapchainManager");

	// The swapchain is essentially a queue of images that are waiting to be presented to the screen
	// Our application will acquire such an image to draw to it, and then return it to the queue
	// How exactly the queue works and the conditions for presenting an image from the queue depend on how the swapchain is set up
	// The general purpose of the swapchain is to synchronize the presentation of images with the refresh rate of the screen.
	VkSurfaceCapabilitiesKHR vkSurfaceCapabilitiesKHR {};
	CHECK_VK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(gpInstanceManager->mVkPhysicalDevice, gpInstanceManager->mVkSurfaceKHR, &vkSurfaceCapabilitiesKHR));
	ASSERT((vkSurfaceCapabilitiesKHR.supportedCompositeAlpha & (VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR | VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR)) != 0);

	uint32_t uiPresentModeCount = 0;
	CHECK_VK(vkGetPhysicalDeviceSurfacePresentModesKHR(gpInstanceManager->mVkPhysicalDevice, gpInstanceManager->mVkSurfaceKHR, &uiPresentModeCount, nullptr));
	ASSERT(uiPresentModeCount != 0);
	std::vector<VkPresentModeKHR> physicalDevicePresentModes(uiPresentModeCount);
	CHECK_VK(vkGetPhysicalDeviceSurfacePresentModesKHR(gpInstanceManager->mVkPhysicalDevice, gpInstanceManager->mVkSurfaceKHR, &uiPresentModeCount, physicalDevicePresentModes.data()));

	LOG("Present modes ({}):", physicalDevicePresentModes.size());
	// FIFO is guaranteed to be available
	VkPresentModeKHR eVkPresentModeKHR = VK_PRESENT_MODE_FIFO_KHR;
	for (const VkPresentModeKHR& reVkPresentModeKHR : physicalDevicePresentModes)
	{
		LOG("  {}", gEnumToString.mVkPresentModeKHRMap.at(reVkPresentModeKHR));

		if (reVkPresentModeKHR == VK_PRESENT_MODE_MAILBOX_KHR && gPresentMode.Get<VkPresentModeKHR>() == VK_PRESENT_MODE_MAILBOX_KHR)
		{
			eVkPresentModeKHR = VK_PRESENT_MODE_MAILBOX_KHR;
		}
		else if (reVkPresentModeKHR == VK_PRESENT_MODE_IMMEDIATE_KHR && gPresentMode.Get<VkPresentModeKHR>() == VK_PRESENT_MODE_IMMEDIATE_KHR)
		{
			eVkPresentModeKHR = VK_PRESENT_MODE_IMMEDIATE_KHR;
		}
	}
	LOG("Present mode selected: {}", gEnumToString.mVkPresentModeKHRMap.at(eVkPresentModeKHR));
	gPresentMode.Reset(eVkPresentModeKHR);

	// The swap extent is the resolution of the swap chain images and it's almost always exactly equal to the resolution of the window that we're drawing to
	if (vkSurfaceCapabilitiesKHR.currentExtent.width == 0xFFFFFFFF)
	{
		// If the surface size is undefined, the size is set to the size of the images requested
		gpGraphics->mFramebufferExtent2D.width = gWantedFramebufferExtent2D.width;
		if (gpGraphics->mFramebufferExtent2D.width < vkSurfaceCapabilitiesKHR.minImageExtent.width)
		{
			gpGraphics->mFramebufferExtent2D.width = vkSurfaceCapabilitiesKHR.minImageExtent.width;
		}
		else if (gpGraphics->mFramebufferExtent2D.width > vkSurfaceCapabilitiesKHR.maxImageExtent.width)
		{
			gpGraphics->mFramebufferExtent2D.width = vkSurfaceCapabilitiesKHR.maxImageExtent.width;
		}

		gpGraphics->mFramebufferExtent2D.height = gWantedFramebufferExtent2D.height;
		if (gpGraphics->mFramebufferExtent2D.height < vkSurfaceCapabilitiesKHR.minImageExtent.height)
		{
			gpGraphics->mFramebufferExtent2D.height = vkSurfaceCapabilitiesKHR.minImageExtent.height;
		}
		else if (gpGraphics->mFramebufferExtent2D.height > vkSurfaceCapabilitiesKHR.maxImageExtent.height)
		{
			gpGraphics->mFramebufferExtent2D.height = vkSurfaceCapabilitiesKHR.maxImageExtent.height;
		}
	}
	else
	{
		// If the surface size is defined, the swapchain size must match
		gpGraphics->mFramebufferExtent2D = vkSurfaceCapabilitiesKHR.currentExtent;
		gWantedFramebufferExtent2D = vkSurfaceCapabilitiesKHR.currentExtent;
	}

	mfAspectRatio = static_cast<float>(gpGraphics->mFramebufferExtent2D.width) / static_cast<float>(gpGraphics->mFramebufferExtent2D.height);

	// VK_PRESENT_MODE_FIFO_KHR and IMMEDIATE: We want 2 images
	// VK_PRESENT_MODE_MAILBOX_KHR: Ask for one extra image (triple buffering)
	uint32_t uiMinImageCount = std::max(gPresentMode.Get<VkPresentModeKHR>() == VK_PRESENT_MODE_MAILBOX_KHR ? 3u : 2u, vkSurfaceCapabilitiesKHR.minImageCount);
	if (vkSurfaceCapabilitiesKHR.maxImageCount > 0)
	{
		uiMinImageCount = std::min(uiMinImageCount, vkSurfaceCapabilitiesKHR.maxImageCount);
	}
	LOG("uiMinImageCount: {}", uiMinImageCount);

	uint32_t pQueueFamilyIndices[]
	{
		static_cast<uint32_t>(gpInstanceManager->miGraphicsQueueFamilyIndex),
		static_cast<uint32_t>(gpInstanceManager->miPresentQueueFamilyIndex),
	};

	// If the graphics and present queues are from different queue families, we either have to explicitly transfer ownership of images between the
	// queues, or we have to create the swapchain with imageSharingMode as VK_SHARING_MODE_CONCURRENT
	bool bDifferentQueueFamilies = gpInstanceManager->miGraphicsQueueFamilyIndex != gpInstanceManager->miPresentQueueFamilyIndex;

	VkSwapchainCreateInfoKHR vkSwapchainCreateInfoKHR
	{
		.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
		.pNext = nullptr,
		.flags = 0,
		.surface = gpInstanceManager->mVkSurfaceKHR,
		.minImageCount = static_cast<uint32_t>(uiMinImageCount),
		.imageFormat = gpInstanceManager->mFramebufferVkFormat,
		.imageColorSpace = VK_COLORSPACE_SRGB_NONLINEAR_KHR,
		.imageExtent = gpGraphics->mFramebufferExtent2D,
		.imageArrayLayers = 1,
		.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
		.imageSharingMode = bDifferentQueueFamilies ? VK_SHARING_MODE_CONCURRENT : VK_SHARING_MODE_EXCLUSIVE,
		.queueFamilyIndexCount = bDifferentQueueFamilies ? 2u : 0u,
		.pQueueFamilyIndices = bDifferentQueueFamilies ? &pQueueFamilyIndices[0] : nullptr,
		.preTransform = (vkSurfaceCapabilitiesKHR.supportedTransforms & VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR) != 0 ? VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR : vkSurfaceCapabilitiesKHR.currentTransform,
		.compositeAlpha = (vkSurfaceCapabilitiesKHR.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR) != 0 ? VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR : VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR,
		.presentMode = gPresentMode.Get<VkPresentModeKHR>(),
		.clipped = VK_TRUE,
		.oldSwapchain = VK_NULL_HANDLE,
	};
	CHECK_VK(vkCreateSwapchainKHR(gpDeviceManager->mVkDevice, &vkSwapchainCreateInfoKHR, nullptr, &mVkSwapchainKHR));

	// Get the swapchain images
	uint32_t uiImageCount = 0;
	CHECK_VK(vkGetSwapchainImagesKHR(gpDeviceManager->mVkDevice, mVkSwapchainKHR, &uiImageCount, nullptr));
	ASSERT(uiImageCount != 0);
	std::vector<VkImage> swapchainImages(uiImageCount);
	CHECK_VK(vkGetSwapchainImagesKHR(gpDeviceManager->mVkDevice, mVkSwapchainKHR, &uiImageCount, swapchainImages.data()));

	// Depth
	VkImageAspectFlags vkImageAspectFlags = VK_IMAGE_ASPECT_DEPTH_BIT;
	if (gpInstanceManager->mDepthVkFormat == VK_FORMAT_D16_UNORM_S8_UINT || gpInstanceManager->mDepthVkFormat == VK_FORMAT_D24_UNORM_S8_UINT || gpInstanceManager->mDepthVkFormat == VK_FORMAT_D32_SFLOAT_S8_UINT)
	{
		vkImageAspectFlags |= VK_IMAGE_ASPECT_STENCIL_BIT;
	}
	mDepthTexture.Create(TextureInfo
	{
		.textureFlags = {},
		.pcName = "Depth",
		.flags = 0,
		.format = gpInstanceManager->mDepthVkFormat,
		.extent = VkExtent3D {.width = gpGraphics->mFramebufferExtent2D.width, .height = gpGraphics->mFramebufferExtent2D.height, .depth = 1},
		.mipLevels = 1,
		.arrayLayers = 1,
		.samples = gMultisampling.Get<bool>() ? gSampleCount.Get<VkSampleCountFlagBits>() : VK_SAMPLE_COUNT_1_BIT,
		.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
		.viewType = VK_IMAGE_VIEW_TYPE_2D,
		.aspectMask = vkImageAspectFlags,
		.eTextureLayout = TextureLayout::kUndefined,
	});

	// Multisampling
	if (gMultisampling.Get<bool>())
	{
		mMultisamplingTexture.Create(TextureInfo
		{
			.textureFlags = {},
			.pcName = "Multisampling",
			.flags = 0,
			.format = gpInstanceManager->mFramebufferVkFormat,
			.extent = VkExtent3D {.width = gpGraphics->mFramebufferExtent2D.width, .height = gpGraphics->mFramebufferExtent2D.height, .depth = 1},
			.mipLevels = 1,
			.arrayLayers = 1,
			.samples = gSampleCount.Get<VkSampleCountFlagBits>(),
			.usage = VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
			.viewType = VK_IMAGE_VIEW_TYPE_2D,
			.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
			.eTextureLayout = TextureLayout::kUndefined,
		});
	}

	mFramebuffers.resize(uiImageCount);
	miFramebufferIndex = 0;
	for (int64_t i = 0; i < uiImageCount; ++i)
	{
		Framebuffer& rFrameBuffer = mFramebuffers.at(i);
		rFrameBuffer.presentVkImage = swapchainImages.at(i);

		// An image view is 'view' into an image, it describes how to access the image and which part of the image to access
		VkImageViewCreateInfo vkImageViewCreateInfo
		{
			.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
			.pNext = nullptr,
			.flags = 0,
			.image = rFrameBuffer.presentVkImage,
			.viewType = VK_IMAGE_VIEW_TYPE_2D,
			.format = gpInstanceManager->mFramebufferVkFormat,
			.components = VkComponentMapping
			{
				.r = VK_COMPONENT_SWIZZLE_R,
				.g = VK_COMPONENT_SWIZZLE_G,
				.b = VK_COMPONENT_SWIZZLE_B,
				.a = VK_COMPONENT_SWIZZLE_A,
			},
			.subresourceRange = VkImageSubresourceRange
			{
				.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
				.baseMipLevel = 0,
				.levelCount = 1,
				.baseArrayLayer = 0,
				.layerCount = 1,
			}
		};
		CHECK_VK(vkCreateImageView(gpDeviceManager->mVkDevice, &vkImageViewCreateInfo, nullptr, &rFrameBuffer.presentVkImageView));
		VK_NAME(VK_OBJECT_TYPE_IMAGE, rFrameBuffer.presentVkImage, "PresentVkImage");
		VK_NAME(VK_OBJECT_TYPE_IMAGE_VIEW, rFrameBuffer.presentVkImageView, "PresentVkImageView");

		// The attachments specified during render pass creation are bound by wrapping them into a VkFramebuffer object
		// A framebuffer object references all of the VkImageView objects that represent the attachments
		// However, the image that we have to use as attachment depends on which image the swap chain returns when we retrieve one for presentation
		// That means that we have to create a framebuffer for all of the images in the swap chain and use the one that corresponds to the retrieved image at drawing time
		VkImageView pVkImageViews[] {rFrameBuffer.presentVkImageView, mDepthTexture.mVkImageView, mMultisamplingTexture.mVkImageView};
		VkFramebufferCreateInfo vkFramebufferCreateInfo
		{
			.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
			.pNext = nullptr,
			.flags = 0,
			.renderPass = mVkRenderPass,
			.attachmentCount = gMultisampling.Get<bool>() ? 3u : 2u,
			.pAttachments = pVkImageViews,
			.width = gpGraphics->mFramebufferExtent2D.width,
			.height = gpGraphics->mFramebufferExtent2D.height,
			.layers = 1,
		};
		CHECK_VK(vkCreateFramebuffer(gpDeviceManager->mVkDevice, &vkFramebufferCreateInfo, nullptr, &rFrameBuffer.presentVkFramebuffer));
	}

	mImageAvailableFences.resize(kiCommandBuffersPerFramebuffer * uiImageCount);
	miImageAvailableIndex = 0;
	for (int64_t i = 0; i < static_cast<int64_t>(mImageAvailableFences.size()); ++i)
	{
		VkFenceCreateInfo vkFenceCreateInfo
		{
			.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
			.pNext = nullptr,
			.flags = VK_FENCE_CREATE_SIGNALED_BIT,
		};
		CHECK_VK(vkCreateFence(gpDeviceManager->mVkDevice, &vkFenceCreateInfo, nullptr, &mImageAvailableFences.at(i)));
	}

	mImageAvailableSemaphores.resize(kiCommandBuffersPerFramebuffer * (uiImageCount + 1));
	miImageAvailableIndex = 0;
	for (int64_t i = 0; i < static_cast<int64_t>(mImageAvailableSemaphores.size()); ++i)
	{
		VkSemaphoreCreateInfo vkSemaphoreCreateInfo
		{
			.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
			.pNext = nullptr,
			.flags = 0,
		};
		CHECK_VK(vkCreateSemaphore(gpDeviceManager->mVkDevice, &vkSemaphoreCreateInfo, nullptr, &mImageAvailableSemaphores.at(i)));
	}
}

SwapchainManager::~SwapchainManager()
{
	for (const VkFence& rVkSemaphore : mImageAvailableFences)
	{
		// https://github.com/KhronosGroup/Vulkan-ValidationLayers/issues/85
		vkWaitForFences(gpDeviceManager->mVkDevice, 1, &rVkSemaphore, VK_TRUE, kFenceTimeoutNs.count());

		vkDestroyFence(gpDeviceManager->mVkDevice, rVkSemaphore, nullptr);
	}

	for (const VkSemaphore& rVkSemaphore : mImageAvailableSemaphores)
	{
		vkDestroySemaphore(gpDeviceManager->mVkDevice, rVkSemaphore, nullptr);
	}

	for (const Framebuffer& rFramebuffer : mFramebuffers)
	{
		vkDestroyFramebuffer(gpDeviceManager->mVkDevice, rFramebuffer.presentVkFramebuffer, nullptr);

		vkDestroyImageView(gpDeviceManager->mVkDevice, rFramebuffer.presentVkImageView, nullptr);
	}

	vkDestroySwapchainKHR(gpDeviceManager->mVkDevice, mVkSwapchainKHR, nullptr);
	vkDestroyRenderPass(gpDeviceManager->mVkDevice, mVkRenderPass, nullptr);

	gpSwapchainManager = nullptr;
}

void SwapchainManager::AcquireNextImage()
{
	SCOPED_CPU_PROFILE(kCpuTimerAcquireImage);

	if (mCurrentImageAvailableVkFence != VK_NULL_HANDLE)
	{
		// Make sure previous image has been fully acquired before proceeding
		SCOPED_CPU_PROFILE(kCpuTimerAcquireImageFence);
		CHECK_VK(vkWaitForFences(gpDeviceManager->mVkDevice, 1, &mCurrentImageAvailableVkFence, VK_TRUE, kFenceTimeoutNs.count()));
		mCurrentImageAvailableVkFence = VK_NULL_HANDLE;
	}

	mCurrentImageAvailableVkFence = GetNextImageAvailableFence();
	CHECK_VK(vkResetFences(gpDeviceManager->mVkDevice, 1, &mCurrentImageAvailableVkFence));

	// Find out the index of the next image
	uint32_t uiFramebufferIndex = 0xFFFFFFFF;
	mImageAvailableVkSemaphore = GetNextImageAvailableSemaphore();
	CHECK_VK(vkAcquireNextImageKHR(gpDeviceManager->mVkDevice, mVkSwapchainKHR, UINT64_MAX, mImageAvailableVkSemaphore, mCurrentImageAvailableVkFence, &uiFramebufferIndex));
	ASSERT(uiFramebufferIndex != 0xFFFFFFFF);
	miFramebufferIndex = uiFramebufferIndex;
}

void SwapchainManager::Present()
{
#if defined(ENABLE_RENDER_THREAD)
	mPresent = std::async(std::launch::async, [this]()
	{
		SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
#endif

		CommandBuffers& rCommandBuffers = gpCommandBufferManager->mPerFramebufferCommandBuffers.at(gpSwapchainManager->miFramebufferIndex);

		uint32_t uiCurrentFramebufferIndex = static_cast<uint32_t>(gpSwapchainManager->miFramebufferIndex);
		VkPresentInfoKHR vkPresentInfoKHR
		{
			.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
			.pNext = nullptr,
			.waitSemaphoreCount = 1,
			.pWaitSemaphores = &rCommandBuffers.mpImageFinishedVkSemaphores[rCommandBuffers.miCurrentIndex],
			.swapchainCount = 1,
			.pSwapchains = &mVkSwapchainKHR,
			.pImageIndices = &uiCurrentFramebufferIndex,
			.pResults = nullptr,
		};

	#if defined(ENABLE_RENDER_THREAD)
		gpCommandBufferManager->mSubmitImage.get();
	#endif
	
		CPU_PROFILE_START(kCpuTimerPresent);
		CHECK_VK(vkQueuePresentKHR(gpDeviceManager->mPresentVkQueue, &vkPresentInfoKHR));
		CPU_PROFILE_STOP(kCpuTimerPresent);

		rCommandBuffers.Next();
	#if defined(ENABLE_RENDER_THREAD)
	});
#endif
}

void SwapchainManager::ReduceInputLag()
{
	if (gReduceInputLag.Get<bool>() && gpSwapchainManager->mCurrentImageAvailableVkFence != VK_NULL_HANDLE)
	{
		// Block the CPU here to reduce input lag to a minimum
		SCOPED_CPU_PROFILE(kCpuTimerReduceInputLagFence);
		CHECK_VK(vkWaitForFences(gpDeviceManager->mVkDevice, 1, &gpSwapchainManager->mCurrentImageAvailableVkFence, VK_TRUE, kFenceTimeoutNs.count()));
		mCurrentImageAvailableVkFence = VK_NULL_HANDLE;
	}
}

} // namespace engine
