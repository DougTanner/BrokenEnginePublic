#if defined(BT_CLIENT)

#include "SwapchainManager.h"

#include "Profile/ProfileManager.h"
#include "Ui/GraphicsSettingsWrappersBase.h"

namespace engine
{

SwapchainManager::SwapchainManager(VkSwapchainKHR oldSwapchain)
: mPresent(common::kThreadPresent)
{
	ASSERT(gpSwapchainManager == nullptr);

	gpSwapchainManager = this;

	ScopedBootTimer scopedBootTimer(kBootTimerSwapchainManager);

	CreateRenderPass();
	CreateSwapchain(oldSwapchain);
	CreateFramebuffers();
	CreateSyncObjects();
}

void SwapchainManager::CreateRenderPass()
{
	// The scene renders into an F16 HDR intermediate so accumulated PBR / emissive / additive-particle
	// highlights are not clamped by the UNORM swapchain. The fullscreen HDR-resolve pass (kPipelineHdrResolve)
	// then tone-maps + color-grades the whole frame into the swapchain via the simplified mVkRenderPass below.

	// --- HDR scene render pass: today's structure (color + depth + optional MSAA) in F16, color ends up
	//     SHADER_READ_ONLY so the resolve pass can sample it in the same command buffer. ---
	VkFormat hdrVkFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
	VkAttachmentDescription pHdrVkAttachmentDescriptions[]
	{
		// HDR color (resolve target when multisampling)
		VkAttachmentDescription
		{
			.flags = 0,
			.format = hdrVkFormat,
			.samples = VK_SAMPLE_COUNT_1_BIT,
			.loadOp = kbFramebufferClearColor
				? (gMultisampling.Get<bool>() ? VK_ATTACHMENT_LOAD_OP_DONT_CARE : VK_ATTACHMENT_LOAD_OP_CLEAR)
				: VK_ATTACHMENT_LOAD_OP_DONT_CARE,
			.storeOp = VK_ATTACHMENT_STORE_OP_STORE,
			.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
			.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
			.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
			.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
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
		// Multisample HDR framebuffer
		VkAttachmentDescription
		{
			.flags = 0,
			.format = hdrVkFormat,
			.samples = gSampleCount.Get<VkSampleCountFlagBits>(),
			.loadOp = kbFramebufferClearColor ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_DONT_CARE,
			.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
			.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
			.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
			.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
			.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
		}
	};

	VkAttachmentReference hdrColorVkAttachmentReference
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
	VkSubpassDescription hdrVkSubpassDescription
	{
		.flags = 0,
		.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
		.inputAttachmentCount = 0,
		.pInputAttachments = nullptr,
		.colorAttachmentCount = 1,
		.pColorAttachments = gMultisampling.Get<bool>() ? &multisamplingVkAttachmentReference : &hdrColorVkAttachmentReference,
		.pResolveAttachments = gMultisampling.Get<bool>() ? &hdrColorVkAttachmentReference : nullptr,
		.pDepthStencilAttachment = &depthVkAttachmentReference,
		.preserveAttachmentCount = 0,
		.pPreserveAttachments = nullptr,
	};

	// Incoming: WAR against the previous frame's resolve pass sampling this HDR color (FRAGMENT_SHADER read),
	// plus the acquire-time color/depth transition. Outgoing: order the same-command-buffer resolve pass's
	// sampling (COLOR_ATTACHMENT_WRITE -> FRAGMENT_SHADER SHADER_READ).
	// Incoming also covers a depth WAW: the depth attachment is the single mDepthTexture reused every frame with
	// UNDEFINED initial layout + CLEAR loadOp, so the previous frame's DEPTH_STENCIL_ATTACHMENT_WRITEs (at the
	// EARLY/LATE_FRAGMENT_TESTS stages) must be made available before this frame's automatic
	// UNDEFINED -> DEPTH_STENCIL_ATTACHMENT_OPTIMAL transition/clear -- otherwise a latent write-after-write the
	// sync-validation layer flags.
	VkSubpassDependency pHdrVkSubpassDependencies[]
	{
		VkSubpassDependency
		{
			.srcSubpass = VK_SUBPASS_EXTERNAL,
			.dstSubpass = 0,
			.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
			.dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
			.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
			.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
			.dependencyFlags = 0,
		},
		VkSubpassDependency
		{
			.srcSubpass = 0,
			.dstSubpass = VK_SUBPASS_EXTERNAL,
			.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
			.dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
			.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
			.dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
			.dependencyFlags = 0,
		},
	};

	VkRenderPassCreateInfo hdrVkRenderPassCreateInfo
	{
		.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
		.pNext = nullptr,
		.flags = 0,
		.attachmentCount = gMultisampling.Get<bool>() ? 3u : 2u,
		.pAttachments = pHdrVkAttachmentDescriptions,
		.subpassCount = 1,
		.pSubpasses = &hdrVkSubpassDescription,
		.dependencyCount = static_cast<uint32_t>(std::size(pHdrVkSubpassDependencies)),
		.pDependencies = pHdrVkSubpassDependencies,
	};
	CHECK_VK(vkCreateRenderPass(gpDeviceManager->mVkDevice, &hdrVkRenderPassCreateInfo, nullptr, &mHdrVkRenderPass));
	VkName(VK_OBJECT_TYPE_RENDER_PASS, mHdrVkRenderPass, "SwapchainManagerHdr");

	// --- Present render pass: single color attachment, single sample. The resolve quad is its only client
	//     (all scene pipelines now target the HDR pass), so no depth/MSAA. DONT_CARE load (fully overwritten),
	//     PRESENT_SRC_KHR final. Keep the acquire-semaphore incoming dependency (color-only). ---
	VkAttachmentDescription presentVkAttachmentDescription
	{
		.flags = 0,
		.format = gpInstanceManager->mFramebufferVkFormat,
		.samples = VK_SAMPLE_COUNT_1_BIT,
		.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
		.storeOp = VK_ATTACHMENT_STORE_OP_STORE,
		.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
		.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
		.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
		.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
	};
	VkAttachmentReference presentVkAttachmentReference
	{
		.attachment = 0,
		.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
	};
	VkSubpassDescription presentVkSubpassDescription
	{
		.flags = 0,
		.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
		.inputAttachmentCount = 0,
		.pInputAttachments = nullptr,
		.colorAttachmentCount = 1,
		.pColorAttachments = &presentVkAttachmentReference,
		.pResolveAttachments = nullptr,
		.pDepthStencilAttachment = nullptr,
		.preserveAttachmentCount = 0,
		.pPreserveAttachments = nullptr,
	};
	// Wait to write colors until the acquired image is available (see the swapchain acquire semaphore chain).
	VkSubpassDependency presentVkSubpassDependency
	{
		.srcSubpass = VK_SUBPASS_EXTERNAL,
		.dstSubpass = 0,
		.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
		.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
		.srcAccessMask = 0,
		.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
		.dependencyFlags = 0,
	};
	VkRenderPassCreateInfo vkRenderPassCreateInfo
	{
		.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
		.pNext = nullptr,
		.flags = 0,
		.attachmentCount = 1,
		.pAttachments = &presentVkAttachmentDescription,
		.subpassCount = 1,
		.pSubpasses = &presentVkSubpassDescription,
		.dependencyCount = 1,
		.pDependencies = &presentVkSubpassDependency,
	};
	CHECK_VK(vkCreateRenderPass(gpDeviceManager->mVkDevice, &vkRenderPassCreateInfo, nullptr, &mVkRenderPass));
	VkName(VK_OBJECT_TYPE_RENDER_PASS, mVkRenderPass, "SwapchainManager");
}

void SwapchainManager::CreateSwapchain(VkSwapchainKHR oldSwapchain)
{
	// The swapchain is essentially a queue of images that are waiting to be presented to the screen
	// Our application will acquire such an image to draw to it, and then return it to the queue
	// How exactly the queue works and the conditions for presenting an image from the queue depend on how the swapchain is set up
	// The general purpose of the swapchain is to synchronize the presentation of images with the refresh rate of the screen.
	VkSurfaceCapabilitiesKHR vkSurfaceCapabilitiesKHR {};
	CHECK_VK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(gpInstanceManager->mVkPhysicalDevice, gpInstanceManager->mVkSurfaceKHR, &vkSurfaceCapabilitiesKHR));
	ASSERT((vkSurfaceCapabilitiesKHR.supportedCompositeAlpha & (VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR | VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR)) != 0);

	// Validate swapchain image usage flags against surface capabilities
	ASSERT((vkSurfaceCapabilitiesKHR.supportedUsageFlags & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) != 0);
	VkImageUsageFlags swapchainUsageFlags = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
	if constexpr (kbScreenshots)
	{
		if ((vkSurfaceCapabilitiesKHR.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) != 0)
		{
			swapchainUsageFlags |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
		}
		else
		{
			LOG(kGraphics, kWarning, "WARNING: Screenshot functionality will be disabled - VK_IMAGE_USAGE_TRANSFER_SRC_BIT not supported by surface");
		}
	}

	uint32_t uiPresentModeCount = 0;
	CHECK_VK(vkGetPhysicalDeviceSurfacePresentModesKHR(gpInstanceManager->mVkPhysicalDevice, gpInstanceManager->mVkSurfaceKHR, &uiPresentModeCount, nullptr));
	ASSERT(uiPresentModeCount != 0);
	std::vector<VkPresentModeKHR> physicalDevicePresentModes(uiPresentModeCount);
	CHECK_VK(vkGetPhysicalDeviceSurfacePresentModesKHR(gpInstanceManager->mVkPhysicalDevice, gpInstanceManager->mVkSurfaceKHR, &uiPresentModeCount, physicalDevicePresentModes.data()));

	LOG(kGraphics, kInfo, "Present modes ({}):", physicalDevicePresentModes.size());
	// FIFO is guaranteed to be available
	VkPresentModeKHR eVkPresentModeKHR = VK_PRESENT_MODE_FIFO_KHR;
	for (const VkPresentModeKHR& reVkPresentModeKHR : physicalDevicePresentModes)
	{
		LOG(kGraphics, kInfo, "  {}", string_VkPresentModeKHR(reVkPresentModeKHR));

		if (reVkPresentModeKHR == VK_PRESENT_MODE_MAILBOX_KHR && gPresentMode.Get<VkPresentModeKHR>() == VK_PRESENT_MODE_MAILBOX_KHR)
		{
			eVkPresentModeKHR = VK_PRESENT_MODE_MAILBOX_KHR;
		}
		else if (reVkPresentModeKHR == VK_PRESENT_MODE_IMMEDIATE_KHR && gPresentMode.Get<VkPresentModeKHR>() == VK_PRESENT_MODE_IMMEDIATE_KHR)
		{
			eVkPresentModeKHR = VK_PRESENT_MODE_IMMEDIATE_KHR;
		}
	}
	LOG(kGraphics, kInfo, "Present mode selected: {}", string_VkPresentModeKHR(eVkPresentModeKHR));
	gPresentMode.Reset(eVkPresentModeKHR);

	// The swap extent is the resolution of the swap chain images and it's almost always exactly equal to the resolution of the window that we're drawing to
	RECT clientRect {};
	GetClientRect(gpGraphics->mHwnd, &clientRect);
	LOG(kGraphics, kDebug, "Swapchain extent resolution: currentExtent {} x {}, minImageExtent {} x {}, maxImageExtent {} x {}, client rect {} x {}", vkSurfaceCapabilitiesKHR.currentExtent.width, vkSurfaceCapabilitiesKHR.currentExtent.height, vkSurfaceCapabilitiesKHR.minImageExtent.width, vkSurfaceCapabilitiesKHR.minImageExtent.height, vkSurfaceCapabilitiesKHR.maxImageExtent.width, vkSurfaceCapabilitiesKHR.maxImageExtent.height, clientRect.right - clientRect.left, clientRect.bottom - clientRect.top);

	if (vkSurfaceCapabilitiesKHR.currentExtent.width == 0xFFFFFFFF)
	{
		// If the surface size is undefined, the size is set to the size of the images requested
		gpGraphics->mFramebufferExtent2D.width = std::clamp(gWantedFramebufferExtent2D.width, vkSurfaceCapabilitiesKHR.minImageExtent.width, vkSurfaceCapabilitiesKHR.maxImageExtent.width);
		gpGraphics->mFramebufferExtent2D.height = std::clamp(gWantedFramebufferExtent2D.height, vkSurfaceCapabilitiesKHR.minImageExtent.height, vkSurfaceCapabilitiesKHR.maxImageExtent.height);
	}
	else
	{
		// If the surface size is defined, the swapchain size must match. Clamp to [minImageExtent, maxImageExtent]
		// symmetrically with the undefined branch — a degenerate currentExtent must never reach .imageExtent or any attachment.
		gpGraphics->mFramebufferExtent2D.width = std::clamp(vkSurfaceCapabilitiesKHR.currentExtent.width, vkSurfaceCapabilitiesKHR.minImageExtent.width, vkSurfaceCapabilitiesKHR.maxImageExtent.width);
		gpGraphics->mFramebufferExtent2D.height = std::clamp(vkSurfaceCapabilitiesKHR.currentExtent.height, vkSurfaceCapabilitiesKHR.minImageExtent.height, vkSurfaceCapabilitiesKHR.maxImageExtent.height);

		gWantedFramebufferExtent2D = gpGraphics->mFramebufferExtent2D;
	}

	mfAspectRatio = static_cast<float>(gpGraphics->mFramebufferExtent2D.width) / static_cast<float>(gpGraphics->mFramebufferExtent2D.height);

	// Using double buffering and vsync locks rendering to an integer fraction of the vsync rate. In turn, reducing the performance of the application if rendering is slower than vsync. Consider setting minImageCount to 3 to use triple buffering to maximize performance in such cases.
	uint32_t uiMinImageCount = std::max(3u, vkSurfaceCapabilitiesKHR.minImageCount);
	if (vkSurfaceCapabilitiesKHR.maxImageCount > 0)
	{
		uiMinImageCount = std::min(uiMinImageCount, vkSurfaceCapabilitiesKHR.maxImageCount);
	}
	LOG(kGraphics, kDebug, "uiMinImageCount: {}", uiMinImageCount);

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
		.minImageCount = uiMinImageCount,
		.imageFormat = gpInstanceManager->mFramebufferVkFormat,
		.imageColorSpace = gpInstanceManager->mFramebufferVkColorSpace,
		.imageExtent = gpGraphics->mFramebufferExtent2D,
		.imageArrayLayers = 1,
		.imageUsage = swapchainUsageFlags,
		.imageSharingMode = bDifferentQueueFamilies ? VK_SHARING_MODE_CONCURRENT : VK_SHARING_MODE_EXCLUSIVE,
		.queueFamilyIndexCount = bDifferentQueueFamilies ? 2u : 0u,
		.pQueueFamilyIndices = bDifferentQueueFamilies ? &pQueueFamilyIndices[0] : nullptr,
		.preTransform = (vkSurfaceCapabilitiesKHR.supportedTransforms & VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR) != 0 ? VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR : vkSurfaceCapabilitiesKHR.currentTransform,
		.compositeAlpha = (vkSurfaceCapabilitiesKHR.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR) != 0 ? VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR : VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR,
		.presentMode = gPresentMode.Get<VkPresentModeKHR>(),
		.clipped = VK_TRUE,
		.oldSwapchain = oldSwapchain,
	};
	CHECK_VK(vkCreateSwapchainKHR(gpDeviceManager->mVkDevice, &vkSwapchainCreateInfoKHR, nullptr, &mVkSwapchainKHR));

