#include "TweaksScreenBase.h"

#if defined(BT_CLIENT)

#include "TweaksSliderMap.h"
#include "Ui/GraphicsSettingsWrappersBase.h"

namespace engine
{

static constexpr const char* kpcSectionNames[] =
{
	"Pbr",
	"Terrain",
	"Water",
	"Lighting",
	"Shadow",
	"Sun/Moon",
	"Misc",
	"Hex Shield",
	"Smoke",
	"Wind",
	"Particles",
};
static_assert(std::size(kpcSectionNames) == static_cast<size_t>(TweakSection::kCount));

using RenderSectionFunc = void (TweaksScreenBase::*)();
static constexpr RenderSectionFunc kRenderSectionFunctions[] =
{
	&TweaksScreenBase::RenderPbrSection,
	&TweaksScreenBase::RenderTerrainSection,
	&TweaksScreenBase::RenderWaterSection,
	&TweaksScreenBase::RenderLightingSection,
	&TweaksScreenBase::RenderShadowSection,
	&TweaksScreenBase::RenderSunMoonSection,
	&TweaksScreenBase::RenderMiscSection,
	&TweaksScreenBase::RenderHexShieldSection,
	&TweaksScreenBase::RenderSmokeSection,
	&TweaksScreenBase::RenderWindSection,
	&TweaksScreenBase::RenderParticlesSection,
};
static_assert(std::size(kRenderSectionFunctions) == static_cast<size_t>(TweakSection::kCount));

// UI scale factor for TweaksScreen
static constexpr float kfUiScale = 1.5f;

TweaksScreenBase::TweaksScreenBase()
{
	// Initialize staggered window positions (Y set to 0, will use mfToggleBarBottom at runtime)
	constexpr float kfStartX = 10.0f;
	constexpr float kfOffsetX = 30.0f;

	for (size_t i = 0; i < static_cast<size_t>(TweakSection::kCount); ++i)
	{
		mWindowPositions[i] = ImVec2(kfStartX + i * kfOffsetX, 0.0f);
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
			// Heap: STL hash buckets allocate. Audit runs once per session at first tweaks-UI open; suppression mirrors TweaksSliderMap::Get().
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

	// Section sliders are twice as wide as default
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
		mActiveSlider = mapKey.data();
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
		for (int64_t i = 0; i < static_cast<int64_t>(TweakSection::kCount); ++i)
		{
			if (!mActiveSlider.empty())
			{
				if (i == miActiveSliderSection)
				{
					RenderSectionWindow(static_cast<TweakSection>(i));
				}
			}
			else if (mSectionVisible[i])
			{
				RenderSectionWindow(static_cast<TweakSection>(i));
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
	ImGui::SetNextWindowPos(ImVec2(0.0f, 10.0f), ImGuiCond_Always);
	ImGui::SetNextWindowSize(ImVec2(rIo.DisplaySize.x, 0.0f));
	ImGui::Begin("Tweaks", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize);
	ImGui::SetWindowFontScale(kfUiScale);

	// Calculate content width (full screen minus window padding)
	float fContentWidth = rIo.DisplaySize.x - ImGui::GetStyle().WindowPadding.x * 2.0f;

	// Calculate button width to fill available space
	float fButtonWidth = (fContentWidth - ImGui::GetStyle().ItemSpacing.x * (static_cast<int64_t>(TweakSection::kCount) - 1)) / static_cast<int64_t>(TweakSection::kCount);

	// Render toggle buttons with alpha=0 when slider is active to preserve layout
	if (bSliderActive)
	{
		ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.0f);
	}

	// Center text within buttons
	ImGui::PushStyleVar(ImGuiStyleVar_SelectableTextAlign, ImVec2(0.5f, 0.5f));
	for (int64_t i = 0; i < static_cast<int64_t>(TweakSection::kCount); ++i)
	{
		if (i > 0)
		{
			ImGui::SameLine();
		}
		if (ImGui::Selectable(kpcSectionNames[i], mSectionVisible[i], 0, ImVec2(fButtonWidth, 0.0f)))
		{
			mSectionVisible[i] = !mSectionVisible[i];
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

void TweaksScreenBase::SaveState(bool* pSectionVisible, ImVec2* pWindowPositions, int8_t* pActiveSubtab, bool* pSectionCollapsed) const
{
	std::memcpy(pSectionVisible, mSectionVisible, sizeof(mSectionVisible));
	std::memcpy(pWindowPositions, mWindowPositions, sizeof(mWindowPositions));
	std::memcpy(pActiveSubtab, mActiveSubtab, sizeof(mActiveSubtab));
	std::memcpy(pSectionCollapsed, mSectionCollapsed, sizeof(mSectionCollapsed));
}

void TweaksScreenBase::LoadState(const bool* pSectionVisible, const ImVec2* pWindowPositions, const int8_t* pActiveSubtab, const bool* pSectionCollapsed)
{
	std::memcpy(mSectionVisible, pSectionVisible, sizeof(mSectionVisible));
	std::memcpy(mWindowPositions, pWindowPositions, sizeof(mWindowPositions));
	std::memcpy(mActiveSubtab, pActiveSubtab, sizeof(mActiveSubtab));
	std::memcpy(mSectionCollapsed, pSectionCollapsed, sizeof(mSectionCollapsed));
	std::fill(std::begin(mApplySubtab), std::end(mApplySubtab), true);
}

void TweaksScreenBase::RenderSectionWindow(TweakSection eSection)
{
	int64_t iSection = static_cast<int64_t>(eSection);
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
	ImVec2 f2InitialPosition = (mWindowPositions[iSection].y > 0.0f) ? mWindowPositions[iSection] : ImVec2 {kfStartX, mfToggleBarBottom};
	ImGui::SetNextWindowPos(f2InitialPosition, ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowCollapsed(mSectionCollapsed[iSection], ImGuiCond_FirstUseEver);
	ImGui::Begin(kpcSectionNames[iSection], bHasActiveSlider ? nullptr : &mSectionVisible[iSection], ImGuiWindowFlags_AlwaysAutoResize);
	ImGui::SetWindowFontScale(kfUiScale);
	mWindowPositions[iSection] = ImGui::GetWindowPos();
	mSectionCollapsed[iSection] = ImGui::IsWindowCollapsed();

	(this->*kRenderSectionFunctions[iSection])();

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
		// Lighting has 5 subtabs (Write/Combine/Read/Visible/Lighting); cycle that many frames so every gated WrapperSlider call fires.
		static constexpr int8_t kiAuditFrameCount = 5;

		if (miAuditFrame == 0)
		{
			std::memcpy(mPreAuditSubtab, mActiveSubtab, sizeof(mActiveSubtab));
			ScopedSuppressAllocationTracking suppress;
			const size_t iSliderCount = TweaksSliderMap::Get().size();
			mAuditTouched.reserve(iSliderCount);
			mAuditMissed.reserve(8); // typical drift is small; reserve nominal to avoid 1-element bucket churn
		}

		// Force every section to expose its `miAuditFrame`-th subtab during the synthetic-render pass.
		for (size_t i = 0; i < static_cast<size_t>(TweakSection::kCount); ++i)
		{
			mActiveSubtab[i] = miAuditFrame;
			mApplySubtab[i] = true;
		}

		mbAuditMode = true;

		// Synthetic offscreen window: BeginTabBar / WrapperSeparatorText / etc. need an active window, but we don't want anything visible or interactive.
		ImGui::SetNextWindowPos(ImVec2(-10000.0f, -10000.0f));
		ImGui::SetNextWindowSize(ImVec2(1.0f, 1.0f));
		ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.0f);
		static constexpr ImGuiWindowFlags kAuditFlags = ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoBringToFrontOnFocus;
		if (ImGui::Begin("##slider-audit", nullptr, kAuditFlags))
		{
			for (size_t i = 0; i < static_cast<size_t>(TweakSection::kCount); ++i)
			{
				(this->*kRenderSectionFunctions[i])();
			}
		}
		ImGui::End();
		ImGui::PopStyleVar();

		mbAuditMode = false;

		// Restore EVERY frame (not just on completion): the actual UI render later in the same Render() call must draw the user's saved tab, not the audit-cycled one.
		std::memcpy(mActiveSubtab, mPreAuditSubtab, sizeof(mActiveSubtab));
		std::fill(std::begin(mApplySubtab), std::end(mApplySubtab), true);

		++miAuditFrame;
		if (miAuditFrame >= kiAuditFrameCount)
		{
			// Heap: TweaksSliderMap iteration touches its hash buckets; mirror the registration-side suppression.
			ScopedSuppressAllocationTracking suppress;
			for (const auto& [rKey, pWrapper] : TweaksSliderMap::Get())
			{
				if (!mAuditTouched.contains(rKey))
				{
					LOG(kDefault, kWarning, "TweaksSliderMap: orphan key '{}'", rKey);
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
