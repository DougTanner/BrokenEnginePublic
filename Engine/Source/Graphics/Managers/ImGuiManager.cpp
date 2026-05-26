#include "ImGuiManager.h"

#include "Ui/GraphicsSettingsWrappersBase.h"
#include "Ui/Screens/DeathMenuScreen.h"
#include "Ui/Screens/GraphicsMenuScreen.h"
#include "Ui/Screens/HudScreen.h"
#include "Ui/Screens/MainMenuScreen.h"
#include "Ui/Screens/ModalScreen.h"
#include "Ui/Screens/PauseMenuScreen.h"
#include "Ui/Screens/SoundMenuScreen.h"
#include "Ui/Screens/TweaksScreen/TweaksScreen.h"

#include "Profile/ProfileManager.h"

#include "Data/Raw.h"

#include "Game.h"

namespace engine
{

ImGuiManager::ImGuiManager(HWND hwnd)
{
	gpImGuiManager = this;

	CreateRenderPass();
	CreateFramebuffers();

	// Create host-visible indirect draw buffer for UI depth pre-pass
	{
		int64_t iFramebufferCount = static_cast<int64_t>(gpSwapchainManager->mFramebuffers.size());
		VkBufferCreateInfo vkBufferCreateInfo
		{
			.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
			.size = static_cast<VkDeviceSize>(iFramebufferCount * sizeof(VkDrawIndirectCommand)),
			.usage = VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT,
			.sharingMode = VK_SHARING_MODE_EXCLUSIVE,
		};
		VmaAllocationCreateInfo vmaAllocationCreateInfo
		{
			.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
			.usage = VMA_MEMORY_USAGE_AUTO,
			.requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
		};
		VmaAllocationInfo vmaAllocationInfo {};
		CHECK_VK(vmaCreateBuffer(gpDeviceManager->mpAllocator, &vkBufferCreateInfo, &vmaAllocationCreateInfo, &mUiPrepassIndirectVkBuffer, &mUiPrepassIndirectVmaAllocation, &vmaAllocationInfo));
		VkName(VK_OBJECT_TYPE_BUFFER, mUiPrepassIndirectVkBuffer, "UiPrepassIndirect");
		mpUiPrepassIndirectMapped = static_cast<VkDrawIndirectCommand*>(vmaAllocationInfo.pMappedData);
		ASSERT(mpUiPrepassIndirectMapped != nullptr);
		__analysis_assume(mpUiPrepassIndirectMapped != nullptr);
		for (int64_t i = 0; i < iFramebufferCount; ++i)
		{
			mpUiPrepassIndirectMapped[i] = {.vertexCount = 6, .instanceCount = 0, .firstVertex = 0, .firstInstance = 0};
		}
	}

	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImPlot::CreateContext();
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

	// Set global dark window background with user-controlled opacity
	{
		ImGuiStyle& rStyle = ImGui::GetStyle();
		float fOpacity = gUiOpacity.Get();
		rStyle.Colors[ImGuiCol_WindowBg] = ImVec4(0.1f, 0.1f, 0.1f, fOpacity);
		rStyle.Colors[ImGuiCol_ChildBg] = ImVec4(0.1f, 0.1f, 0.1f, fOpacity);
		rStyle.Colors[ImGuiCol_PopupBg] = ImVec4(0.1f, 0.1f, 0.1f, fOpacity);
	}

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

	// Allocate game screens
	mpTweaksScreen = std::make_unique<game::TweaksScreen>();
	mpMainMenuScreen = std::make_unique<game::MainMenuScreen>();
	mpModalScreen = std::make_unique<game::ModalScreen>();
	mpPauseMenuScreen = std::make_unique<game::PauseMenuScreen>();
	mpGraphicsMenuScreen = std::make_unique<game::GraphicsMenuScreen>();
	mpSoundMenuScreen = std::make_unique<game::SoundMenuScreen>();
	mpDeathMenuScreen = std::make_unique<game::DeathMenuScreen>();
	mpHudScreen = std::make_unique<game::HudScreen>();
}

ImGuiManager::~ImGuiManager()
{
	ImGui_ImplVulkan_Shutdown();
	ImGui_ImplWin32_Shutdown();
	ImPlot::DestroyContext();
	ImGui::DestroyContext();

	vmaDestroyBuffer(gpDeviceManager->mpAllocator, mUiPrepassIndirectVkBuffer, mUiPrepassIndirectVmaAllocation);

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

void ImGuiManager::Prepare(int64_t iFramebuffer)
{
	ImGui::GetStyle().FontScaleMain = gUiFontScale.Get();

	// Apply UI opacity (opaque UI forces 1.0, otherwise use slider value)
	auto [bOpaqueUi, bPreviousOpaqueUi, bOpaqueUiChanged] = gOpaqueUi.Changed<bool>();
	auto [fUiOpacity, fPreviousUiOpacity, bUiOpacityChanged] = gUiOpacity.Changed<float>();
	if (bOpaqueUiChanged || bUiOpacityChanged)
	{
		ImGuiStyle& rStyle = ImGui::GetStyle();
		float fAlpha = bOpaqueUi ? 1.0f : fUiOpacity;
		rStyle.Colors[ImGuiCol_WindowBg].w = fAlpha;
		rStyle.Colors[ImGuiCol_ChildBg].w = fAlpha;
		rStyle.Colors[ImGuiCol_PopupBg].w = fAlpha;
	}

	ImGui_ImplVulkan_NewFrame();
	ImGui_ImplWin32_NewFrame();
	ImGui::NewFrame();

	// Menu screens required to progress past the pre-game / rejection flows must render even
	// with Tweaks open — otherwise a persisted-active Tweaks menu strands a fresh client with
	// no way to reach Connect/Spawn. Each self-gates internally on the game's UI state.
	mpMainMenuScreen->Render();
	mpModalScreen->Render();

	// Hide in-game UI when Tweaks menu is active
	if (game::gpGame->ShouldShowInGameUi())
	{
		mpHudScreen->Render();
		mpPauseMenuScreen->Render();
		mpGraphicsMenuScreen->Render();
		mpSoundMenuScreen->Render();
		mpDeathMenuScreen->Render();
	}

	mpTweaksScreen->Render();

	gpProfileManager->RenderImPlotGraphs();

	ImGui::Render();
	mpDrawData = ImGui::GetDrawData();

	UpdateUiRectBuffers(iFramebuffer);
}

void ImGuiManager::RegisterOpaqueRect(const ImVec2& pos, const ImVec2& size)
{
	if (!gOpaqueUi.Get<bool>() || miOpaqueRectCount >= kiMaxUiRects)
	{
		return;
	}

	float fWidth = static_cast<float>(gpGraphics->mFramebufferExtent2D.width);
	float fHeight = static_cast<float>(gpGraphics->mFramebufferExtent2D.height);

	// Convert pixel coords to NDC [-1, 1] (Y inverted for negative viewport height)
	float fMinX = 2.0f * pos.x / fWidth - 1.0f;
	float fMaxX = 2.0f * (pos.x + size.x) / fWidth - 1.0f;
	float fMinY = 1.0f - 2.0f * (pos.y + size.y) / fHeight;
	float fMaxY = 1.0f - 2.0f * pos.y / fHeight;

	mOpaqueRects[miOpaqueRectCount] = {fMinX, fMinY, fMaxX, fMaxY};
	++miOpaqueRectCount;
}

void ImGuiManager::UpdateUiRectBuffers(int64_t iFramebuffer)
{
	if (miOpaqueRectCount > 0)
	{
		Buffer& rStorageBuffer = gpBufferManager->mUiRectStorageBuffers.at(iFramebuffer);
		XMFLOAT4* pRects = reinterpret_cast<XMFLOAT4*>(rStorageBuffer.mpMappedMemory);
		memcpy(pRects, mOpaqueRects, static_cast<size_t>(miOpaqueRectCount) * sizeof(XMFLOAT4));
	}

	mpUiPrepassIndirectMapped[iFramebuffer] = {.vertexCount = 6, .instanceCount = static_cast<uint32_t>(miOpaqueRectCount), .firstVertex = 0, .firstInstance = 0};
	miOpaqueRectCount = 0;
}

void ImGuiManager::Submit(int64_t iFramebuffer)
{
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

	gpProfileManager->ResetQueryPools(iFramebuffer, rCommandBuffers.mImGuiVkCommandBuffer, kGpuTimerUiRender, kGpuTimerCount);
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
