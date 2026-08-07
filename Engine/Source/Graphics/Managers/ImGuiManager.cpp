#if defined(BT_CLIENT)

#include "ImGuiManager.h"

#include "Ui/GraphicsSettingsWrappersBase.h"
#include "Ui/Screens/DeathMenuScreen.h"
#include "Ui/Screens/GameSettingsScreen.h"
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

namespace
{

// Base hues per UiTheme; ApplyThemeColors derives the full ImGuiStyle::Colors[] set from these (designer-pass placeholders)
struct ThemePalette
{
	ImVec4 f4Text;
	ImVec4 f4TextDisabled;
	ImVec4 f4Bg;
	ImVec4 f4BgElevated;
	ImVec4 f4Accent;
	ImVec4 f4AccentHover;
	ImVec4 f4AccentActive;
	ImVec4 f4Border;
};

constexpr ThemePalette kThemePalettes[]
{
	// kNavalSteel: near-black blue-grey, steel borders, cyan/teal accent
	{
		.f4Text = ImVec4(0.86f, 0.91f, 0.94f, 1.0f),
		.f4TextDisabled = ImVec4(0.45f, 0.52f, 0.58f, 1.0f),
		.f4Bg = ImVec4(0.07f, 0.09f, 0.11f, 1.0f),
		.f4BgElevated = ImVec4(0.12f, 0.16f, 0.20f, 1.0f),
		.f4Accent = ImVec4(0.15f, 0.75f, 0.85f, 1.0f),
		.f4AccentHover = ImVec4(0.25f, 0.85f, 0.95f, 1.0f),
		.f4AccentActive = ImVec4(0.10f, 0.60f, 0.70f, 1.0f),
		.f4Border = ImVec4(0.25f, 0.33f, 0.40f, 1.0f),
	},
	// kDarkAmber: charcoal, warm amber accent
	{
		.f4Text = ImVec4(0.92f, 0.89f, 0.84f, 1.0f),
		.f4TextDisabled = ImVec4(0.55f, 0.51f, 0.45f, 1.0f),
		.f4Bg = ImVec4(0.09f, 0.09f, 0.09f, 1.0f),
		.f4BgElevated = ImVec4(0.15f, 0.14f, 0.13f, 1.0f),
		.f4Accent = ImVec4(0.95f, 0.65f, 0.15f, 1.0f),
		.f4AccentHover = ImVec4(1.0f, 0.75f, 0.25f, 1.0f),
		.f4AccentActive = ImVec4(0.80f, 0.52f, 0.10f, 1.0f),
		.f4Border = ImVec4(0.38f, 0.33f, 0.26f, 1.0f),
	},
	// kMidnightMauve: near-black indigo base, lavender/mauve accent (Catppuccin Mocha) — designer-pass placeholders
	{
		.f4Text = ImVec4(0.80f, 0.84f, 0.96f, 1.0f),
		.f4TextDisabled = ImVec4(0.50f, 0.52f, 0.61f, 1.0f),
		.f4Bg = ImVec4(0.12f, 0.12f, 0.18f, 1.0f),
		.f4BgElevated = ImVec4(0.19f, 0.20f, 0.27f, 1.0f),
		.f4Accent = ImVec4(0.80f, 0.65f, 0.97f, 1.0f),
		.f4AccentHover = ImVec4(0.85f, 0.73f, 1.0f, 1.0f),
		.f4AccentActive = ImVec4(0.68f, 0.52f, 0.86f, 1.0f),
		.f4Border = ImVec4(0.27f, 0.28f, 0.35f, 1.0f),
	},
};
static_assert(std::size(kThemePalettes) == static_cast<size_t>(UiTheme::kCount));

ImVec4 WithAlpha(const ImVec4& rf4Color, float fAlpha)
{
	return ImVec4(rf4Color.x, rf4Color.y, rf4Color.z, fAlpha);
}

} // namespace

