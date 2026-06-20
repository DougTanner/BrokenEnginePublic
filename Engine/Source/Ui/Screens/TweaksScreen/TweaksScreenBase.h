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
	kSound,
	kHexShield,
	kSmoke,
	kWind,
	kParticles,
	kCount
};

// Bitmask sibling of TweakSection for the persisted section-state flag sets (mSectionVisible / mApplySubtab / mSectionCollapsed).
// TweakSection itself stays a 0..kCount index (array sizing + RenderSectionWindow dispatch), so a separate 1<<n mirror is needed for common::Flags.
// Keep in lockstep with TweakSection when adding a section.
enum class TweakSectionFlags : uint32_t
{
	kModel     = 1u << 0,
	kTerrain   = 1u << 1,
	kWater     = 1u << 2,
	kLighting  = 1u << 3,
	kShadow    = 1u << 4,
	kSunMoon   = 1u << 5,
	kMisc      = 1u << 6,
	kSound     = 1u << 7,
	kHexShield = 1u << 8,
	kSmoke     = 1u << 9,
	kWind      = 1u << 10,
	kParticles = 1u << 11,
};
static_assert(static_cast<int>(TweakSection::kCount) <= 32, "TweakSectionFlags backing is uint32_t");

// Section index (0..kCount) -> its single TweakSectionFlags bit.
inline TweakSectionFlags SectionFlag(int64_t iSection)
{
	return static_cast<TweakSectionFlags>(1u << iSection);
}

// All sections set — replaces the std::fill(begin, end, true) over the old bool[kCount] arrays.
inline constexpr common::Flags<TweakSectionFlags> kAllSectionFlags = static_cast<TweakSectionFlags>((1u << static_cast<int>(TweakSection::kCount)) - 1u);

class TweaksScreenBase
{
public:

	TweaksScreenBase();
	virtual ~TweaksScreenBase() = default;

	void SaveState(common::Flags<TweakSectionFlags>& rSectionVisible, ImVec2* pWindowPositions, int8_t* pActiveSubtab, common::Flags<TweakSectionFlags>& rSectionCollapsed) const;
	void LoadState(common::Flags<TweakSectionFlags> sectionVisible, const ImVec2* pWindowPositions, const int8_t* pActiveSubtab, common::Flags<TweakSectionFlags> sectionCollapsed);

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
	void RenderSoundSection();
	virtual void RenderSoundEffects() {}
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

	void RunSliderAuditFrame();

	std::string_view mActiveSlider;
	int64_t miActiveSliderSection = -1; // -1 for toggle bar, 0+ for sections

	common::Flags<TweakSectionFlags> mSectionVisible {};
	ImVec2 mWindowPositions[static_cast<size_t>(TweakSection::kCount)];
	int8_t mActiveSubtab[static_cast<size_t>(TweakSection::kCount)] {};
	common::Flags<TweakSectionFlags> mApplySubtab {};
	common::Flags<TweakSectionFlags> mSectionCollapsed {};
	float mfToggleBarBottom = 0.0f;

	// One-shot first-open slider-map drift audit. Cycles mActiveSubtab[] across 5 frames so multi-tab sections (Lighting=5, Water=4, Wind/Smoke=2) are fully exercised.
	// miAuditFrame: 0..(kiAuditFrameCount-1) = audit running, -1 = audit complete.
	std::unordered_set<std::string_view> mAuditTouched;
	std::unordered_set<std::string_view> mAuditMissed;
	int8_t mPreAuditSubtab[static_cast<size_t>(TweakSection::kCount)] {};
	int8_t miAuditFrame = 0;
	bool mbAuditMode = false;
};

} // namespace engine

#endif // BT_CLIENT
