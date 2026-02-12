#pragma once

namespace engine
{

enum class TweakSection : int
{
	kTest = 0,
	kModel,
	kTerrain,
	kWaterSpecular,
	kWaterLow,
	kWaterMedium,
	kLighting,
	kWaterLighting,
	kShadow,
	kMisc,
	kHexShield,
	kSmoke,
	kWind,
	kCount
};

class TweaksScreen
{
public:

	TweaksScreen();
	~TweaksScreen() = default;

	void Render();

	void RenderToggleBar();
	void RenderSectionWindow(TweakSection eSection);

	void RenderTestSection();
	void RenderPbrSection();
	void RenderTerrainSection();
	void RenderWaterSpecularSection();
	void RenderWaterLowSection();
	void RenderWaterMediumSection();
	void RenderLightingSection();
	void RenderWaterLightingSection();
	void RenderShadowSection();
	void RenderMiscSection();
	void RenderHexShieldSection();
	void RenderSmokeSection();
	void RenderWindSection();

	void WrapperSlider(std::string_view label, int iSection, float fWidthMultiplier = 2.0f);
	void WrapperSeparatorText(std::string_view label);

private:

	std::string_view mActiveSlider;
	int miActiveSliderSection = -1; // -1 for toggle bar, 0+ for sections

	bool mpSectionVisible[static_cast<size_t>(TweakSection::kCount)] {};
	ImVec2 mpWindowPositions[static_cast<size_t>(TweakSection::kCount)];
	float mfToggleBarBottom = 0.0f;
};

} // namespace engine