ImGuiManager::ImGuiManager(HWND hwnd)
{
	ASSERT(gpImGuiManager == nullptr);

	gpImGuiManager = this;

	CreateRenderPass();
	CreateFramebuffers();
	CreateUiPrepassIndirectBuffer();

	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImPlot::CreateContext();

	// Enable the vendored imgui's test-engine ItemAdd/ItemInfo hooks so engine::AgentUiRegistry captures widget
	// rects/labels each frame. Only worth the per-item hook cost when the agent layer is live; defaults false otherwise.
	if (gpAgentCommandServer != nullptr)
	{
		ImGui::GetCurrentContext()->TestEngineHookItems = true;
	}
	ImGuiIO& rIo = ImGui::GetIO();
	rIo.IniFilename = nullptr;
	rIo.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
	rIo.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;

	// Load one default font for Latin and CJK text, with oversampling for crisp rendering
	const EagerChunk& rFontChunk = gpFileManager->GetEagerChunkMap().at(data::kRawNotoSansSCLightotfCrc);
	ImFontConfig fontConfig;
	fontConfig.OversampleH = 2;
	fontConfig.OversampleV = 1;
	fontConfig.FontDataOwnedByAtlas = false;
	// Trust boundary: on-disk ChunkHeader::iSize drives the TTF byte length ImGui reads from pData; bound it to the eager chunk's true extent before the copy (reject non-positive too — a negative int64 passes the upper bound and reaches stb_truetype as a negative int).
	if (rFontChunk.pHeader->iSize <= 0 || rFontChunk.pHeader->iSize > rFontChunk.iDataSize)
	{
		throw common::CorruptStreamException("ImGuiManager font");
	}
	ImGui::GetIO().Fonts->AddFontFromMemoryTTF(rFontChunk.pData, static_cast<int>(rFontChunk.pHeader->iSize), 26.0f, &fontConfig);

	bool bWin32Init = ImGui_ImplWin32_Init(hwnd);
	ASSERT(bWin32Init);

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
	// ImGui_ImplVulkan_Init unconditionally returns true (failures trip internal IM_ASSERTs), so its result is not worth checking.
	ImGui_ImplVulkan_Init(&initInfo);

	// Minimized/zero-height: keep the 1.0f default scale rather than collapsing style to 0
	const float fHeight = static_cast<float>(gpGraphics->mFramebufferExtent2D.height);
	if (fHeight > 0.0f)
	{
		mfUiScale = fHeight / kfUiReferenceHeight;
	}
	SetupThemeGeometry(mfUiScale);
	ImGui::GetStyle().FontScaleDpi = mfUiScale;
	ApplyThemeColors(GetUiTheme());

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
	mpGameSettingsScreen = std::make_unique<game::GameSettingsScreen>();
	mpDeathMenuScreen = std::make_unique<game::DeathMenuScreen>();
	mpHudScreen = std::make_unique<game::HudScreen>();
}

// Re-applyable: the whole style is reset to ImGui defaults (colors preserved) before the explicit overrides and
// ScaleAllSizes below, so this may run repeatedly on resolution change without cumulative drift — no per-field
// exhaustive-list maintenance needed. ScaleAllSizes touches ~35 fields (incl. internal _MainScale); resetting only
// the ~14 fields set here would let the rest (IndentSpacing, CellPadding, WindowMinSize, ...) compound on re-run.
void ImGuiManager::SetupThemeGeometry(float fUiScale)
{
	ImGuiStyle& rStyle = ImGui::GetStyle();

	// Reset geometry to ImGui defaults while preserving colors (colors are owned by ApplyThemeColors / the opacity
	// path, which touch only rStyle.Colors). Default ImGuiStyle ctor is heap-free, so the stack struct is safe in
	// the allocation-tracked main loop. This resets FontScaleMain/FontScaleDpi to 1.0f, so callers re-set them after:
	// FontScaleDpi after both call sites (ctor + Prepare); FontScaleMain only after the Prepare block, so the ctor
	// leaves it at 1.0f and relies on the first Prepare to apply gUiFontScale.
	// FontSizeBase resets to 0.0f and self-heals (re-derived from the default font's LegacySize on next font update).
	ImGuiStyle defaultStyle;
	std::copy(std::begin(rStyle.Colors), std::end(rStyle.Colors), std::span(defaultStyle.Colors).begin());
	rStyle = defaultStyle;

	// WindowRounding stays small: RegisterOpaqueRect occlusion rects are rectangular, so with gOpaqueUi on, large rounding
	// would occlude the 3D scene behind the rounded-off corners (4.0f base -> 8px at 4K after the 2x scale below)
	rStyle.WindowRounding = 4.0f;
	rStyle.ChildRounding = 3.0f;
	rStyle.FrameRounding = 3.0f;
	rStyle.PopupRounding = 3.0f;
	rStyle.GrabRounding = 3.0f;
	rStyle.TabRounding = 3.0f;
	rStyle.WindowBorderSize = 1.0f;
	rStyle.FrameBorderSize = 1.0f;
	rStyle.WindowPadding = ImVec2(10.0f, 10.0f);
	rStyle.FramePadding = ImVec2(6.0f, 4.0f);
	rStyle.ItemSpacing = ImVec2(8.0f, 6.0f);
	rStyle.ItemInnerSpacing = ImVec2(6.0f, 4.0f);
	rStyle.ScrollbarSize = 14.0f;
	rStyle.GrabMinSize = 12.0f;

	// Scale UI element sizes to 2x at the 4K reference, times the resolution scale
	rStyle.ScaleAllSizes(2.0f * fUiScale);
}

