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

constexpr float kfGraphicsMinWidthFraction = 0.5f;
constexpr float kfGraphicsMaxWidthFraction = 0.9f;
constexpr float kfGraphicsFontScaleAtMinimum = 2.0f;
constexpr float kfGraphicsFontScaleAtMaximum = 1.2f;
constexpr float kfGraphicsHeadingScale = 1.15f;
constexpr float kfGraphicsColumnGutterPixels = 80.0f;

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
	float fFontScaleRange = engine::gUiFontScale.GetMax() - engine::gUiFontScale.GetMin();
	float fFontScalePosition = (engine::gUiFontScale.Get() - engine::gUiFontScale.GetMin()) / fFontScaleRange;
	float fPanelWidthFraction = std::lerp(kfGraphicsMinWidthFraction, kfGraphicsMaxWidthFraction, fFontScalePosition);
	// Graphics is the densest player-facing menu. Preserve the user's monotonic Font Size adjustment while
	// compressing its local base scale toward the high end, and retain the theme's base geometry instead of
	// doubling padding and spacing a second time.
	float fMenuFontScale = std::lerp(kfGraphicsFontScaleAtMinimum, kfGraphicsFontScaleAtMaximum, fFontScalePosition);

	ImGui::SetNextWindowPos(ImVec2(rIo.DisplaySize.x * 0.5f, rIo.DisplaySize.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
	ImGui::SetNextWindowSize(ImVec2(rIo.DisplaySize.x * fPanelWidthFraction, 0.0f));
	// Window auto-resizes to its content (content can exceed a 4K screen); cap the height so the whole panel stays on
	// screen.
	ImGui::SetNextWindowSizeConstraints(ImVec2(0.0f, 0.0f), ImVec2(rIo.DisplaySize.x, rIo.DisplaySize.y * kfGraphicsMaxHeightFraction));

	// Always transparent regardless of Opaque UI, so the FPS readout below reflects worst-case cost
	ImVec4 f4WindowBg = ImGui::GetStyle().Colors[ImGuiCol_WindowBg];
	f4WindowBg.w = engine::gUiOpacity.Get();
	ImGui::PushStyleColor(ImGuiCol_WindowBg, f4WindowBg);

	ScopedMenuFont menuFont(fMenuFontScale);
	ImGui::Begin("GraphicsMenu", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize);

	// Border + accent strip only — the themed WindowBg already fills the panel
	ImVec2 vPanelPos = ImGui::GetWindowPos();
	ImVec2 vPanelSize = ImGui::GetWindowSize();
	DrawPanelAccents(ImGui::GetWindowDrawList(), vPanelPos, ImVec2(vPanelPos.x + vPanelSize.x, vPanelPos.y + vPanelSize.y));

	bool bBackPressed = false;
	if (ImGui::BeginTable("GraphicsHeader", 3, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoPadOuterX | ImGuiTableFlags_NoSavedSettings))
	{
		ImGui::TableSetupColumn("GraphicsTitle", ImGuiTableColumnFlags_WidthStretch, 3.0f);
		ImGui::TableSetupColumn("GraphicsFps", ImGuiTableColumnFlags_WidthStretch, 1.0f);
		ImGui::TableSetupColumn("GraphicsBack", ImGuiTableColumnFlags_WidthStretch, 1.0f);
		ImGui::TableNextRow();

		ImGui::TableNextColumn();
		float fHeaderHeight = 0.0f;
		{
			ScopedMenuFont headingFont(fMenuFontScale * kfGraphicsHeadingScale);
			fHeaderHeight = ImGui::GetTextLineHeight();
			ImGui::TextUnformatted(AppendUtf8(common::gpThreadLocal->mWorkbuffer, TranslatedString(kStringGraphics)));
		}

		ImGui::TableNextColumn();
		ImGui::SetCursorPosY(ImGui::GetCursorPosY() + std::max(0.0f, (fHeaderHeight - ImGui::GetTextLineHeight()) * 0.5f));
		ImGui::Text("FPS: %lld", engine::gpGraphics->mRendersInTheLastSecond.Get());

		ImGui::TableNextColumn();
		float fBackWidth = MenuButtonsWidth({U"Back"});
		ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - fBackWidth);
		ImGui::SetCursorPosY(ImGui::GetCursorPosY() + std::max(0.0f, (fHeaderHeight - ImGui::GetFrameHeight()) * 0.5f));
		bBackPressed = MenuButton("Back", ImVec2(fBackWidth, 0.0f), mfBackHoverAnim);

		ImGui::EndTable();
	}

	ImGui::Separator();

	const ImGuiStyle& rStyle = ImGui::GetStyle();
	ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(kfGraphicsColumnGutterPixels * engine::UiScale() * 0.5f, rStyle.CellPadding.y));
	if (ImGui::BeginTable("GraphicsColumns", 2, ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_NoPadOuterX))
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

		ImGui::Separator();

		WrapperToggle("Anisotropy", &engine::gAnisotropy);
		if (engine::gAnisotropy.Get<bool>())
		{
			ColumnSlider("Max Anisotropy", &engine::gMaxAnisotropy);
		}
		ColumnSlider("Mip Lod Bias", &engine::gMipLodBias);

		// Right column: Effects & UI
		ImGui::TableNextColumn();

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
		// Unconditional: this screen's own background alpha always comes from the slider, even with Opaque UI on
		ColumnSlider("UI Opacity", &engine::gUiOpacity);

		WrapperPlusMinus("Font Size", &engine::gUiFontScale, 0.1f);

		RadioRow("Theme", &engine::gUiTheme, static_cast<float>(engine::GetUiTheme()),
			{{"Naval Steel", static_cast<float>(engine::UiTheme::kNavalSteel)}, {"Dark Amber", static_cast<float>(engine::UiTheme::kDarkAmber)}, {"Midnight Mauve", static_cast<float>(engine::UiTheme::kMidnightMauve)}});

		ImGui::EndTable();
	}
	ImGui::PopStyleVar();

	if (bBackPressed)
	{
		SaveGraphicsSettings();
		gpGame->meUiState = UiState::kPause;
	}

	ImGui::End();
	ImGui::PopStyleColor();
}

} // namespace game

#endif // BT_CLIENT
