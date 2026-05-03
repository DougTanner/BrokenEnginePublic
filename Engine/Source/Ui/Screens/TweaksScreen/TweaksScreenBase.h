#pragma once

#if defined(BT_CLIENT)

namespace engine
{

enum class TweakSection : int
{
	kModel = 0,
	kTerrain,
	kWater,
	kLighting,
	kShadow,
	kSunMoon,
	kMisc,
	kHexShield,
	kSmoke,
	kWind,
	kParticles,
	kCount
};

class TweaksScreenBase
{
public:

	TweaksScreenBase();
	virtual ~TweaksScreenBase() = default;

	void SaveState(bool* pSectionVisible, ImVec2* pWindowPositions, int8_t* pActiveSubtab, bool* pSectionCollapsed) const;
	void LoadState(const bool* pSectionVisible, const ImVec2* pWindowPositions, const int8_t* pActiveSubtab, const bool* pSectionCollapsed);

	void Render();

	void RenderToggleBar();
	void RenderSectionWindow(TweakSection eSection);

	void RenderPbrSection();
	void RenderTerrainSection();
	void RenderWaterSection();
	void RenderLightingSection();
	virtual void RenderLightingEffectsVisibleTab() {}
	virtual void RenderLightingEffectsLightingTab() {}
	void RenderShadowSection();
	void RenderSunMoonSection();
	void RenderMiscSection();
	virtual void RenderHexShieldSection() = 0;
	void RenderSmokeSection();
	virtual void RenderSmokeDepositsTab() {}
	void RenderWindSection();
	virtual void RenderWindDepositsTab() {}
	virtual void RenderParticlesSection() = 0;

	void RenderWaveCountRadioButtons(Wrapper& rCountWrapper);
	void WrapperSlider(std::string_view label, int64_t iSection, float fWidthMultiplier = 2.0f, std::string_view mapKey = {});
	void WrapperSeparatorText(std::string_view label);
	// Chevron-style discrete index selector: << [name] >> with wrap-around. iCount must equal the wrapper's allowed-value count.
	void ChevronIndexSelector(std::string_view label, Wrapper& rWrapper, const std::string_view* pNames, int64_t iCount);

	std::string_view mActiveSlider;
	int64_t miActiveSliderSection = -1; // -1 for toggle bar, 0+ for sections

	bool mSectionVisible[static_cast<size_t>(TweakSection::kCount)] {};
	ImVec2 mWindowPositions[static_cast<size_t>(TweakSection::kCount)];
	int8_t mActiveSubtab[static_cast<size_t>(TweakSection::kCount)] {};
	bool mApplySubtab[static_cast<size_t>(TweakSection::kCount)] {};
	bool mSectionCollapsed[static_cast<size_t>(TweakSection::kCount)] {};
	float mfToggleBarBottom = 0.0f;
};

} // namespace engine

#endif // BT_CLIENT
