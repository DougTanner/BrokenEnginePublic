#include "GraphicsMenuScreen.h"

#if defined(BT_CLIENT)

#include "Game.h"
#include "MenuUtils.h"
#include "Ui/GraphicsSettingsWrappersBase.h"
#include "Ui/SunMoonWrappersBase.h"

namespace game
{

void GraphicsMenuScreen::Render()
{
	if (gpGame->meUiState != UiState::kGraphicsSettings)
	{
		return;
	}

	ImGuiIO& rIo = ImGui::GetIO();
	ScopedMenuScale menuScale;

	ImGui::SetNextWindowPos(ImVec2(rIo.DisplaySize.x * 0.5f, rIo.DisplaySize.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
	ImGui::SetNextWindowSize(ImVec2(rIo.DisplaySize.x * 0.6f, 0.0f));

	ImGui::Begin("GraphicsMenu", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize);
	ImGui::SetWindowFontScale(kfMenuUiScale);

	engine::gpImGuiManager->RegisterOpaqueRect(ImGui::GetWindowPos(), ImGui::GetWindowSize());

	// FPS display
	ImGui::Text("FPS: %lld", engine::gpGraphics->mRendersInTheLastSecond.Get());

	ImGui::Separator();

	ImGui::Columns(2, "GraphicsColumns", false);

	// Left column: Display & Quality
	// Time of day (only in main menu)
	if (gpGame->InMainMenu())
	{
		WrapperSlider("Time of Day", &engine::gSunAngleOverride);
	}

	WrapperSlider("Minimum Ambient", &engine::gSunMoonMinimumAmbient);

	ImGui::Separator();

	WrapperToggle("Fullscreen", &engine::gFullscreen);

	ImGui::Text("Presentation Mode");
	int64_t iPresentMode = 0;
	VkPresentModeKHR ePresentMode = engine::gPresentMode.Get<VkPresentModeKHR>();
	if (ePresentMode == VK_PRESENT_MODE_IMMEDIATE_KHR) iPresentMode = 0;
	else if (ePresentMode == VK_PRESENT_MODE_MAILBOX_KHR) iPresentMode = 1;
	else if (ePresentMode == VK_PRESENT_MODE_FIFO_KHR) iPresentMode = 2;

	if (ImGui::RadioButton("Immediate", iPresentMode == 0)) engine::gPresentMode.Set<VkPresentModeKHR>(VK_PRESENT_MODE_IMMEDIATE_KHR);
	ImGui::SameLine();
	if (ImGui::RadioButton("Mailbox", iPresentMode == 1)) engine::gPresentMode.Set<VkPresentModeKHR>(VK_PRESENT_MODE_MAILBOX_KHR);
	ImGui::SameLine();
	if (ImGui::RadioButton("FIFO", iPresentMode == 2)) engine::gPresentMode.Set<VkPresentModeKHR>(VK_PRESENT_MODE_FIFO_KHR);

	ImGui::Separator();

	WrapperToggle("Multisampling", &engine::gMultisampling);
	if (engine::gMultisampling.Get<bool>())
	{
		int64_t iSampleCount = 0;
		VkSampleCountFlagBits eSampleCount = engine::gSampleCount.Get<VkSampleCountFlagBits>();
		if (eSampleCount == VK_SAMPLE_COUNT_2_BIT) iSampleCount = 0;
		else if (eSampleCount == VK_SAMPLE_COUNT_4_BIT) iSampleCount = 1;
		else if (eSampleCount == VK_SAMPLE_COUNT_8_BIT) iSampleCount = 2;
		else if (eSampleCount == VK_SAMPLE_COUNT_16_BIT) iSampleCount = 3;

		if (ImGui::RadioButton("2x", iSampleCount == 0)) engine::gSampleCount.Set<VkSampleCountFlagBits>(VK_SAMPLE_COUNT_2_BIT);
		ImGui::SameLine();
		if (ImGui::RadioButton("4x", iSampleCount == 1)) engine::gSampleCount.Set<VkSampleCountFlagBits>(VK_SAMPLE_COUNT_4_BIT);
		ImGui::SameLine();
		if (ImGui::RadioButton("8x", iSampleCount == 2)) engine::gSampleCount.Set<VkSampleCountFlagBits>(VK_SAMPLE_COUNT_8_BIT);
		ImGui::SameLine();
		if (ImGui::RadioButton("16x", iSampleCount == 3)) engine::gSampleCount.Set<VkSampleCountFlagBits>(VK_SAMPLE_COUNT_16_BIT);
	}

	// Right column: Effects & Misc
	ImGui::NextColumn();

	WrapperToggle("Anisotropy", &engine::gAnisotropy);
	if (engine::gAnisotropy.Get<bool>())
	{
		WrapperSlider("Max Anisotropy", &engine::gMaxAnisotropy);
	}
	WrapperSlider("Mip Lod Bias", &engine::gMipLodBias);

	ImGui::Separator();

	WrapperToggle("Sample Shading", &engine::gSampleShading);
	if (engine::gSampleShading.Get<bool>())
	{
		WrapperSlider("Min Sample Shading", &engine::gMinSampleShading);
	}

	ImGui::Separator();

	ImGui::Text("World Detail");
	int64_t iWorldDetail = 0;
	float fWorldDetail = engine::gWorldDetail.Get();
	if (fWorldDetail == 0.0625f) iWorldDetail = 0;
	else if (fWorldDetail == 0.125f) iWorldDetail = 1;
	else if (fWorldDetail == 0.25f) iWorldDetail = 2;

	if (ImGui::RadioButton("1/16", iWorldDetail == 0)) engine::gWorldDetail.Set(0.0625f);
	ImGui::SameLine();
	if (ImGui::RadioButton("1/8", iWorldDetail == 1)) engine::gWorldDetail.Set(0.125f);
	ImGui::SameLine();
	if (ImGui::RadioButton("1/4", iWorldDetail == 2)) engine::gWorldDetail.Set(0.25f);

	ImGui::Separator();

	WrapperToggle("Smoke", &engine::gSmokeEnabled);
	if (engine::gSmokeEnabled.Get<bool>())
	{
		WrapperSlider("Smoke Pixels", &engine::gSmokeSimulationPixels);
		WrapperSlider("Smoke Area", &engine::gSmokeSimulationArea);
	}

	WrapperToggle("Wind", &engine::gWindEnabled);

	ImGui::Separator();

	WrapperToggle("Opaque UI", &engine::gOpaqueUi);
	if (!engine::gOpaqueUi.Get<bool>())
	{
		WrapperSlider("UI Opacity", &engine::gUiOpacity);
	}

	WrapperPlusMinus("Font Size", &engine::gUiFontScale, 0.1f);

	ImGui::Columns(1);

	// Back button
	ImGui::Separator();
	if (ImGui::Button("Back"))
	{
		Game::SaveGraphicsSettings();
		gpGame->meUiState = UiState::kPause;
	}

	ImGui::End();
}

} // namespace game

#endif // BT_CLIENT
