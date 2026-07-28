#include "TweaksScreenBase.h"

#if defined(BT_CLIENT)

#include "TweaksSliderMap.h"
#include "Ui/GraphicsSettingsWrappersBase.h"

namespace engine
{

// Registry storage. Startup-only writes, immutable during rendering, and fixed-size so registration never allocates.
static TweakSectionDesc sSectionDescs[kiMaxTweakSections] {};
static int64_t siSectionCount = 0;

// Order-sensitive fold of the registered stable keys: the per-key CRCs are hashed as one byte run, so a
// reordering, a rename, or a count change all produce a different value.
static common::crc_t ComputeLayoutCrc()
{
	common::crc_t crcKeys[kiMaxTweakSections] {};
	for (int64_t i = 0; i < siSectionCount; ++i)
	{
		crcKeys[i] = common::Crc(sSectionDescs[i].stableKey);
	}
	return common::Crc(crcKeys, siSectionCount);
}

// UI scale factor for TweaksScreen
static constexpr float kfUiScale = 1.5f;

void TweaksScreenBase::RegisterSection(int64_t& riSection, const TweakSectionDesc& rDesc)
{
	ASSERT(riSection == kiInvalidTweakSection);
	ASSERT(siSectionCount < kiMaxTweakSections);
	riSection = siSectionCount;
	sSectionDescs[siSectionCount] = rDesc;
	++siSectionCount;
}

int64_t TweaksScreenBase::SectionCount()
{
	return siSectionCount;
}

const TweakSectionDesc& TweaksScreenBase::GetSection(int64_t iSection)
{
	return sSectionDescs[iSection];
}

common::Flags<TweakSectionFlags> TweaksScreenBase::AllSectionFlags()
{
	return static_cast<TweakSectionFlags>((1u << siSectionCount) - 1u);
}

void RegisterEngineTweakSections()
{
	TweaksScreenBase::RegisterSection(giTweakSectionPbr, {.displayName = "Pbr", .stableKey = "Pbr",
		.pfnRender = [](TweaksScreenBase& rScreen) { rScreen.RenderPbrSection(); }});
	TweaksScreenBase::RegisterSection(giTweakSectionTerrain, {.displayName = "Terrain", .stableKey = "Terrain",
		.pfnRender = [](TweaksScreenBase& rScreen) { rScreen.RenderTerrainSection(); }});
	TweaksScreenBase::RegisterSection(giTweakSectionWater, {.displayName = "Water", .stableKey = "Water",
		.pfnRender = [](TweaksScreenBase& rScreen) { rScreen.RenderWaterSection(); }});
	TweaksScreenBase::RegisterSection(giTweakSectionLighting, {.displayName = "Lighting", .stableKey = "Lighting",
		.pfnRender = [](TweaksScreenBase& rScreen) { rScreen.RenderLightingSection(); }});
	TweaksScreenBase::RegisterSection(giTweakSectionShadow, {.displayName = "Shadow", .stableKey = "Shadow",
		.pfnRender = [](TweaksScreenBase& rScreen) { rScreen.RenderShadowSection(); }});
	TweaksScreenBase::RegisterSection(giTweakSectionSunMoon, {.displayName = "Sun/Moon", .stableKey = "SunMoon",
		.pfnRender = [](TweaksScreenBase& rScreen) { rScreen.RenderSunMoonSection(); }});
	TweaksScreenBase::RegisterSection(giTweakSectionMisc, {.displayName = "Misc", .stableKey = "Misc",
		.pfnRender = [](TweaksScreenBase& rScreen) { rScreen.RenderMiscSection(); }});
	TweaksScreenBase::RegisterSection(giTweakSectionSound, {.displayName = "Sound", .stableKey = "Sound",
		.pfnRender = [](TweaksScreenBase& rScreen) { rScreen.RenderSoundSection(); }});
	TweaksScreenBase::RegisterSection(giTweakSectionSmoke, {.displayName = "Smoke", .stableKey = "Smoke",
		.pfnRender = [](TweaksScreenBase& rScreen) { rScreen.RenderSmokeSection(); }});
	TweaksScreenBase::RegisterSection(giTweakSectionWind, {.displayName = "Wind", .stableKey = "Wind",
		.pfnRender = [](TweaksScreenBase& rScreen) { rScreen.RenderWindSection(); }});
}

TweaksScreenBase::TweaksScreenBase()
{
	// Every section must already be registered; SectionCount() below is the final count. Registration runs
	// from Main.cpp rather than here because device loss recreates Graphics, and so this object, in place -
	// registering here would double-register.

	// Initialize staggered window positions (Y set to 0, will use mfToggleBarBottom at runtime)
	constexpr float kfStartX = 10.0f;
	constexpr float kfOffsetX = 30.0f;
	const float fUiScale = UiScale();

	for (int64_t i = 0; i < SectionCount(); ++i)
	{
		mWindowPositions[i] = ImVec2((kfStartX + static_cast<float>(i) * kfOffsetX) * fUiScale, 0.0f);
	}
}

void TweaksScreenBase::RenderWaveCountRadioButtons(Wrapper& rCountWrapper)
{
	if (mActiveSlider.empty())
	{
		ImGui::Text("Wave Count");
		ImGui::SameLine();
		int64_t iCurrent = rCountWrapper.Get<int64_t>();
		for (const std::pair<const char*, int64_t>& rPair : std::initializer_list<std::pair<const char*, int64_t>> {{"15", 15}, {"31", 31}, {"63", 63}, {"127", 127}, {"255", 255}})
		{
			if (ImGui::RadioButton(rPair.first, iCurrent == rPair.second))
			{
				rCountWrapper.Set(rPair.second);
			}
			ImGui::SameLine();
		}
		ImGui::NewLine();
	}
}

void TweaksScreenBase::ChevronIndexSelector(std::string_view label, Wrapper& rWrapper, const std::string_view* pNames, int64_t iCount)
{
	// Match WrapperSlider's alpha-fade-while-dragging behavior so this widget keeps its layout slot when another slider is active.
	const bool bAnotherSliderActive = !mActiveSlider.empty();
	if (bAnotherSliderActive)
	{
		ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.0f);
	}