void ImGuiManager::ApplyThemeColors(UiTheme eTheme)
{
	const ThemePalette& rPalette = kThemePalettes[static_cast<size_t>(eTheme)];
	ImVec4* pColors = ImGui::GetStyle().Colors;

	// Window backgrounds carry the user-controlled opacity; Prepare() rewrites only their .w on opacity changes
	float fAlpha = gOpaqueUi.Get<bool>() ? 1.0f : gUiOpacity.Get();
	pColors[ImGuiCol_WindowBg] = WithAlpha(rPalette.f4Bg, fAlpha);
	pColors[ImGuiCol_ChildBg] = WithAlpha(rPalette.f4Bg, fAlpha);
	pColors[ImGuiCol_PopupBg] = WithAlpha(rPalette.f4Bg, fAlpha);

	pColors[ImGuiCol_Text] = rPalette.f4Text;
	pColors[ImGuiCol_TextDisabled] = rPalette.f4TextDisabled;
	pColors[ImGuiCol_Border] = WithAlpha(rPalette.f4Border, 0.6f);
	pColors[ImGuiCol_FrameBg] = WithAlpha(rPalette.f4BgElevated, 0.8f);
	pColors[ImGuiCol_FrameBgHovered] = WithAlpha(rPalette.f4Accent, 0.25f);
	pColors[ImGuiCol_FrameBgActive] = WithAlpha(rPalette.f4Accent, 0.4f);
	pColors[ImGuiCol_TitleBg] = rPalette.f4Bg;
	pColors[ImGuiCol_TitleBgActive] = rPalette.f4BgElevated;
	pColors[ImGuiCol_TitleBgCollapsed] = WithAlpha(rPalette.f4Bg, 0.6f);
	pColors[ImGuiCol_MenuBarBg] = rPalette.f4BgElevated;
	pColors[ImGuiCol_ScrollbarBg] = WithAlpha(rPalette.f4Bg, 0.6f);
	pColors[ImGuiCol_ScrollbarGrab] = rPalette.f4BgElevated;
	pColors[ImGuiCol_ScrollbarGrabHovered] = WithAlpha(rPalette.f4Accent, 0.6f);
	pColors[ImGuiCol_ScrollbarGrabActive] = rPalette.f4AccentActive;
	pColors[ImGuiCol_CheckMark] = rPalette.f4Accent;
	pColors[ImGuiCol_SliderGrab] = WithAlpha(rPalette.f4Accent, 0.8f);
	pColors[ImGuiCol_SliderGrabActive] = rPalette.f4AccentActive;
	pColors[ImGuiCol_Button] = WithAlpha(rPalette.f4BgElevated, 0.9f);
	pColors[ImGuiCol_ButtonHovered] = WithAlpha(rPalette.f4AccentHover, 0.4f);
	pColors[ImGuiCol_ButtonActive] = WithAlpha(rPalette.f4AccentActive, 0.6f);
	pColors[ImGuiCol_Header] = WithAlpha(rPalette.f4Accent, 0.25f);
	pColors[ImGuiCol_HeaderHovered] = WithAlpha(rPalette.f4Accent, 0.35f);
	pColors[ImGuiCol_HeaderActive] = WithAlpha(rPalette.f4Accent, 0.45f);
	pColors[ImGuiCol_Separator] = WithAlpha(rPalette.f4Border, 0.6f);
	pColors[ImGuiCol_SeparatorHovered] = WithAlpha(rPalette.f4Accent, 0.6f);
	pColors[ImGuiCol_SeparatorActive] = rPalette.f4Accent;
	pColors[ImGuiCol_ResizeGrip] = WithAlpha(rPalette.f4Accent, 0.2f);
	pColors[ImGuiCol_ResizeGripHovered] = WithAlpha(rPalette.f4Accent, 0.5f);
	pColors[ImGuiCol_ResizeGripActive] = rPalette.f4Accent;
	pColors[ImGuiCol_Tab] = rPalette.f4Bg;
	pColors[ImGuiCol_TabHovered] = WithAlpha(rPalette.f4Accent, 0.4f);
	pColors[ImGuiCol_TabSelected] = rPalette.f4BgElevated;
	pColors[ImGuiCol_TabSelectedOverline] = rPalette.f4Accent;
	pColors[ImGuiCol_TabDimmed] = WithAlpha(rPalette.f4Bg, 0.8f);
	pColors[ImGuiCol_TabDimmedSelected] = WithAlpha(rPalette.f4BgElevated, 0.8f);
	pColors[ImGuiCol_PlotLines] = rPalette.f4Accent;
	pColors[ImGuiCol_PlotLinesHovered] = rPalette.f4AccentHover;
	pColors[ImGuiCol_PlotHistogram] = rPalette.f4Accent;
	pColors[ImGuiCol_PlotHistogramHovered] = rPalette.f4AccentHover;
	pColors[ImGuiCol_TextSelectedBg] = WithAlpha(rPalette.f4Accent, 0.35f);
	pColors[ImGuiCol_NavCursor] = rPalette.f4Accent;

	pColors[ImGuiCol_TextLink] = rPalette.f4Accent;
	pColors[ImGuiCol_InputTextCursor] = rPalette.f4Text;
	pColors[ImGuiCol_TreeLines] = WithAlpha(rPalette.f4Border, 0.6f);
	pColors[ImGuiCol_BorderShadow] = ImVec4(0.0f, 0.0f, 0.0f, 0.3f);
	pColors[ImGuiCol_UnsavedMarker] = rPalette.f4Accent;
	pColors[ImGuiCol_DragDropTarget] = rPalette.f4Accent;
	pColors[ImGuiCol_DragDropTargetBg] = WithAlpha(rPalette.f4Accent, 0.25f);
	pColors[ImGuiCol_TableHeaderBg] = rPalette.f4BgElevated;
	pColors[ImGuiCol_TableBorderStrong] = WithAlpha(rPalette.f4Border, 0.6f);
	pColors[ImGuiCol_TableBorderLight] = WithAlpha(rPalette.f4Border, 0.35f);
	pColors[ImGuiCol_TableRowBg] = WithAlpha(rPalette.f4Bg, 0.0f);
	pColors[ImGuiCol_TableRowBgAlt] = WithAlpha(rPalette.f4BgElevated, 0.35f);
	pColors[ImGuiCol_NavWindowingHighlight] = WithAlpha(rPalette.f4Accent, 0.7f);
	pColors[ImGuiCol_NavWindowingDimBg] = WithAlpha(rPalette.f4Bg, 0.2f);
	pColors[ImGuiCol_ModalWindowDimBg] = WithAlpha(rPalette.f4Bg, 0.35f);
	pColors[ImGuiCol_TabDimmedSelectedOverline] = WithAlpha(rPalette.f4Accent, 0.5f);
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

	if (gpImGuiManager == this)
	{
		gpImGuiManager = nullptr;
	}
}

