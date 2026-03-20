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
	kWindDeposits,
	kCount
};

class TweaksScreenBase
{
public:

	TweaksScreenBase();
	virtual ~TweaksScreenBase() = default;

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
	virtual void RenderHexShieldSection() = 0;
	void RenderSmokeSection();
	void RenderWindSection();
	virtual void RenderWindDepositsSection() = 0;

	void RenderWaveCountRadioButtons(Wrapper& rCountWrapper);
	void WrapperSlider(std::string_view label, int64_t iSection, float fWidthMultiplier = 2.0f);
	void WrapperSeparatorText(std::string_view label);

protected:

	std::string_view mActiveSlider;
	int64_t miActiveSliderSection = -1; // -1 for toggle bar, 0+ for sections

	bool mSectionVisible[static_cast<size_t>(TweakSection::kCount)] {};
	ImVec2 mWindowPositions[static_cast<size_t>(TweakSection::kCount)];
	float mfToggleBarBottom = 0.0f;
};

} // namespace engine