	int64_t iIndex = std::clamp(rWrapper.GetIndex(), int64_t {0}, iCount - 1);

	// Render << and >> adjacent first, then the label — keeps button positions fixed when the displayed name changes width.
	char pId[128];
	std::snprintf(pId, sizeof(pId), "<<##%.*s_prev", static_cast<int>(label.size()), label.data());
	if (ImGui::Button(pId) && !bAnotherSliderActive)
	{
		rWrapper.SetIndex((iIndex + iCount - 1) % iCount);
	}
	ImGui::SameLine();
	std::snprintf(pId, sizeof(pId), ">>##%.*s_next", static_cast<int>(label.size()), label.data());
	if (ImGui::Button(pId) && !bAnotherSliderActive)
	{
		rWrapper.SetIndex((iIndex + 1) % iCount);
	}
	ImGui::SameLine();
	const std::string_view name = pNames[iIndex];
	ImGui::Text("%.*s [%lld/%lld] %.*s", static_cast<int>(label.size()), label.data(), iIndex + 1, iCount, static_cast<int>(name.size()), name.data());

	if (bAnotherSliderActive)
	{
		ImGui::PopStyleVar();
	}
}

bool TweaksScreenBase::BeginSubtab(const char* pcLabel, int64_t iSection, int8_t iTab)
{
	const bool bApplySavedSubtab = (mApplySubtab & SectionFlag(iSection)) && mActiveSubtab[iSection] == iTab;
	if (!ImGui::BeginTabItem(pcLabel, nullptr, bApplySavedSubtab ? ImGuiTabItemFlags_SetSelected : 0))
	{
		return false;
	}

	if (bApplySavedSubtab)
	{
		mApplySubtab.Clear(SectionFlag(iSection));
	}
	if (!(mApplySubtab & SectionFlag(iSection)))
	{
		mActiveSubtab[iSection] = iTab;
	}
	return true;
}

