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

	void SaveState(bool* pSectionVisible, ImVec2* pWindowPositions, int8_t* pActiveSubtab) const;
	void LoadState(const bool* pSectionVisible, const ImVec2* pWindowPositions, const int8_t* pActiveSubtab);

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
	void RenderShadowSection();
	void RenderMiscSection();
	virtual void RenderHexShieldSection() = 0;
	void RenderSmokeSection();
	void RenderWindSection();
	virtual void RenderWindDepositsSection() = 0;

	void RenderWaveCountRadioButtons(Wrapper& rCountWrapper);
	void WrapperSlider(std::string_view label, int64_t iSection, float fWidthMultiplier = 2.0f, std::string_view mapKey = {});
	void WrapperSeparatorText(std::string_view label);

	std::string_view mActiveSlider;
	int64_t miActiveSliderSection = -1; // -1 for toggle bar, 0+ for sections

	bool mSectionVisible[static_cast<size_t>(TweakSection::kCount)] {};
	ImVec2 mWindowPositions[static_cast<size_t>(TweakSection::kCount)];
	int8_t mActiveSubtab[static_cast<size_t>(TweakSection::kCount)] {};
	bool mApplySubtab[static_cast<size_t>(TweakSection::kCount)] {};
	float mfToggleBarBottom = 0.0f;
};

} // namespace engine
