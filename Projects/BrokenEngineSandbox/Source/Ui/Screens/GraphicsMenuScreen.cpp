#include "GraphicsMenuScreen.h"

#if defined(BT_CLIENT)

#include "Game.h"
#include "MenuUtils.h"
#include "Ui/GraphicsQualityWrappers.h"
#include "Ui/GraphicsSettingsWrappersBase.h"
#include "Ui/LightingWrappersBase.h"
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

// WrapperSlider with its label drawn to the left of the bar instead of trailing it, the bar filling the rest of the
// table column (-FLT_MIN item width). ImGui only ever draws a widget's own label after the frame, so the visible text
// is emitted separately and the slider carries a hidden-label id ("##" prefix, display portion empty). That id is what
// the agent UI snapshot records, so describe_ui reports "##Minimum Ambient" and a harness query for the human-readable
// name resolves through AgentUiRegistry::ResolveLabel's case-insensitive substring tier.
void ColumnSlider(const char* pcLabel, engine::Wrapper* pWrapper)
{
	ImGui::AlignTextToFramePadding();
	ImGui::TextUnformatted(pcLabel);
	ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);

	char pcSliderId[64];
	std::snprintf(pcSliderId, sizeof(pcSliderId), "##%s", pcLabel);
	ImGui::SetNextItemWidth(-FLT_MIN);
	WrapperSlider(pcSliderId, pWrapper);
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

		WrapperToggle("Sample Shading", &engine::gSampleShading);
		if (engine::gSampleShading.Get<bool>())
		{
			ColumnSlider("Min Sample Shading", &engine::gMinSampleShading);
		}

		ImGui::Separator();

		WrapperToggle("Anisotropy", &engine::gAnisotropy);
		if (engine::gAnisotropy.Get<bool>())
		{
			ColumnSlider("Max Anisotropy", &engine::gMaxAnisotropy);
		}
		ColumnSlider("Mip Lod Bias", &engine::gMipLodBias);

		// Right column: Effects & UI
		ImGui::TableNextColumn();

		if (RadioRow("Water", &gWaterLevel, gWaterLevel.Get(), {{"Low", 0.0f}, {"Medium", 1.0f}, {"High", 2.0f}}))
		{
			ApplyWaterLevel();
		}

		ImGui::Separator();

		if (RadioRow("Terrain Shadows", &gTerrainShadowsLevel, gTerrainShadowsLevel.Get(), {{"Low", 0.0f}, {"Medium", 1.0f}, {"High", 2.0f}}))
		{
			ApplyTerrainShadowsLevel();
		}

		ImGui::Separator();

		if (RadioRow("Object Shadows", &gObjectShadowsLevel, gObjectShadowsLevel.Get(), {{"Low", 0.0f}, {"Medium", 1.0f}, {"High", 2.0f}}))
		{
			ApplyObjectShadowsLevel();
		}

		ImGui::Separator();

		if (RadioRow("Lighting", &gLightingLevel, gLightingLevel.Get(), {{"Low", 0.0f}, {"Medium", 1.0f}, {"High", 2.0f}}))
		{
			ApplyLightingLevel();
		}

		ImGui::Separator();

		WrapperToggle("Smoke", &engine::gSmokeEnabled);
		if (engine::gSmokeEnabled.Get<bool>())
		{
			if (RadioRow("Smoke Detail", &gSmokeDetailLevel, gSmokeDetailLevel.Get(), {{"Low", 0.0f}, {"Medium", 1.0f}, {"High", 2.0f}}))
			{
				ApplySmokeDetailLevel();
			}

			ColumnSlider("Smoke Area", &engine::gSmokeSimulationArea);
		}

		ImGui::Separator();

		WrapperToggle("Wind", &engine::gWindEnabled);
		ColumnSlider("Lighting Update Cadence", &engine::gLightingUpdateCadence);

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
