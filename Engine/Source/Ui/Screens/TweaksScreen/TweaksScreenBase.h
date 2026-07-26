#pragma once

#if defined(BT_CLIENT)

namespace engine
{

// Ceiling is 31, not 32: TweakSectionFlags is backed by uint32_t and AllSectionFlags() would need 1u << 32 (UB) at 32.
inline constexpr int64_t kiMaxTweakSections = 31;
inline constexpr int64_t kiInvalidTweakSection = -1;

// Enumerator-free by design. Section identity is assigned at startup by RegisterSection, so the engine names
// no section here and nothing has to be kept in lockstep. common::Flags only requires an unsigned underlying type.
enum class TweakSectionFlags : uint32_t
{
};

// Section index (0..SectionCount()) -> its single TweakSectionFlags bit.
inline TweakSectionFlags SectionFlag(int64_t iSection)
{
	return static_cast<TweakSectionFlags>(1u << iSection);
}

class TweaksScreenBase;

struct TweakSectionDesc
{
	// Toggle-bar label and ImGui window title/ID. Register with string literals only: consumed as a C string.
	std::string_view displayName;
	// Persisted identity, folded into the layout CRC. Kept distinct from displayName so relabeling a section
	// does not discard saved layout.
	std::string_view stableKey;
	void (*pfnRender)(TweaksScreenBase& rScreen) = nullptr;
};

// Persisted section layout, engine-owned and embedded by value in the game settings struct so exactly one
// array bound and one sizeof exist in the program. Padding is explicit (uiPad) because the whole object is
// written verbatim and repeated saves of unchanged settings must be byte-identical.
struct TweakSectionState
{
	common::Flags<TweakSectionFlags> visible {};
	common::Flags<TweakSectionFlags> collapsed {};
	float fWindowPositionX[kiMaxTweakSections] {};
	float fWindowPositionY[kiMaxTweakSections] {};
	int8_t iActiveSubtab[kiMaxTweakSections] {};
	uint8_t uiPad[1] {};
	// common::Crc over the registered stableKeys in registration order. The dense indices above identify the
	// same sections only while this matches; on mismatch LoadState discards the file and defaults stand.
	common::crc_t crcLayout = 0;
};
static_assert(std::is_trivially_copyable_v<TweakSectionState>);
static_assert(sizeof(TweakSectionState) == 296, "TweaksSettings::kiVersion must be bumped with this layout");

class TweaksScreenBase
{
public:

	TweaksScreenBase();
	virtual ~TweaksScreenBase() = default;

	// Startup-only and single-threaded, from RegisterEngineTweakSections() / game::RegisterGameTweakSections()
	// before the first UI frame. Appends rDesc, assigns the next dense index, and writes it back through
	// riSection. The table is immutable afterward, so render-time reads need no synchronization.
	static void RegisterSection(int64_t& riSection, const TweakSectionDesc& rDesc);
	static int64_t SectionCount();
	static const TweakSectionDesc& GetSection(int64_t iSection);
	static common::Flags<TweakSectionFlags> AllSectionFlags();

	void SaveState(TweakSectionState& rState) const;
	void LoadState(const TweakSectionState& rState);

	void Render();

	void RenderToggleBar();
	void RenderSectionWindow(int64_t iSection);

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
	void RenderSmokeSection();
	virtual void RenderSmokeDepositsTab() {}
	void RenderWindSection();
	virtual void RenderWindDepositsTab() {}

	void RenderWaveCountRadioButtons(Wrapper& rCountWrapper);
	bool BeginSubtab(const char* pcLabel, int64_t iSection, int8_t iTab);
	void WrapperSlider(std::string_view label, int64_t iSection, float fWidthMultiplier = 2.0f, std::string_view mapKey = {});
	void WrapperSeparatorText(std::string_view label);
	// Chevron-style discrete index selector: << [name] >> with wrap-around. iCount must equal the wrapper's allowed-value count.
	void ChevronIndexSelector(std::string_view label, Wrapper& rWrapper, const std::string_view* pNames, int64_t iCount);

	void RunSliderAuditFrame();

	std::string_view mActiveSlider;
	int64_t miActiveSliderSection = -1; // -1 for toggle bar, 0+ for sections

	common::Flags<TweakSectionFlags> mSectionVisible {};
	ImVec2 mWindowPositions[kiMaxTweakSections] {};
	int8_t mActiveSubtab[kiMaxTweakSections] {};
	common::Flags<TweakSectionFlags> mApplySubtab {};
	common::Flags<TweakSectionFlags> mSectionCollapsed {};
	float mfToggleBarBottom = 0.0f;

	// Slider-map drift audit runs once per TweaksScreen lifetime and is re-armed by graphics reconstruction. Cycles mActiveSubtab[] across 6 frames: frames 0-4 queue tabs and frame 5 settles the final selection.
	// miAuditFrame: 0..(kiAuditFrameCount-1) = audit running, -1 = audit complete.
	std::unordered_set<std::string_view> mAuditTouched;
	std::unordered_set<std::string_view> mAuditMissed;
	int8_t mPreAuditSubtab[kiMaxTweakSections] {};
	int8_t miAuditFrame = 0;
	bool mbAuditMode = false;
};

// Startup-assigned dense section indices, written by RegisterEngineTweakSections(). kiInvalidTweakSection until then.
inline int64_t giTweakSectionPbr = kiInvalidTweakSection;
inline int64_t giTweakSectionTerrain = kiInvalidTweakSection;
inline int64_t giTweakSectionWater = kiInvalidTweakSection;
inline int64_t giTweakSectionLighting = kiInvalidTweakSection;
inline int64_t giTweakSectionShadow = kiInvalidTweakSection;
inline int64_t giTweakSectionSunMoon = kiInvalidTweakSection;
inline int64_t giTweakSectionMisc = kiInvalidTweakSection;
inline int64_t giTweakSectionSound = kiInvalidTweakSection;
inline int64_t giTweakSectionSmoke = kiInvalidTweakSection;
inline int64_t giTweakSectionWind = kiInvalidTweakSection;

// Registers the engine's sections. Called from Main.cpp immediately before the Graphics ctor (which builds
// ImGuiManager -> TweaksScreen) and therefore before game::LoadTweaksSettings(). Deliberately not in the
// TweaksScreenBase ctor: device loss recreates Graphics in place and would double-register.
void RegisterEngineTweakSections();

} // namespace engine

#endif // BT_CLIENT