// Create host-visible indirect draw buffer for UI depth pre-pass
void ImGuiManager::CreateUiPrepassIndirectBuffer()
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
	for (int64_t i = 0; i < iFramebufferCount; ++i)
	{
		mpUiPrepassIndirectMapped[i] = {.vertexCount = 6, .instanceCount = 0, .firstVertex = 0, .firstInstance = 0};
	}
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
	// Changed() advances the single-consumer change tracking; apply re-reads via GetUiTheme() for the trust-boundary clamp
	if (std::get<2>(gUiTheme.Changed<UiTheme>()))
	{
		ApplyThemeColors(GetUiTheme());
	}

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

	// Recompute resolution scale before NewFrame (io.DisplaySize is stale until ImGui_ImplWin32_NewFrame below).
	// SetupThemeGeometry is re-applyable, but only re-run it when the scale actually changed. The live resize path
	// recreates ImGuiManager on a fresh ImGui context (an extent change escalates the kSwapchain destroy tier, which
	// rebuilds this manager), so this per-frame guard is defense-in-depth for any future path that changes the
	// extent without recreation.
	const float fPreviousUiScale = mfUiScale;
	// Minimized/zero-height: keep previous scale rather than collapsing style to 0
	const float fHeight = static_cast<float>(gpGraphics->mFramebufferExtent2D.height);
	if (fHeight > 0.0f)
	{
		mfUiScale = fHeight / kfUiReferenceHeight;
	}
	if (mfUiScale != fPreviousUiScale)
	{
		SetupThemeGeometry(mfUiScale);
	}
	// Both font scale factors set unconditionally after the geometry block: SetupThemeGeometry's whole-style reset
	// (rStyle = defaultStyle) clears FontScaleMain to 1.0f, so re-applying here restores the user's Font Size on a
	// scale-change frame rather than dropping it for that frame.
	ImGui::GetStyle().FontScaleMain = gUiFontScale.Get();
	ImGui::GetStyle().FontScaleDpi = mfUiScale;

	ImGui_ImplVulkan_NewFrame();
	ImGui_ImplWin32_NewFrame();
	// The Win32 backend re-queues the physical cursor pos in NewFrame above; re-issue the harness's synthetic pin after
	// it so the injected pos wins last-writer-wins in ImGui::NewFrame (agent harness only; pin-valid gate lives inside).
	if (gpAgentInput != nullptr)
	{
		gpAgentInput->ReissueImGuiMousePos();
	}

	// Suppressed harness client: neutralize the two NewFrame physical polls the Win32 backend just ran. This runs after
	// the synthetic re-pin above so an active pin still wins last-writer-wins.
	if (PhysicalInputSuppressed())
	{
		ImGuiIO& rIo = ImGui::GetIO();

		// (d) Physical cursor poll: unless a synthetic pin owns io.MousePos, park it at ImGui's no-mouse sentinel.
		// Missing agent input must also suppress the physical cursor rather than dereference a nullable startup global.
		if (gpAgentInput == nullptr || !gpAgentInput->ImGuiMousePosPinned())
		{
			rIo.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
		}

		// (e) ImGui gamepad-nav poll (XInputGetState): clear the whole ImGuiKey_Gamepad* range. The harness injects no
		// gamepad ImGui events, so there is no conflict.
		for (int64_t iKey = ImGuiKey_GamepadStart; iKey <= ImGuiKey_GamepadRStickDown; ++iKey)
		{
			rIo.AddKeyEvent(static_cast<ImGuiKey>(iKey), false);
		}
	}

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
		mpGameSettingsScreen->Render();
		mpDeathMenuScreen->Render();
	}

	mpTweaksScreen->Render();

	gpProfileManager->RenderImPlotGraphs();
	RenderTextAreas();

	ImGui::Render();
	mpDrawData = ImGui::GetDrawData();

	// Publish this completed frame's widget/window snapshot as the agent registry's read table (double-buffered).
	if (gpAgentUiRegistry != nullptr)
	{
		gpAgentUiRegistry->Swap();
	}

	UpdateUiRectBuffers(iFramebuffer);
}

