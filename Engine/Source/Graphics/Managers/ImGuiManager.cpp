#include "ImGuiManager.h"

#include "Profile/ProfileManager.h"

#include "Game.h"

#include "Data/Raw.h"

namespace engine
{

ImGuiManager::ImGuiManager(HWND hwnd)
{
	gpImGuiManager = this;

	CreateRenderPass();
	CreateFramebuffers();

	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& rIo = ImGui::GetIO();
	rIo.IniFilename = nullptr;
	rIo.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
	rIo.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;

	// Load EFIGS font (default) with oversampling for crisp rendering
	const EagerChunk& rEfigsFontChunk = gpFileManager->GetEagerChunkMap().at(data::kRawRobotoMediumttfCrc);
	ImFontConfig fontConfig;
	fontConfig.OversampleH = 2;
	fontConfig.OversampleV = 1;
	fontConfig.FontDataOwnedByAtlas = false;
	ImGui::GetIO().Fonts->AddFontFromMemoryTTF(rEfigsFontChunk.pData, static_cast<int>(rEfigsFontChunk.pHeader->iSize), 26.0f, &fontConfig);

	// Load Chinese font for CJK text support
	const EagerChunk& rChineseFontChunk = gpFileManager->GetEagerChunkMap().at(data::kRawNotoSansSCLightotfCrc);
	mpChineseFont = ImGui::GetIO().Fonts->AddFontFromMemoryTTF(rChineseFontChunk.pData, static_cast<int>(rChineseFontChunk.pHeader->iSize), 26.0f, &fontConfig);

	ImGui_ImplWin32_Init(hwnd);

	ImGui_ImplVulkan_InitInfo initInfo
	{
		.Instance = gpInstanceManager->mVkInstance,
		.PhysicalDevice = gpInstanceManager->mVkPhysicalDevice,
		.Device = gpDeviceManager->mVkDevice,
		.QueueFamily = static_cast<uint32_t>(gpInstanceManager->miGraphicsQueueFamilyIndex),
		.Queue = gpDeviceManager->mGraphicsVkQueue,
		.DescriptorPool = gpDeviceManager->mVkDescriptorPool,
		.MinImageCount = static_cast<uint32_t>(gpSwapchainManager->mFramebuffers.size()),
		.ImageCount = static_cast<uint32_t>(gpSwapchainManager->mFramebuffers.size()),
		.PipelineInfoMain
		{
			.RenderPass = mImGuiRenderPass,
			.Subpass = 0,
			.MSAASamples = VK_SAMPLE_COUNT_1_BIT,
		},
		.MinAllocationSize = 1024 * 1024,
	};
	ImGui_ImplVulkan_Init(&initInfo);

	// Scale UI element sizes to 2x
	ImGui::GetStyle().ScaleAllSizes(2.0f);

	// Do a dummy frame cycle to ensure ImGui is in a clean state
	// NewFrame triggers font atlas creation, then upload textures before EndFrame validates them
	ImGui_ImplVulkan_NewFrame();
	ImGui_ImplWin32_NewFrame();
	ImGui::NewFrame();

	// Upload font atlas texture created by NewFrame above
	for (ImTextureData* pTexture : ImGui::GetPlatformIO().Textures)
	{
		if (pTexture->Status != ImTextureStatus_OK)
		{
			ImGui_ImplVulkan_UpdateTexture(pTexture);
		}
	}

	ImGui::EndFrame();

	// Initialize game screens after ImGui backend is ready
	mHudScreen.Initialize();
}

void ImGuiManager::RecreateSamplerDependencies()
{
	mHudScreen.Shutdown();
	mHudScreen.Initialize();
}

ImGuiManager::~ImGuiManager()
{
	// Shutdown game screens before ImGui backend is destroyed
	mHudScreen.Shutdown();

	ImGui_ImplVulkan_Shutdown();
	ImGui_ImplWin32_Shutdown();
	ImGui::DestroyContext();

	for (const VkFramebuffer vkFramebuffer : mImGuiFramebuffers)
	{
		vkDestroyFramebuffer(gpDeviceManager->mVkDevice, vkFramebuffer, nullptr);
	}

	vkDestroyRenderPass(gpDeviceManager->mVkDevice, mImGuiRenderPass, nullptr);

	gpImGuiManager = nullptr;
}

void ImGuiManager::CreateRenderPass()
{
	VkAttachmentDescription vkAttachmentDescription
	{
		.flags = 0,
		.format = gpInstanceManager->mFramebufferVkFormat,
		.samples = VK_SAMPLE_COUNT_1_BIT,
		.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
		.storeOp = VK_ATTACHMENT_STORE_OP_STORE,
		.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
		.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
		.initialLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
		.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
	};

	VkAttachmentReference vkAttachmentReference
	{
		.attachment = 0,
		.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
	};

	VkSubpassDescription vkSubpassDescription
	{
		.flags = 0,
		.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
		.inputAttachmentCount = 0,
		.pInputAttachments = nullptr,
		.colorAttachmentCount = 1,
		.pColorAttachments = &vkAttachmentReference,
		.pResolveAttachments = nullptr,
		.pDepthStencilAttachment = nullptr,
		.preserveAttachmentCount = 0,
		.pPreserveAttachments = nullptr,
	};

	VkSubpassDependency vkSubpassDependency
	{
		.srcSubpass = VK_SUBPASS_EXTERNAL,
		.dstSubpass = 0,
		.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
		.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
		.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
		.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
		.dependencyFlags = 0,
	};

	VkRenderPassCreateInfo vkRenderPassCreateInfo
	{
		.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
		.pNext = nullptr,
		.flags = 0,
		.attachmentCount = 1,
		.pAttachments = &vkAttachmentDescription,
		.subpassCount = 1,
		.pSubpasses = &vkSubpassDescription,
		.dependencyCount = 1,
		.pDependencies = &vkSubpassDependency,
	};
	CHECK_VK(vkCreateRenderPass(gpDeviceManager->mVkDevice, &vkRenderPassCreateInfo, nullptr, &mImGuiRenderPass));
	VkName(VK_OBJECT_TYPE_RENDER_PASS, mImGuiRenderPass, "ImGui");
}

void ImGuiManager::CreateFramebuffers()
{
	mImGuiFramebuffers.resize(gpSwapchainManager->mFramebuffers.size());
	for (size_t i = 0; i < mImGuiFramebuffers.size(); ++i)
	{
		VkImageView vkImageView = gpSwapchainManager->mFramebuffers.at(i).presentVkImageView;
		VkFramebufferCreateInfo vkFramebufferCreateInfo
		{
			.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
			.pNext = nullptr,
			.flags = 0,
			.renderPass = mImGuiRenderPass,
			.attachmentCount = 1,
			.pAttachments = &vkImageView,
			.width = gpGraphics->mFramebufferExtent2D.width,
			.height = gpGraphics->mFramebufferExtent2D.height,
			.layers = 1,
		};
		CHECK_VK(vkCreateFramebuffer(gpDeviceManager->mVkDevice, &vkFramebufferCreateInfo, nullptr, &mImGuiFramebuffers.at(i)));
		VkName(VK_OBJECT_TYPE_FRAMEBUFFER, mImGuiFramebuffers.at(i), std::format("ImGui_{}", i).c_str());
	}
}

void ImGuiManager::Submit(int64_t iFramebuffer)
{
	ImGui_ImplVulkan_NewFrame();
	ImGui_ImplWin32_NewFrame();
	ImGui::NewFrame();

	// Render HUD (background layer, visible during gameplay)
	mHudScreen.Render();

	// Render menu screens based on UiState
	mMainMenuScreen.Render();
	mPauseMenuScreen.Render();
	mGraphicsMenuScreen.Render();
	mSoundMenuScreen.Render();
	mDeathMenuScreen.Render();

	mTweaksScreen.Render();

	ImGui::Render();
	mpDrawData = ImGui::GetDrawData();

	CommandBuffers& rCommandBuffers = gpCommandBufferManager->mPerFramebufferCommandBuffers.at(iFramebuffer);

	CHECK_VK(vkResetCommandBuffer(rCommandBuffers.mImGuiVkCommandBuffer, 0));

	VkCommandBufferBeginInfo vkCommandBufferBeginInfo
	{
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
		.pNext = nullptr,
		.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
		.pInheritanceInfo = nullptr,
	};
	CHECK_VK(vkBeginCommandBuffer(rCommandBuffers.mImGuiVkCommandBuffer, &vkCommandBufferBeginInfo));

	gpProfileManager->ResetUiQueryPool(iFramebuffer, rCommandBuffers.mImGuiVkCommandBuffer);
	gpProfileManager->GpuStart(iFramebuffer, rCommandBuffers.mImGuiVkCommandBuffer, kGpuTimerUiRender);

	VkRenderPassBeginInfo vkRenderPassBeginInfo
	{
		.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
		.pNext = nullptr,
		.renderPass = mImGuiRenderPass,
		.framebuffer = mImGuiFramebuffers.at(iFramebuffer),
		.renderArea = VkRect2D
		{
			.offset = {0, 0},
			.extent = gpGraphics->mFramebufferExtent2D,
		},
		.clearValueCount = 0,
		.pClearValues = nullptr,
	};
	vkCmdBeginRenderPass(rCommandBuffers.mImGuiVkCommandBuffer, &vkRenderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

	ImGui_ImplVulkan_RenderDrawData(mpDrawData, rCommandBuffers.mImGuiVkCommandBuffer);

	vkCmdEndRenderPass(rCommandBuffers.mImGuiVkCommandBuffer);

	gpProfileManager->GpuStop(iFramebuffer, rCommandBuffers.mImGuiVkCommandBuffer, kGpuTimerUiRender);

	CHECK_VK(vkEndCommandBuffer(rCommandBuffers.mImGuiVkCommandBuffer));

	VkPipelineStageFlags vkWaitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	VkSubmitInfo vkSubmitInfo
	{
		.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
		.pNext = nullptr,
		.waitSemaphoreCount = 1,
		.pWaitSemaphores = &rCommandBuffers.mMainFinishedVkSemaphore,
		.pWaitDstStageMask = &vkWaitStage,
		.commandBufferCount = 1,
		.pCommandBuffers = &rCommandBuffers.mImGuiVkCommandBuffer,
		.signalSemaphoreCount = 1,
		.pSignalSemaphores = &rCommandBuffers.mImGuiFinishedVkSemaphore,
	};
	CHECK_VK(vkQueueSubmit(gpDeviceManager->mGraphicsVkQueue, 1, &vkSubmitInfo, rCommandBuffers.mVkFence));
}

} // namespace engine
