#include "GraphicsMenuScreen.h"

#if defined(BT_CLIENT)

#include "Game.h"
#include "MenuUtils.h"
#include "Ui/GraphicsSettingsWrappersBase.h"
#include "Ui/Localization.h"
#include "Ui/SunMoonWrappersBase.h"

namespace game
{

namespace
{

// Float-backed RadioButton row: optional header text, then one RadioButton per option on a single line. The
// checked button is the option whose value equals the wrapper's current value; clicking one writes that value.
// One place for the enum<->index mapping of present mode, sample count, water detail, and theme — every Wrapper
// flavor is float-backed, so equality/Set on the raw float is exact for the discrete values used here.
// RadioButton and header labels are the harness automation API — do not rename.
void RadioRow(const char* pcHeader, engine::Wrapper* pWrapper, float fCurrent, std::initializer_list<std::pair<const char*, float>> aOptions)
{
	if (pcHeader != nullptr)
	{
		ImGui::TextUnformatted(pcHeader);
	}

	bool bFirst = true;
	for (const std::pair<const char*, float>& rOption : aOptions)
	{
		if (!bFirst)
		{
			// Wrap to a new line when the next radio would clip at the column edge (long labels like "Midnight Mauve")
			ImGui::SameLine();
			float fOptionWidth = ImGui::GetFrameHeight() + ImGui::GetStyle().ItemInnerSpacing.x + ImGui::CalcTextSize(rOption.first).x;
			if (ImGui::GetContentRegionAvail().x < fOptionWidth)
			{
				ImGui::NewLine();
			}
		}
		bFirst = false;

		if (ImGui::RadioButton(rOption.first, fCurrent == rOption.second))
		{
			pWrapper->Set(rOption.second);
		}
	}
}

// WrapperSlider whose bar is shortened to leave room for its trailing label inside the current table column, so a
// label like "Minimum Ambient" is no longer clipped at the column edge. Negative item width means "fill the column
// minus this many pixels from the right" (ImGui CalcItemWidth), floored at 1px by ImGui.
void ColumnSlider(const char* pcLabel, engine::Wrapper* pWrapper)
{
	ImGui::SetNextItemWidth(-(ImGui::CalcTextSize(pcLabel).x + ImGui::GetStyle().ItemInnerSpacing.x));
	WrapperSlider(pcLabel, pWrapper);
}

} // namespace

void GraphicsMenuScreen::Render()
{
	if (gpGame->meUiState != UiState::kGraphicsSettings)
	{
		return;
	}

	ImGuiIO& rIo = ImGui::GetIO();
	ScopedMenuScale menuScale;

	ImGui::SetNextWindowPos(ImVec2(rIo.DisplaySize.x * 0.5f, rIo.DisplaySize.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
	ImGui::SetNextWindowSize(ImVec2(rIo.DisplaySize.x * kfSettingsPanelWidthFraction, 0.0f));
	// Window auto-resizes to its content (content can exceed a 4K screen); cap the height so the whole panel plus the
	// Back button stays on screen.
	ImGui::SetNextWindowSizeConstraints(ImVec2(0.0f, 0.0f), ImVec2(rIo.DisplaySize.x, rIo.DisplaySize.y * kfGraphicsMaxHeightFraction));

	ScopedMenuFont menuFont;
	ImGui::Begin("GraphicsMenu", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize);

	engine::gpImGuiManager->RegisterOpaqueRect(ImGui::GetWindowPos(), ImGui::GetWindowSize());

	// Border + accent strip only — the opaque themed WindowBg must stay intact for RegisterOpaqueRect occlusion
	ImVec2 vPanelPos = ImGui::GetWindowPos();
	ImVec2 vPanelSize = ImGui::GetWindowSize();
	DrawPanelAccents(ImGui::GetWindowDrawList(), vPanelPos, ImVec2(vPanelPos.x + vPanelSize.x, vPanelPos.y + vPanelSize.y));

	MenuHeading(AppendUtf8(common::gpThreadLocal->mWorkbuffer, TranslatedString(kStringGraphics)));

	// FPS display
	ImGui::Text("FPS: %lld", engine::gpGraphics->mRendersInTheLastSecond.Get());

	ImGui::Separator();

	if (ImGui::BeginTable("GraphicsColumns", 2, ImGuiTableFlags_SizingStretchSame))
	{
		// Left column: Display
		ImGui::TableNextColumn();

		// Time of day (only in main menu)
		if (gpGame->InMainMenu())
		{
			ColumnSlider("Time of Day", &engine::gSunAngleOverride);
		}

		ColumnSlider("Minimum Ambient", &engine::gSunMoonMinimumAmbient);

		ImGui::Separator();

		WrapperToggle("Fullscreen", &engine::gFullscreen);

		RadioRow("Presentation Mode", &engine::gPresentMode, engine::gPresentMode.Get(),
			{{"Immediate", static_cast<float>(VK_PRESENT_MODE_IMMEDIATE_KHR)}, {"Mailbox", static_cast<float>(VK_PRESENT_MODE_MAILBOX_KHR)}, {"FIFO", static_cast<float>(VK_PRESENT_MODE_FIFO_KHR)}});

		ImGui::Separator();

		WrapperToggle("Multisampling", &engine::gMultisampling);
		if (engine::gMultisampling.Get<bool>())
		{
			RadioRow(nullptr, &engine::gSampleCount, engine::gSampleCount.Get(),
				{{"2x", static_cast<float>(VK_SAMPLE_COUNT_2_BIT)}, {"4x", static_cast<float>(VK_SAMPLE_COUNT_4_BIT)}, {"8x", static_cast<float>(VK_SAMPLE_COUNT_8_BIT)}, {"16x", static_cast<float>(VK_SAMPLE_COUNT_16_BIT)}});
		}

		ImGui::Separator();

		RadioRow("Water Shape Detail", &engine::gWaterShapeDetail, engine::gWaterShapeDetail.Get(),
			{{"1/4", 0.25f}, {"1/2", 0.5f}});

		// Right column: Effects & UI
		ImGui::TableNextColumn();

		WrapperToggle("Anisotropy", &engine::gAnisotropy);
		if (engine::gAnisotropy.Get<bool>())
		{
			ColumnSlider("Max Anisotropy", &engine::gMaxAnisotropy);
		}
		ColumnSlider("Mip Lod Bias", &engine::gMipLodBias);

		ImGui::Separator();

		WrapperToggle("Sample Shading", &engine::gSampleShading);
		if (engine::gSampleShading.Get<bool>())
		{
			ColumnSlider("Min Sample Shading", &engine::gMinSampleShading);
		}

		ImGui::Separator();

		WrapperToggle("Smoke", &engine::gSmokeEnabled);
		if (engine::gSmokeEnabled.Get<bool>())
		{
			ColumnSlider("Smoke Pixels", &engine::gSmokeSimulationPixels);
			ColumnSlider("Smoke Area", &engine::gSmokeSimulationArea);
		}

		WrapperToggle("Wind", &engine::gWindEnabled);

		ImGui::Separator();

		WrapperToggle("Opaque UI", &engine::gOpaqueUi);
		if (!engine::gOpaqueUi.Get<bool>())
		{
			ColumnSlider("UI Opacity", &engine::gUiOpacity);
		}

		WrapperPlusMinus("Font Size", &engine::gUiFontScale, 0.1f);

		RadioRow("Theme", &engine::gUiTheme, static_cast<float>(engine::GetUiTheme()),
			{{"Naval Steel", static_cast<float>(engine::UiTheme::kNavalSteel)}, {"Dark Amber", static_cast<float>(engine::UiTheme::kDarkAmber)}, {"Midnight Mauve", static_cast<float>(engine::UiTheme::kMidnightMauve)}});

		ImGui::EndTable();
	}

	// Back button
	ImGui::Separator();
	float fBackWidth = MenuButtonsWidth({U"Back"});
	if (MenuButton("Back", ImVec2(fBackWidth, 0.0f), mfBackHoverAnim))
	{
		SaveGraphicsSettings();
		gpGame->meUiState = UiState::kPause;
	}

	ImGui::End();
}

} // namespace game

#endif // BT_CLIENT