void TweaksScreenBase::WrapperSlider(std::string_view label, int64_t iSection, float fWidthMultiplier, std::string_view mapKey)
{
	if (mapKey.empty())
	{
		mapKey = label;
	}

	if constexpr (kbDebugInput)
	{
		if (mbAuditMode)
		{
			// Heap: STL hash buckets allocate. Audit runs once per TweaksScreen lifetime and is re-armed by graphics reconstruction; suppression mirrors TweaksSliderMap::Get().
			ScopedSuppressAllocationTracking suppress;
			std::unordered_map<std::string_view, Wrapper*>& rSliderMap = TweaksSliderMap::Get();
			if (rSliderMap.contains(mapKey))
			{
				mAuditTouched.insert(mapKey);
			}
			else
			{
				mAuditMissed.insert(mapKey);
			}
			return;
		}
	}

	std::unordered_map<std::string_view, Wrapper*>& rSliderMap = TweaksSliderMap::Get();
	auto it = rSliderMap.find(mapKey);
	if (it == rSliderMap.end())
	{
		return;
	}

	// Render non-active sliders with alpha=0 to preserve layout
	bool bIsActiveSlider = (mActiveSlider.empty() || mapKey == mActiveSlider);
	if (!bIsActiveSlider)
	{
		ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.0f);
	}

	// Scale section slider width by the caller-provided multiplier
	if (iSection >= 0)
	{
		ImGui::SetNextItemWidth(ImGui::CalcItemWidth() * fWidthMultiplier);
	}

	Wrapper* pWrapper = it->second;
	float fValue = pWrapper->Get();

	// When mapKey differs from label, append ##mapKey for unique ImGui ID
	char pImGuiLabel[128] {};
	if (mapKey != label)
	{
		std::snprintf(pImGuiLabel, sizeof(pImGuiLabel), "%.*s##%.*s", static_cast<int>(label.size()), label.data(), static_cast<int>(mapKey.size()), mapKey.data());
	}
	else
	{
		std::snprintf(pImGuiLabel, sizeof(pImGuiLabel), "%.*s", static_cast<int>(label.size()), label.data());
	}

	if (ImGui::SliderFloat(pImGuiLabel, &fValue, pWrapper->GetMin(), pWrapper->GetMax(), "%.6f"))
	{
		pWrapper->Set(fValue);
	}
	if (ImGui::IsItemActive())
	{
		mActiveSlider = mapKey;
		miActiveSliderSection = iSection;
	}

	if (!bIsActiveSlider)
	{
		ImGui::PopStyleVar();
	}
}

void TweaksScreenBase::Render()
{
	if constexpr (kbDebugInput)
	{
		ImGuiIO& rIo = ImGui::GetIO();

		// Clear active slider when mouse released
		if (!rIo.MouseDown[0])
		{
			mActiveSlider = {};
		}

		// Scale UI elements
		ImGuiStyle& rStyle = ImGui::GetStyle();
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(rStyle.FramePadding.x * kfUiScale, rStyle.FramePadding.y * kfUiScale));
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(rStyle.ItemSpacing.x * kfUiScale, rStyle.ItemSpacing.y * kfUiScale));
		ImGui::PushStyleVar(ImGuiStyleVar_ItemInnerSpacing, ImVec2(rStyle.ItemInnerSpacing.x * kfUiScale, rStyle.ItemInnerSpacing.y * kfUiScale));
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(rStyle.WindowPadding.x * kfUiScale, rStyle.WindowPadding.y * kfUiScale));

		if (miAuditFrame >= 0)
		{
			RunSliderAuditFrame();
		}

		RenderToggleBar();

		// Render visible section windows (only the one with active slider when dragging)
		for (int64_t i = 0; i < SectionCount(); ++i)
		{
			if (!mActiveSlider.empty())
			{
				if (i == miActiveSliderSection)
				{
					RenderSectionWindow(i);
				}
			}
			else if (mSectionVisible & SectionFlag(i))
			{
				RenderSectionWindow(i);
			}
		}

		ImGui::PopStyleVar(4);
	}
}

