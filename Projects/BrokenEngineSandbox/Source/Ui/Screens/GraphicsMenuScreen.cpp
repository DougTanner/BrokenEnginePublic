#include "GraphicsMenuScreen.h"

#if defined(BT_CLIENT)

#include "Game.h"
#include "MenuUtils.h"

namespace game
{

void GraphicsMenuScreen::Render()
{
	if (gpGame->meUiState != UiState::kGraphics)
	{
		return;
	}

	ImGuiIO& rIo = ImGui::GetIO();
	ScopedMenuScale menuScale;

	// Semi-transparent background
	ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.1f, 0.1f, 0.1f, 0.9f));

	ImGui::SetNextWindowPos(ImVec2(rIo.DisplaySize.x * 0.05f, rIo.DisplaySize.y * 0.04f), ImGuiCond_Always);

	ImGui::Begin("GraphicsMenu", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize);
	ImGui::SetWindowFontScale(kfMenuUiScale);

	// FPS display
	ImGui::Text("FPS: %lld", engine::gpGraphics->mRendersInTheLastSecond.Get());

	ImGui::Separator();

	// Time of day (only in main menu)
	if (gpGame->InMainMenu())
	{
		WrapperSlider("Time of Day", &engine::gSunAngleOverride);
	}

	WrapperSlider("Minimum Ambient", &engine::gMinimumAmbient);

	ImGui::Separator();

	WrapperToggle("Fullscreen", &engine::gFullscreen);

	ImGui::Text("Presentation Mode");
	int iPresentMode = 0;
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
		int iSampleCount = 0;
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

	ImGui::Separator();

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
	int iWorldDetail = 0;
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

	WrapperToggle("Smoke", &engine::gSmoke);
	if (engine::gSmoke.Get<bool>())
	{
		WrapperSlider("Smoke Pixels", &engine::gSmokeSimulationPixels);
		WrapperSlider("Smoke Area", &engine::gSmokeSimulationArea);
	}

	WrapperToggle("Wind", &engine::gWind);

	// Back button
	ImGui::Separator();
	if (ImGui::Button("Back"))
	{
		gpGame->meUiState = UiState::kPause;
	}

	ImGui::End();

	ImGui::PopStyleColor();
}

} // namespace game

#endif // BT_CLIENT