	// Destroy old swapchain after successfully creating new one
	if (oldSwapchain != VK_NULL_HANDLE)
	{
		vkDestroySwapchainKHR(gpDeviceManager->mVkDevice, oldSwapchain, nullptr);
	}
	VkName(VK_OBJECT_TYPE_SWAPCHAIN_KHR, mVkSwapchainKHR, "");
}

void SwapchainManager::CreateFramebuffers()
{
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
		.name = "Depth",
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

	// Multisampling (F16 HDR MSAA attachment for mHdrVkRenderPass, resolved into mHdrTexture)
	if (gMultisampling.Get<bool>())
	{
		mMultisamplingTexture.Create(TextureInfo
		{
			.textureFlags = {},
			.name = "Multisampling",
			.flags = 0,
			.format = VK_FORMAT_R16G16B16A16_SFLOAT,
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

	// HDR intermediate color: the scene render pass writes here; the resolve pass samples it.
	mHdrTexture.Create(TextureInfo
	{
		.textureFlags = {},
		.name = "Hdr",
		.flags = 0,
		.format = VK_FORMAT_R16G16B16A16_SFLOAT,
		.extent = VkExtent3D {.width = gpGraphics->mFramebufferExtent2D.width, .height = gpGraphics->mFramebufferExtent2D.height, .depth = 1},
		.mipLevels = 1,
		.arrayLayers = 1,
		.samples = VK_SAMPLE_COUNT_1_BIT,
		.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
		.viewType = VK_IMAGE_VIEW_TYPE_2D,
		.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
		.eTextureLayout = TextureLayout::kShaderReadOnly,
	});

	// Single HDR framebuffer: none of its attachments (HDR color / depth / MSAA) are per-swapchain-image.
	// Attachment order matches mHdrVkRenderPass: HDR color (0), depth (1), MSAA (2).
	VkImageView pHdrVkImageViews[] {mHdrTexture.mVkImageView, mDepthTexture.mVkImageView, mMultisamplingTexture.mVkImageView};
	VkFramebufferCreateInfo hdrVkFramebufferCreateInfo
	{
		.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
		.pNext = nullptr,
		.flags = 0,
		.renderPass = mHdrVkRenderPass,
		.attachmentCount = gMultisampling.Get<bool>() ? 3u : 2u,
		.pAttachments = pHdrVkImageViews,
		.width = gpGraphics->mFramebufferExtent2D.width,
		.height = gpGraphics->mFramebufferExtent2D.height,
		.layers = 1,
	};
	CHECK_VK(vkCreateFramebuffer(gpDeviceManager->mVkDevice, &hdrVkFramebufferCreateInfo, nullptr, &mHdrVkFramebuffer));
	VkName(VK_OBJECT_TYPE_FRAMEBUFFER, mHdrVkFramebuffer, "Hdr");

	mFramebuffers.resize(uiImageCount);
	miFramebufferIndex = 0;
	for (int64_t i = 0; Framebuffer& rFrameBuffer : mFramebuffers)
	{
		rFrameBuffer.presentVkImage = swapchainImages.at(i++);

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
		VkName(VK_OBJECT_TYPE_IMAGE, rFrameBuffer.presentVkImage, std::format("Present {}", i - 1).c_str());
		VkName(VK_OBJECT_TYPE_IMAGE_VIEW, rFrameBuffer.presentVkImageView, std::format("Present {}", i - 1).c_str());

		// The attachments specified during render pass creation are bound by wrapping them into a VkFramebuffer object
		// A framebuffer object references all of the VkImageView objects that represent the attachments
		// However, the image that we have to use as attachment depends on which image the swap chain returns when we retrieve one for presentation
		// That means that we have to create a framebuffer for all of the images in the swap chain and use the one that corresponds to the retrieved image at drawing time.
		// Single color attachment: the present pass only runs the HDR-resolve quad (depth/MSAA live on mHdrVkFramebuffer).
		VkImageView pVkImageViews[] {rFrameBuffer.presentVkImageView};
		VkFramebufferCreateInfo vkFramebufferCreateInfo
		{
			.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
			.pNext = nullptr,
			.flags = 0,
			.renderPass = mVkRenderPass,
			.attachmentCount = 1,
			.pAttachments = pVkImageViews,
			.width = gpGraphics->mFramebufferExtent2D.width,
			.height = gpGraphics->mFramebufferExtent2D.height,
			.layers = 1,
		};
		CHECK_VK(vkCreateFramebuffer(gpDeviceManager->mVkDevice, &vkFramebufferCreateInfo, nullptr, &rFrameBuffer.presentVkFramebuffer));
		VkName(VK_OBJECT_TYPE_FRAMEBUFFER, rFrameBuffer.presentVkFramebuffer, std::format("Present {}", i - 1).c_str());
	}
}

void SwapchainManager::CreateSyncObjects()
{
	mImageAvailableFences.resize(mFramebuffers.size());
	miFenceAvailableIndex = 0;
	for ([[maybe_unused]] int64_t i = 0; VkFence& rFence : mImageAvailableFences)
	{
		VkFenceCreateInfo vkFenceCreateInfo
		{
			.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
			.pNext = nullptr,
			.flags = VK_FENCE_CREATE_SIGNALED_BIT,
		};
		CHECK_VK(vkCreateFence(gpDeviceManager->mVkDevice, &vkFenceCreateInfo, nullptr, &rFence));
		VkName(VK_OBJECT_TYPE_FENCE, rFence, std::format("ImageAvailable {}", i++).c_str());
	}

	mImageAvailableSemaphores.resize(mFramebuffers.size() + 1);
	miImageAvailableIndex = 0;
	for ([[maybe_unused]] int64_t i = 0; VkSemaphore& rSemaphore : mImageAvailableSemaphores)
	{
		VkSemaphoreCreateInfo vkSemaphoreCreateInfo
		{
			.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
			.pNext = nullptr,
			.flags = 0,
		};
		CHECK_VK(vkCreateSemaphore(gpDeviceManager->mVkDevice, &vkSemaphoreCreateInfo, nullptr, &rSemaphore));
		VkName(VK_OBJECT_TYPE_SEMAPHORE, rSemaphore, std::format("ImageAvailable {}", i++).c_str());
	}
}

SwapchainManager::~SwapchainManager()
{
	for (const VkFence& rVkFence : mImageAvailableFences)
	{
		vkDestroyFence(gpDeviceManager->mVkDevice, rVkFence, nullptr);
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

	if (mVkSwapchainKHR != VK_NULL_HANDLE)
	{
		vkDestroySwapchainKHR(gpDeviceManager->mVkDevice, mVkSwapchainKHR, nullptr);
	}
	vkDestroyFramebuffer(gpDeviceManager->mVkDevice, mHdrVkFramebuffer, nullptr);
	vkDestroyRenderPass(gpDeviceManager->mVkDevice, mHdrVkRenderPass, nullptr);
	vkDestroyRenderPass(gpDeviceManager->mVkDevice, mVkRenderPass, nullptr);

	if (gpSwapchainManager == this)
	{
		gpSwapchainManager = nullptr;
	}
}

VkSwapchainKHR SwapchainManager::ReleaseHandleForRecreation()
{
	VkSwapchainKHR vkSwapchainKHR = mVkSwapchainKHR;
	mVkSwapchainKHR = VK_NULL_HANDLE;
	return vkSwapchainKHR;
}

void SwapchainManager::AcquireNextImage()
{
	ScopedCpuProfile scopedCpuProfile(kCpuTimerAcquireImage);

	if (mCurrentImageAvailableVkFence != VK_NULL_HANDLE)
	{
		// Make sure previous image has been fully acquired before proceeding
		ScopedCpuProfile scopedCpuProfileFence(kCpuTimerAcquireImageFence);
		CHECK_VK(vkWaitForFences(gpDeviceManager->mVkDevice, 1, &mCurrentImageAvailableVkFence, VK_TRUE, kFenceTimeoutNanoseconds.count()));
		mCurrentImageAvailableVkFence = VK_NULL_HANDLE;
	}

	// Find out the index of the next image
	mCurrentImageAvailableVkFence = GetNextImageAvailableFence();
	CHECK_VK(vkResetFences(gpDeviceManager->mVkDevice, 1, &mCurrentImageAvailableVkFence));
	mImageAvailableVkSemaphore = GetNextImageAvailableSemaphore();
	uint32_t uiFramebufferIndex = 0xFFFFFFFF;
	VkResult vkResult = vkAcquireNextImageKHR(gpDeviceManager->mVkDevice, mVkSwapchainKHR, UINT64_MAX, mImageAvailableVkSemaphore, mCurrentImageAvailableVkFence, &uiFramebufferIndex);
	if (vkResult == VK_SUCCESS)
	{
		miFramebufferIndex = uiFramebufferIndex;
	}
	// Handle stale swapchain by requesting deferred recreation
	else if (vkResult == VK_ERROR_OUT_OF_DATE_KHR || vkResult == VK_SUBOPTIMAL_KHR)
	{
		mCurrentImageAvailableVkFence = VK_NULL_HANDLE;
		// std::max, not plain assign: a same-frame kSurface escalation must never downgrade to kSwapchain.
		gpGraphics->meDestroyType = std::max(DestroyType::kSwapchain, gpGraphics->meDestroyType);
	}
	else
	{
		mCurrentImageAvailableVkFence = VK_NULL_HANDLE;
		CHECK_VK(vkResult);
	}
}

void SwapchainManager::PresentToQueue(int64_t iFramebufferIndex)
{
	CommandBuffers& rCommandBuffers = gpCommandBufferManager->mPerFramebufferCommandBuffers.at(iFramebufferIndex);

	uint32_t uiCurrentFramebufferIndex = static_cast<uint32_t>(iFramebufferIndex);
	VkSemaphore waitSemaphore = rCommandBuffers.mImGuiFinishedVkSemaphore;
	VkPresentInfoKHR vkPresentInfoKHR
	{
		.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
		.pNext = nullptr,
		.waitSemaphoreCount = 1,
		.pWaitSemaphores = &waitSemaphore,
		.swapchainCount = 1,
		.pSwapchains = &mVkSwapchainKHR,
		.pImageIndices = &uiCurrentFramebufferIndex,
		.pResults = nullptr,
	};

	gpProfileManager->CpuStart(kCpuTimerPresent);
	VkResult vkResult = vkQueuePresentKHR(gpDeviceManager->mPresentVkQueue, &vkPresentInfoKHR);
	gpProfileManager->CpuStop(kCpuTimerPresent);

	// Handle stale swapchain by requesting deferred recreation
	if (vkResult == VK_ERROR_OUT_OF_DATE_KHR || vkResult == VK_SUBOPTIMAL_KHR)
	{
		// This present-worker write is ordered before the main thread's Refresh() read-modify-write only by
		//   gpSwapchainManager->mPresent.Wait() in the kbRenderThread frame-tail block (Graphics::RenderMainPresentAcquire,
		//   before the next Create()). That same Wait also keeps the next frame's Global submit from racing this
		//   vkQueuePresentKHR on the shared queue. Preserve that Wait when refactoring the frame tail. Same "plain member
		//   published across a PersistentWorker Wake/Wait edge" family as CommandBufferManager's mbParticleSemaphoreSignaled
		//   and CommandBuffers.h (mFlags/mVkFence).
		// std::max, not plain assign: a same-frame kSurface escalation must never downgrade to kSwapchain.
		gpGraphics->meDestroyType = std::max(DestroyType::kSwapchain, gpGraphics->meDestroyType);
		return;
	}
	CHECK_VK(vkResult);
}

void SwapchainManager::Present(int64_t iFramebufferIndex)
{
	if constexpr (kbRenderThread)
	{
		mPresent.Wake([this, iFramebufferIndex]()
		{
			gpCommandBufferManager->mSubmitMain.Wait();
			PresentToQueue(iFramebufferIndex);
		});
	}
	else
	{
		PresentToQueue(iFramebufferIndex);
	}
}

} // namespace engine

#endif // defined(BT_CLIENT)