void TweaksScreenBase::RenderToggleBar()
{
	// Capture state at start (mActiveSlider can change during WrapperSlider)
	bool bSliderActive = !mActiveSlider.empty();

	ImGuiIO& rIo = ImGui::GetIO();

	// Make window invisible (no background, border, or title)
	ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
	ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));

	// Full-width window at top of screen
	ImGui::SetNextWindowPos(ImVec2(0.0f, 10.0f * UiScale()), ImGuiCond_Always);
	ImGui::SetNextWindowSize(ImVec2(rIo.DisplaySize.x, 0.0f));
	ImGui::Begin("Tweaks", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize);
	ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * kfUiScale);

	// Calculate content width (full screen minus window padding)
	float fContentWidth = rIo.DisplaySize.x - ImGui::GetStyle().WindowPadding.x * 2.0f;

	// Calculate button width to fill available space
	float fButtonWidth = (fContentWidth - ImGui::GetStyle().ItemSpacing.x * (SectionCount() - 1)) / SectionCount();

	// Render toggle buttons with alpha=0 when slider is active to preserve layout
	if (bSliderActive)
	{
		ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.0f);
	}

	// Center text within buttons
	ImGui::PushStyleVar(ImGuiStyleVar_SelectableTextAlign, ImVec2(0.5f, 0.5f));
	for (int64_t i = 0; i < SectionCount(); ++i)
	{
		if (i > 0)
		{
			ImGui::SameLine();
		}
		if (ImGui::Selectable(GetSection(i).displayName.data(), mSectionVisible & SectionFlag(i), 0, ImVec2(fButtonWidth, 0.0f)))
		{
			mSectionVisible.Toggle(SectionFlag(i));
		}
	}
	ImGui::PopStyleVar();

	if (bSliderActive)
	{
		ImGui::PopStyleVar();
	}

	// Sun angle slider spans full content width (no label), double height
	ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(ImGui::GetStyle().FramePadding.x, ImGui::GetStyle().FramePadding.y * 2.0f));
	ImGui::SetNextItemWidth(fContentWidth);
	float fSunAngle = gSunAngleOverride.Get();
	if (ImGui::SliderFloat("##Sun Angle", &fSunAngle, gSunAngleOverride.GetMin(), gSunAngleOverride.GetMax(), "%.6f"))
	{
		gSunAngleOverride.Set(fSunAngle);
	}
	if (ImGui::IsItemActive())
	{
		mActiveSlider = "##Sun Angle";
		miActiveSliderSection = -1;
	}
	ImGui::PopStyleVar();

	mfToggleBarBottom = ImGui::GetWindowPos().y + ImGui::GetWindowSize().y;

	ImGui::PopFont();
	ImGui::End();

	ImGui::PopStyleColor(2);
}

void TweaksScreenBase::WrapperSeparatorText(std::string_view label)
{
	if (!mActiveSlider.empty())
	{
		ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.0f);
	}
	ImGui::SeparatorText(label.data());
	if (!mActiveSlider.empty())
	{
		ImGui::PopStyleVar();
	}
}

void TweaksScreenBase::SaveState(TweakSectionState& rState) const
{
	// Clear first so unregistered slots and uiPad are written as zeros: two saves of unchanged settings must be byte-identical.
	rState = {};

	rState.visible = mSectionVisible;
	rState.collapsed = mSectionCollapsed;
	for (int64_t i = 0; i < kiMaxTweakSections; ++i)
	{
		rState.fWindowPositionX[i] = mWindowPositions[i].x;
		rState.fWindowPositionY[i] = mWindowPositions[i].y;
	}
	std::memcpy(rState.iActiveSubtab, mActiveSubtab, sizeof(mActiveSubtab));
	rState.crcLayout = ComputeLayoutCrc();
}

void TweaksScreenBase::LoadState(const TweakSectionState& rState)
{
	if (rState.crcLayout != ComputeLayoutCrc())
	{
		// The registered section set changed since the save, so its dense indices no longer name the same
		// sections. Discard the layout and keep the constructor defaults.
		LOG(kDefault, kWarning, "LoadTweaks section layout changed, using default layout");
		return;
	}

	mSectionVisible = rState.visible;
	mSectionCollapsed = rState.collapsed;
	for (int64_t i = 0; i < kiMaxTweakSections; ++i)
	{
		mWindowPositions[i] = ImVec2(rState.fWindowPositionX[i], rState.fWindowPositionY[i]);
	}
	std::memcpy(mActiveSubtab, rState.iActiveSubtab, sizeof(mActiveSubtab));
	mApplySubtab = AllSectionFlags();
}

