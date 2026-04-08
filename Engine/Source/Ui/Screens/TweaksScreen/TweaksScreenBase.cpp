#include "TweaksScreenBase.h"

#include "TweaksSliderMap.h"

namespace engine
{

static constexpr const char* kpcSectionNames[] =
{
	"Test",
	"Pbr",
	"Terrain",
	"Water Specular",
	"Water Low",
	"Water Medium",
	"Water Debug",
	"Lighting",
	"Shadow",
	"Misc",
	"Hex Shield",
	"Smoke",
	"Wind",
	"Wind Deposits",
};
static_assert(std::size(kpcSectionNames) == static_cast<size_t>(TweakSection::kCount));

using RenderSectionFunc = void (TweaksScreenBase::*)();
static constexpr RenderSectionFunc kRenderSectionFunctions[] =
{
	&TweaksScreenBase::RenderTestSection,
	&TweaksScreenBase::RenderPbrSection,
	&TweaksScreenBase::RenderTerrainSection,
	&TweaksScreenBase::RenderWaterSpecularSection,
	&TweaksScreenBase::RenderWaterLowSection,
	&TweaksScreenBase::RenderWaterMediumSection,
	&TweaksScreenBase::RenderWaterDebugSection,
	&TweaksScreenBase::RenderLightingSection,
	&TweaksScreenBase::RenderShadowSection,
	&TweaksScreenBase::RenderMiscSection,
	&TweaksScreenBase::RenderHexShieldSection,
	&TweaksScreenBase::RenderSmokeSection,
	&TweaksScreenBase::RenderWindSection,
	&TweaksScreenBase::RenderWindDepositsSection,
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

void TweaksScreenBase::WrapperSlider(std::string_view label, int64_t iSection, float fWidthMultiplier, std::string_view mapKey)
{
	if (mapKey.empty())
	{
		mapKey = label;
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
	char pImGuiLabel[128];
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

void TweaksScreenBase::SaveState(bool* pSectionVisible, ImVec2* pWindowPositions, int8_t* pActiveSubtab) const
{
	std::memcpy(pSectionVisible, mSectionVisible, sizeof(mSectionVisible));
	std::memcpy(pWindowPositions, mWindowPositions, sizeof(mWindowPositions));
	std::memcpy(pActiveSubtab, mActiveSubtab, sizeof(mActiveSubtab));
}

void TweaksScreenBase::LoadState(const bool* pSectionVisible, const ImVec2* pWindowPositions, const int8_t* pActiveSubtab)
{
	std::memcpy(mSectionVisible, pSectionVisible, sizeof(mSectionVisible));
	std::memcpy(mWindowPositions, pWindowPositions, sizeof(mWindowPositions));
	std::memcpy(mActiveSubtab, pActiveSubtab, sizeof(mActiveSubtab));
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
	ImGui::Begin(kpcSectionNames[iSection], bHasActiveSlider ? nullptr : &mSectionVisible[iSection], ImGuiWindowFlags_AlwaysAutoResize);
	ImGui::SetWindowFontScale(kfUiScale);
	mWindowPositions[iSection] = ImGui::GetWindowPos();

	(this->*kRenderSectionFunctions[iSection])();

	ImGui::End();

	if (bHasActiveSlider)
	{
		ImGui::PopStyleColor(4);
	}
}

} // namespace engine