void ImGuiManager::UpdateTextArea(TextAreas eTextArea, std::string_view characters)
{
	ASSERT(common::gpMultithreading->IsMainThread());
	ASSERT(characters.size() < TextArea::kiMaxChars);

	TextArea& rTextArea = mTextAreas[eTextArea];
	rTextArea.iCharacterCount = static_cast<int64_t>(characters.size());
	std::memcpy(rTextArea.pcText, characters.data(), rTextArea.iCharacterCount);
}

void ImGuiManager::RenderTextAreas()
{
	const ImVec2 displaySize = ImGui::GetIO().DisplaySize;
	ImDrawList* pDrawList = ImGui::GetBackgroundDrawList();
	ImFont* pFont = ImGui::GetFont();

	for (const TextArea& rTextArea : mTextAreas)
	{
		if (rTextArea.iCharacterCount == 0)
		{
			continue;
		}

		const char* pcTextEnd = rTextArea.pcText + rTextArea.iCharacterCount;
		const ImVec2 pos(rTextArea.fX * displaySize.x, rTextArea.fY * displaySize.y);
		const float fFontSize = 0.25f * rTextArea.fSize * displaySize.y;
		const ImVec2 shadowPos(pos.x + 0.00075f * displaySize.x, pos.y + 0.00175f * displaySize.y);

		// Heap: ImGui may grow internal draw-list vertex/index buffers for first-use or worst-case profile text.
		ScopedSuppressAllocationTracking suppress;
		pDrawList->AddText(pFont, fFontSize, shadowPos, IM_COL32_BLACK, rTextArea.pcText, pcTextEnd);
		pDrawList->AddText(pFont, fFontSize, pos, IM_COL32_WHITE, rTextArea.pcText, pcTextEnd);
	}
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
		std::memcpy(pRects, mOpaqueRects, static_cast<size_t>(miOpaqueRectCount) * sizeof(XMFLOAT4));
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

#endif // defined(BT_CLIENT)