void TweaksScreenBase::RenderSectionWindow(int64_t iSection)
{
	bool bHasActiveSlider = (!mActiveSlider.empty() && miActiveSliderSection == iSection);

	// Make window decorations transparent when a slider is active
	if (bHasActiveSlider)
	{
		ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
		ImGui::PushStyleColor(ImGuiCol_TitleBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
		ImGui::PushStyleColor(ImGuiCol_TitleBgActive, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
		ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
	}

	constexpr float kfStartX = 10.0f;
	ImVec2 f2InitialPosition = (mWindowPositions[iSection].y > 0.0f) ? mWindowPositions[iSection] : ImVec2 {kfStartX * UiScale(), mfToggleBarBottom};
	ImGui::SetNextWindowPos(f2InitialPosition, ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowCollapsed(mSectionCollapsed & SectionFlag(iSection), ImGuiCond_FirstUseEver);
	bool bSectionVisible = (mSectionVisible & SectionFlag(iSection)); // ImGui::Begin writes the close-button [x] state back through this bool*
	ImGui::Begin(GetSection(iSection).displayName.data(), bHasActiveSlider ? nullptr : &bSectionVisible, ImGuiWindowFlags_AlwaysAutoResize);
	ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * kfUiScale);
	mWindowPositions[iSection] = ImGui::GetWindowPos();
	mSectionCollapsed.Set(SectionFlag(iSection), ImGui::IsWindowCollapsed());
	if (!bHasActiveSlider)
	{
		mSectionVisible.Set(SectionFlag(iSection), bSectionVisible);
	}

	GetSection(iSection).pfnRender(*this);

	ImGui::PopFont();
	ImGui::End();

	if (bHasActiveSlider)
	{
		ImGui::PopStyleColor(4);
	}
}

void TweaksScreenBase::RunSliderAuditFrame()
{
	if constexpr (kbDebugInput)
	{
		// Lighting has 5 subtabs (Write/Combine/Read/Visible/Lighting); ImGui applies selection on the following tab-bar frame, so frames 0-4 queue tabs and frame 5 renders Lighting.
		static constexpr int8_t kiAuditFrameCount = 6;

		if (miAuditFrame == 0)
		{
			std::memcpy(mPreAuditSubtab, mActiveSubtab, sizeof(mActiveSubtab));
			ScopedSuppressAllocationTracking suppress;
			const size_t iSliderCount = TweaksSliderMap::Get().size();
			mAuditTouched.reserve(iSliderCount);
			mAuditMissed.reserve(8); // typical drift is small; reserve nominal to avoid 1-element bucket churn
		}

		// Force every section to expose its `miAuditFrame`-th subtab during the synthetic-render pass.
		for (int64_t i = 0; i < SectionCount(); ++i)
		{
			mActiveSubtab[i] = miAuditFrame;
			mApplySubtab.Set(SectionFlag(i));
		}

		mbAuditMode = true;

		// Synthetic offscreen window: BeginTabBar / WrapperSeparatorText / etc. need an active window, but we don't want anything visible or interactive.
		ImGui::SetNextWindowPos(ImVec2(-10000.0f, -10000.0f));
		ImGui::SetNextWindowSize(ImVec2(1.0f, 1.0f));
		static constexpr ImGuiWindowFlags kAuditFlags = ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoBringToFrontOnFocus;
		if (ImGui::Begin("##slider-audit", nullptr, kAuditFlags))
		{
			for (int64_t i = 0; i < SectionCount(); ++i)
			{
				GetSection(i).pfnRender(*this);
			}
		}
		ImGui::End();

		mbAuditMode = false;

		// Restore EVERY frame (not just on completion): the actual UI render later in the same Render() call must draw the user's saved tab, not the audit-cycled one.
		std::memcpy(mActiveSubtab, mPreAuditSubtab, sizeof(mActiveSubtab));
		mApplySubtab = AllSectionFlags();

		++miAuditFrame;
		if (miAuditFrame >= kiAuditFrameCount)
		{
			// Heap: TweaksSliderMap iteration touches its hash buckets; mirror the registration-side suppression.
			ScopedSuppressAllocationTracking suppress;
			for (const std::pair<const std::string_view, Wrapper*>& rEntry : TweaksSliderMap::Get())
			{
				if (!mAuditTouched.contains(rEntry.first))
				{
					LOG(kDefault, kWarning, "TweaksSliderMap: orphan key '{}'", rEntry.first);
				}
			}
			for (std::string_view missedKey : mAuditMissed)
			{
				LOG(kDefault, kWarning, "TweaksSliderMap: missed key '{}'", missedKey);
			}
			miAuditFrame = -1; // sentinel: audit complete
		}
	}
}

} // namespace engine

#endif // BT_CLIENT
