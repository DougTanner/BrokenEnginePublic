#include "TweaksScreen.h"

#include "Ui/WrapperBase.h"

#include "Game.h"

namespace engine
{

static constexpr const char* kpcSectionNames[] =
{
	"Test",
	"glTF",
	"Terrain",
	"Water Specular",
	"Water Low",
	"Water Medium",
	"Lighting",
	"Water Lighting",
	"Shadow",
	"Misc",
	"Hex Shield",
	"Smoke",
};
static_assert(std::size(kpcSectionNames) == static_cast<size_t>(TweakSection::kCount));

using RenderSectionFunc = void (TweaksScreen::*)();
static constexpr RenderSectionFunc kRenderSectionFunctions[] =
{
	&TweaksScreen::RenderTestSection,
	&TweaksScreen::RenderGltfSection,
	&TweaksScreen::RenderTerrainSection,
	&TweaksScreen::RenderWaterSpecularSection,
	&TweaksScreen::RenderWaterLowSection,
	&TweaksScreen::RenderWaterMediumSection,
	&TweaksScreen::RenderLightingSection,
	&TweaksScreen::RenderWaterLightingSection,
	&TweaksScreen::RenderShadowSection,
	&TweaksScreen::RenderMiscSection,
	&TweaksScreen::RenderHexShieldSection,
	&TweaksScreen::RenderSmokeSection,
};
static_assert(std::size(kRenderSectionFunctions) == static_cast<size_t>(TweakSection::kCount));

// UI scale factor for TweaksScreen
static constexpr float kfUiScale = 1.5f;

// Slider lookup map for active slider rendering
static std::unordered_map<std::string_view, Wrapper*>& GetSliderMap()
{
	static std::unordered_map<std::string_view, Wrapper*> sSliderMap =
	{
		// Test
		{"Test One", &gTestOne},
		{"Test Two", &gTestTwo},
		// glTF
		{"Exposure", &gGltfExposure},
		{"Gamma", &gGltfGamma},
		{"Ambient (IBL)", &gGltfIblAmbient},
		{"Diffuse", &gGltfDiffuse},
		{"Specular", &gGltfSpecular},
		{"Smoke", &gGltfSmoke},
		{"BRDF", &gGltfBrdf},
		{"BRDF Power", &gGltfBrdfPower},
		{"IBL", &gGltfIbl},
		{"IBL Power", &gGltfIblPower},
		{"Sun", &gGltfSun},
		{"Sun Power", &gGltfSunPower},
		{"Lighting", &gGltfLighting},
		{"Lighting Power", &gGltfLightingPower},
		// Terrain - Beach
		{"Snow Multiplier", &gTerrainSnowMultiplier},
		{"Beach Height", &gTerrainBeachHeight},
		{"Beach Sand Size", &gTerrainBeachSandSize},
		{"Beach Sand Blend", &gTerrainBeachSandBlend},
		{"Beach Normals Size 1", &gTerrainBeachNormalsSizeOne},
		{"Beach Normals Size 2", &gTerrainBeachNormalsSizeTwo},
		{"Beach Normals Size 3", &gTerrainBeachNormalsSizeThree},
		{"Beach Normals Blend", &gTerrainBeachNormalsBlend},
		// Terrain - Rock
		{"Island Height", &gIslandHeight},
		{"Rock Multiplier", &gTerrainRockMultiplier},
		{"Rock Size", &gTerrainRockSize},
		{"Rock Blend", &gTerrainRockBlend},
		{"Rock Normals Size 1", &gTerrainRockNormalsSizeOne},
		{"Rock Normals Size 2", &gTerrainRockNormalsSizeTwo},
		{"Rock Normals Size 3", &gTerrainRockNormalsSizeThree},
		{"Rock Normals Blend", &gTerrainRockNormalsBlend},
		// Water Specular - Normals
		{"Sampled Normals Size", &gLightingSampledNormalsSize},
		{"Sampled Normals Size Mod", &gLightingSampledNormalsSizeMod},
		{"Sampled Normals Speed", &gLightingSampledNormalsSpeed},
		{"Depth Reflection Feather", &gWaterDepthReflectionFeather},
		// Water Specular - Skybox
		{"Sun Bias", &gLightingWaterSkyboxSunBias},
		{"Normal Soften", &gLightingWaterSkyboxNormalSoften},
		{"Normal Blend Wave", &gLightingWaterSkyboxNormalBlendWave},
		{"Intensity", &gLightingWaterSkyboxIntensity},
		{"Add", &gLightingWaterSkyboxAdd},
		{"Skybox 1", &gLightingWaterSkyboxOne},
		{"Skybox 1 Power", &gLightingWaterSkyboxOnePower},
		{"Skybox 2", &gLightingWaterSkyboxTwo},
		{"Skybox 2 Power", &gLightingWaterSkyboxTwoPower},
		{"Skybox 3", &gLightingWaterSkyboxThree},
		{"Skybox 3 Power", &gLightingWaterSkyboxThreePower},
		// Water Specular - Height Darken
		{"Height Darken Top", &gWaterHeightDarkenTop},
		{"Height Darken Bottom", &gWaterHeightDarkenBottom},
		{"Height Darken Clamp", &gWaterHeightDarkenClamp},
		// Water Low - Wave
		{"Low Max", &gLowMax},
		{"Angle", &gLowAngle},
		{"Wavelength", &gLowWavelength},
		{"Amplitude", &gLowAmplitude},
		{"Speed", &gLowSpeed},
		{"Steepness", &gLowSteepness},
		// Water Low - Adjustments
		{"Angle Adjust", &gLowAngleAdjust},
		{"Wavelength Adjust", &gLowWavelengthAdjust},
		{"Amplitude Adjust", &gLowAmplitudeAdjust},
		{"Speed Adjust", &gLowSpeedAdjust},
		// Water Low - Beach Fade
		{"Beach Directional Fade Bottom", &gBeachDirectionalFadeBottom},
		{"Beach Directional Fade Height", &gBeachDirectionalFadeHeight},
		// Water Medium - Wave
		{"Medium Wavelength", &gMediumWavelength},
		{"Medium Amplitude", &gMediumAmplitude},
		{"Medium Speed", &gMediumSpeed},
		{"Medium Steepness", &gMediumSteepness},
		// Water Medium - Adjustments
		{"Medium Angle Adjust", &gMediumAngleAdjust},
		{"Medium Wavelength Adjust", &gMediumWavelengthAdjust},
		{"Medium Amplitude Adjust", &gMediumAmplitudeAdjust},
		{"Medium Speed Adjust", &gMediumSpeedAdjust},
		// Lighting - Blur
		{"Texture Multiplier", &gLightingTextureMultiplier},
		{"Blur Distance", &gLightingBlurDistance},
		{"Blur Directionality", &gLightingBlurDirectionality},
		{"Blur Jitter", &gLightingBlurJitter},
		{"Downscale", &gLightingBlurDownscale},
		// Lighting - Combine
		{"Combine Index", &gLightingCombineIndex},
		{"Blur First Divisor", &gLightingBlurFirstDivisor},
		{"Blur Divisor", &gLightingBlurDivisor},
		{"Combine Decay", &gLightingCombineDecay},
		{"Combine Power", &gLightingCombinePower},
		// Lighting - Directional
		{"Directional", &gLightingDirectional},
		{"Indirect", &gLightingIndirect},
		{"Terrain", &gLightingTerrain},
		{"Terrain Add", &gLightingAddTerrain},
		{"Objects", &gLightingObjects},
		{"Objects Add", &gLightingObjectsAdd},
		{"Time of Day Multiplier", &gLightingTimeOfDayMultiplier},
		// Water Lighting - Specular
		{"Specular Normal Soften", &gLightingWaterSpecularNormalSoften},
		{"Specular Normal Blend Wave", &gLightingWaterSpecularNormalBlendWave},
		{"Specular Diffuse", &gLightingWaterSpecularDiffuse},
		{"Specular Direct", &gLightingWaterSpecularDirect},
		{"Water Specular", &gLightingWaterSpecular},
		{"Specular Intensity", &gLightingWaterSpecularIntensity},
		{"Specular Add", &gLightingWaterSpecularAdd},
		{"Specular One", &gLightingWaterSpecularOne},
		{"Specular Two", &gLightingWaterSpecularTwo},
		{"Specular Three", &gLightingWaterSpecularThree},
		// Shadow - Feather
		{"Feather Noon", &gShadowFeatherNoon},
		{"Feather Noon Offset", &gShadowFeatherNoonOffset},
		{"Feather Sunset", &gShadowFeatherSunset},
		{"Feather Sunset Offset", &gShadowFeatherSunsetOffset},
		{"Feather Power", &gShadowFeatherPower},
		{"Distance Falloff", &gShadowDistanceFallof},
		{"Blur Sigma", &gShadowBlurSigma},
		{"Affect Ambient", &gShadowAffectAmbient},
		{"Height Fade Top", &gShadowHeightFadeTop},
		{"Height Fade Bottom", &gShadowHeightFadeBottom},
		// Shadow - Object Shadows
		{"Render Multiplier", &gObjectShadowsRenderMultiplier},
		{"Blur Multiplier", &gObjectShadowsBlurMultiplier},
		{"Shadow Noon", &gObjectShadowsNoon},
		{"Shadow Sunset", &gObjectShadowsSunset},
		{"Sunset Stretch", &gObjectShadowsSunsetStretch},
		{"Blur Distance Noon", &gObjectShadowsBlurDistanceNoon},
		{"Blur Distance Sunset", &gObjectShadowsBlurDistanceSunset},
		{"Smoke Shadow Intensity", &gSmokeShadowIntensity},
		// Misc
		{"Misc Island Height", &gIslandHeight},
		{"Water Depth", &gWaterDepth},
		{"Water Terrain Height", &gWaterTerrainHeight},
		{"Water Terrain Fade", &gWaterTerrainFade},
		{"Misc Depth Reflection Feather", &gWaterDepthReflectionFeather},
		{"Misc0", &gMisc0},
		// Hex Shield - Edge
		{"Grow", &gHexShieldGrow},
		{"Edge Distance", &gHexShieldEdgeDistance},
		{"Edge Power", &gHexShieldEdgePower},
		{"Edge Multiplier", &gHexShieldEdgeMultiplier},
		// Hex Shield - Wave
		{"Wave Multiplier", &gHexShieldWaveMultiplier},
		{"Wave Dot", &gHexShieldWaveDotMultiplier},
		{"Wave Intensity", &gHexShieldWaveIntensityMultiplier},
		{"Wave Intensity Power", &gHexShieldWaveIntensityPower},
		{"Wave Falloff Power", &gHexShieldWaveFalloffPower},
		// Hex Shield - Direction
		{"Direction Falloff Power", &gHexShieldDirectionFalloffPower},
		{"Direction Multiplier", &gHexShieldDirectionMultiplier},
		// Smoke - Decay
		{"Smoke Max", &gSmokeMax},
		{"Smoke Power", &gSmokePower},
		{"Smoke Decay", &gSmokeDecay},
		{"Smoke Decay Extra", &gSmokeDecayExtra},
		{"Smoke Decay Extra Threshold", &gSmokeDecayExtraThreshold},
		{"Smoke Edge Decay Distance", &gSmokeEdgeDecayDistance},
		// Smoke - Color
		{"Smoke Color Min", &gSmokeColorMin},
		{"Smoke Color Multiplier", &gSmokeColorMultiplier},
		{"Smoke Trails Falloff", &gSmokeTrailsFalloff},
		// Smoke - Wind/Noise
		{"Smoke Wind Noise Scale", &gSmokeWindNoiseScale},
		{"Smoke Wind Noise Quantity", &gSmokeWindNoiseQuantity},
		{"Smoke Noise Quantity", &gSmokeNoiseQuantity},
		{"Smoke Noise Scale One", &gSmokeNoiseScaleOne},
		{"Smoke Noise Scale Two", &gSmokeNoiseScaleTwo},
	};
	return sSliderMap;
}

TweaksScreen::TweaksScreen()
{
	// Initialize staggered window positions to prevent overlap
	constexpr float kfStartX = 10.0f;
	constexpr float kfStartY = 60.0f;
	constexpr float kfOffsetX = 30.0f;
	constexpr float kfOffsetY = 30.0f;

	for (size_t i = 0; i < mWindowPositions.size(); ++i)
	{
		mWindowPositions[i] = ImVec2(kfStartX + i * kfOffsetX, kfStartY + i * kfOffsetY);
	}
}

void TweaksScreen::WrapperSlider(std::string_view label, int iSection)
{
	auto& rSliderMap = GetSliderMap();
	auto it = rSliderMap.find(label);
	if (it == rSliderMap.end())
	{
		return;
	}

	// Render non-active sliders with alpha=0 to preserve layout
	bool bIsActiveSlider = (mpcActiveSlider == nullptr || label == mpcActiveSlider);
	if (!bIsActiveSlider)
	{
		ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.0f);
	}

	// Section sliders are twice as wide as default
	if (iSection >= 0)
	{
		ImGui::SetNextItemWidth(ImGui::CalcItemWidth() * 2.0f);
	}

	Wrapper* pWrapper = it->second;
	float fValue = pWrapper->Get();
	if (ImGui::SliderFloat(label.data(), &fValue, pWrapper->GetMin(), pWrapper->GetMax(), "%.6f"))
	{
		pWrapper->Set(fValue);
	}
	if (ImGui::IsItemActive())
	{
		mpcActiveSlider = label.data();
		miActiveSliderSection = iSection;
	}

	if (!bIsActiveSlider)
	{
		ImGui::PopStyleVar();
	}
}

void TweaksScreen::Render()
{
#if defined(ENABLE_DEBUG_INPUT)
	if (!game::gpGame->mbShowImGui)
	{
		return;
	}

	ImGuiIO& rIo = ImGui::GetIO();

	// Clear active slider when mouse released
	if (!rIo.MouseDown[0])
	{
		mpcActiveSlider = nullptr;
	}

	// Scale UI elements
	ImGuiStyle& rStyle = ImGui::GetStyle();
	ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(rStyle.FramePadding.x * kfUiScale, rStyle.FramePadding.y * kfUiScale));
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(rStyle.ItemSpacing.x * kfUiScale, rStyle.ItemSpacing.y * kfUiScale));
	ImGui::PushStyleVar(ImGuiStyleVar_ItemInnerSpacing, ImVec2(rStyle.ItemInnerSpacing.x * kfUiScale, rStyle.ItemInnerSpacing.y * kfUiScale));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(rStyle.WindowPadding.x * kfUiScale, rStyle.WindowPadding.y * kfUiScale));

	RenderToggleBar();

	// Render visible section windows (only the one with active slider when dragging)
	for (int i = 0; i < static_cast<int>(TweakSection::kCount); ++i)
	{
		if (mpcActiveSlider != nullptr)
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
#endif
}

void TweaksScreen::RenderToggleBar()
{
	// Capture state at start (mpcActiveSlider can change during WrapperSlider)
	bool bSliderActive = (mpcActiveSlider != nullptr);

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
	float fButtonWidth = (fContentWidth - ImGui::GetStyle().ItemSpacing.x * (static_cast<int>(TweakSection::kCount) - 1)) / static_cast<int>(TweakSection::kCount);

	// Render toggle buttons with alpha=0 when slider is active to preserve layout
	if (bSliderActive)
	{
		ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.0f);
	}

	// Center text within buttons
	ImGui::PushStyleVar(ImGuiStyleVar_SelectableTextAlign, ImVec2(0.5f, 0.5f));
	for (int i = 0; i < static_cast<int>(TweakSection::kCount); ++i)
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

	// Sun angle slider spans full content width (no label)
	ImGui::SetNextItemWidth(fContentWidth);
	float fSunAngle = gSunAngleOverride.Get();
	if (ImGui::SliderFloat("##Sun Angle", &fSunAngle, gSunAngleOverride.GetMin(), gSunAngleOverride.GetMax(), "%.6f"))
	{
		gSunAngleOverride.Set(fSunAngle);
	}
	if (ImGui::IsItemActive())
	{
		mpcActiveSlider = "##Sun Angle";
		miActiveSliderSection = -1;
	}

	ImGui::End();

	ImGui::PopStyleColor(2);
}

void TweaksScreen::WrapperSeparatorText(const char* pcLabel)
{
	if (mpcActiveSlider != nullptr)
	{
		ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.0f);
	}
	ImGui::SeparatorText(pcLabel);
	if (mpcActiveSlider != nullptr)
	{
		ImGui::PopStyleVar();
	}
}

void TweaksScreen::RenderSectionWindow(TweakSection eSection)
{
	int iSection = static_cast<int>(eSection);
	bool bHasActiveSlider = (mpcActiveSlider != nullptr && miActiveSliderSection == iSection);

	// Make window decorations transparent when a slider is active
	if (bHasActiveSlider)
	{
		ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
		ImGui::PushStyleColor(ImGuiCol_TitleBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
		ImGui::PushStyleColor(ImGuiCol_TitleBgActive, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
		ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
	}

	ImGui::SetNextWindowPos(mWindowPositions[iSection], ImGuiCond_FirstUseEver);
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

void TweaksScreen::RenderTestSection()
{
	WrapperSlider("Test One", static_cast<int>(TweakSection::kTest));
	WrapperSlider("Test Two", static_cast<int>(TweakSection::kTest));
}

void TweaksScreen::RenderGltfSection()
{
	WrapperSeparatorText("Tone Mapping");
	WrapperSlider("Exposure", static_cast<int>(TweakSection::kGltf));
	WrapperSlider("Gamma", static_cast<int>(TweakSection::kGltf));

	WrapperSeparatorText("Lighting");
	WrapperSlider("Ambient (IBL)", static_cast<int>(TweakSection::kGltf));
	WrapperSlider("Diffuse", static_cast<int>(TweakSection::kGltf));
	WrapperSlider("Specular", static_cast<int>(TweakSection::kGltf));
	WrapperSlider("Smoke", static_cast<int>(TweakSection::kGltf));

	WrapperSeparatorText("BRDF");
	WrapperSlider("BRDF", static_cast<int>(TweakSection::kGltf));
	WrapperSlider("BRDF Power", static_cast<int>(TweakSection::kGltf));

	WrapperSeparatorText("IBL");
	WrapperSlider("IBL", static_cast<int>(TweakSection::kGltf));
	WrapperSlider("IBL Power", static_cast<int>(TweakSection::kGltf));

	WrapperSeparatorText("Sun");
	WrapperSlider("Sun", static_cast<int>(TweakSection::kGltf));
	WrapperSlider("Sun Power", static_cast<int>(TweakSection::kGltf));

	WrapperSeparatorText("Post Lighting");
	WrapperSlider("Lighting", static_cast<int>(TweakSection::kGltf));
	WrapperSlider("Lighting Power", static_cast<int>(TweakSection::kGltf));
}

void TweaksScreen::RenderTerrainSection()
{
	WrapperSeparatorText("Beach");
	WrapperSlider("Snow Multiplier", static_cast<int>(TweakSection::kTerrain));
	WrapperSlider("Beach Height", static_cast<int>(TweakSection::kTerrain));
	WrapperSlider("Beach Sand Size", static_cast<int>(TweakSection::kTerrain));
	WrapperSlider("Beach Sand Blend", static_cast<int>(TweakSection::kTerrain));
	WrapperSlider("Beach Normals Size 1", static_cast<int>(TweakSection::kTerrain));
	WrapperSlider("Beach Normals Size 2", static_cast<int>(TweakSection::kTerrain));
	WrapperSlider("Beach Normals Size 3", static_cast<int>(TweakSection::kTerrain));
	WrapperSlider("Beach Normals Blend", static_cast<int>(TweakSection::kTerrain));

	WrapperSeparatorText("Rock");
	WrapperSlider("Island Height", static_cast<int>(TweakSection::kTerrain));
	WrapperSlider("Rock Multiplier", static_cast<int>(TweakSection::kTerrain));
	WrapperSlider("Rock Size", static_cast<int>(TweakSection::kTerrain));
	WrapperSlider("Rock Blend", static_cast<int>(TweakSection::kTerrain));
	WrapperSlider("Rock Normals Size 1", static_cast<int>(TweakSection::kTerrain));
	WrapperSlider("Rock Normals Size 2", static_cast<int>(TweakSection::kTerrain));
	WrapperSlider("Rock Normals Size 3", static_cast<int>(TweakSection::kTerrain));
	WrapperSlider("Rock Normals Blend", static_cast<int>(TweakSection::kTerrain));
}

void TweaksScreen::RenderWaterSpecularSection()
{
	WrapperSeparatorText("Normals");
	WrapperSlider("Sampled Normals Size", static_cast<int>(TweakSection::kWaterSpecular));
	WrapperSlider("Sampled Normals Size Mod", static_cast<int>(TweakSection::kWaterSpecular));
	WrapperSlider("Sampled Normals Speed", static_cast<int>(TweakSection::kWaterSpecular));
	WrapperSlider("Depth Reflection Feather", static_cast<int>(TweakSection::kWaterSpecular));

	WrapperSeparatorText("Skybox");
	WrapperSlider("Sun Bias", static_cast<int>(TweakSection::kWaterSpecular));
	WrapperSlider("Normal Soften", static_cast<int>(TweakSection::kWaterSpecular));
	WrapperSlider("Normal Blend Wave", static_cast<int>(TweakSection::kWaterSpecular));
	WrapperSlider("Intensity", static_cast<int>(TweakSection::kWaterSpecular));
	WrapperSlider("Add", static_cast<int>(TweakSection::kWaterSpecular));
	WrapperSlider("Skybox 1", static_cast<int>(TweakSection::kWaterSpecular));
	WrapperSlider("Skybox 1 Power", static_cast<int>(TweakSection::kWaterSpecular));
	WrapperSlider("Skybox 2", static_cast<int>(TweakSection::kWaterSpecular));
	WrapperSlider("Skybox 2 Power", static_cast<int>(TweakSection::kWaterSpecular));
	WrapperSlider("Skybox 3", static_cast<int>(TweakSection::kWaterSpecular));
	WrapperSlider("Skybox 3 Power", static_cast<int>(TweakSection::kWaterSpecular));

	WrapperSeparatorText("Height Darken");
	WrapperSlider("Height Darken Top", static_cast<int>(TweakSection::kWaterSpecular));
	WrapperSlider("Height Darken Bottom", static_cast<int>(TweakSection::kWaterSpecular));
	WrapperSlider("Height Darken Clamp", static_cast<int>(TweakSection::kWaterSpecular));
}

void TweaksScreen::RenderWaterLowSection()
{
	// Radio buttons for wave count selection (skip when slider is active)
	if (mpcActiveSlider == nullptr)
	{
		ImGui::Text("Wave Count");
		ImGui::SameLine();
		int64_t iCurrent = gLowCount.Get<int64_t>();
		for (const auto& [pcOptionLabel, iValue] : std::initializer_list<std::pair<const char*, int64_t>>{{"15", 15}, {"31", 31}, {"63", 63}, {"127", 127}, {"255", 255}})
		{
			if (ImGui::RadioButton(pcOptionLabel, iCurrent == iValue))
			{
				gLowCount.Set(iValue);
			}
			ImGui::SameLine();
		}
		ImGui::NewLine();
	}

	WrapperSeparatorText("Wave");
	WrapperSlider("Low Max", static_cast<int>(TweakSection::kWaterLow));
	WrapperSlider("Angle", static_cast<int>(TweakSection::kWaterLow));
	WrapperSlider("Wavelength", static_cast<int>(TweakSection::kWaterLow));
	WrapperSlider("Amplitude", static_cast<int>(TweakSection::kWaterLow));
	WrapperSlider("Speed", static_cast<int>(TweakSection::kWaterLow));
	WrapperSlider("Steepness", static_cast<int>(TweakSection::kWaterLow));

	WrapperSeparatorText("Adjustments");
	WrapperSlider("Angle Adjust", static_cast<int>(TweakSection::kWaterLow));
	WrapperSlider("Wavelength Adjust", static_cast<int>(TweakSection::kWaterLow));
	WrapperSlider("Amplitude Adjust", static_cast<int>(TweakSection::kWaterLow));
	WrapperSlider("Speed Adjust", static_cast<int>(TweakSection::kWaterLow));

	WrapperSeparatorText("Beach Fade");
	WrapperSlider("Beach Directional Fade Bottom", static_cast<int>(TweakSection::kWaterLow));
	WrapperSlider("Beach Directional Fade Height", static_cast<int>(TweakSection::kWaterLow));
}

void TweaksScreen::RenderWaterMediumSection()
{
	// Radio buttons for wave count selection (skip when slider is active)
	if (mpcActiveSlider == nullptr)
	{
		ImGui::Text("Wave Count");
		ImGui::SameLine();
		int64_t iCurrent = gMediumCount.Get<int64_t>();
		for (const auto& [pcOptionLabel, iValue] : std::initializer_list<std::pair<const char*, int64_t>>{{"15", 15}, {"31", 31}, {"63", 63}, {"127", 127}, {"255", 255}})
		{
			if (ImGui::RadioButton(pcOptionLabel, iCurrent == iValue))
			{
				gMediumCount.Set(iValue);
			}
			ImGui::SameLine();
		}
		ImGui::NewLine();
	}

	WrapperSeparatorText("Wave");
	WrapperSlider("Medium Wavelength", static_cast<int>(TweakSection::kWaterMedium));
	WrapperSlider("Medium Amplitude", static_cast<int>(TweakSection::kWaterMedium));
	WrapperSlider("Medium Speed", static_cast<int>(TweakSection::kWaterMedium));
	WrapperSlider("Medium Steepness", static_cast<int>(TweakSection::kWaterMedium));

	WrapperSeparatorText("Adjustments");
	WrapperSlider("Medium Angle Adjust", static_cast<int>(TweakSection::kWaterMedium));
	WrapperSlider("Medium Wavelength Adjust", static_cast<int>(TweakSection::kWaterMedium));
	WrapperSlider("Medium Amplitude Adjust", static_cast<int>(TweakSection::kWaterMedium));
	WrapperSlider("Medium Speed Adjust", static_cast<int>(TweakSection::kWaterMedium));
}

void TweaksScreen::RenderLightingSection()
{
	WrapperSeparatorText("Blur");
	WrapperSlider("Texture Multiplier", static_cast<int>(TweakSection::kLighting));
	WrapperSlider("Blur Distance", static_cast<int>(TweakSection::kLighting));
	WrapperSlider("Blur Directionality", static_cast<int>(TweakSection::kLighting));
	WrapperSlider("Blur Jitter", static_cast<int>(TweakSection::kLighting));
	WrapperSlider("Downscale", static_cast<int>(TweakSection::kLighting));

	WrapperSeparatorText("Combine");
	WrapperSlider("Combine Index", static_cast<int>(TweakSection::kLighting));
	WrapperSlider("Blur First Divisor", static_cast<int>(TweakSection::kLighting));
	WrapperSlider("Blur Divisor", static_cast<int>(TweakSection::kLighting));
	WrapperSlider("Combine Decay", static_cast<int>(TweakSection::kLighting));
	WrapperSlider("Combine Power", static_cast<int>(TweakSection::kLighting));

	WrapperSeparatorText("Directional");
	WrapperSlider("Directional", static_cast<int>(TweakSection::kLighting));
	WrapperSlider("Indirect", static_cast<int>(TweakSection::kLighting));
	WrapperSlider("Terrain", static_cast<int>(TweakSection::kLighting));
	WrapperSlider("Terrain Add", static_cast<int>(TweakSection::kLighting));
	WrapperSlider("Objects", static_cast<int>(TweakSection::kLighting));
	WrapperSlider("Objects Add", static_cast<int>(TweakSection::kLighting));
	WrapperSlider("Time of Day Multiplier", static_cast<int>(TweakSection::kLighting));
}

void TweaksScreen::RenderWaterLightingSection()
{
	WrapperSeparatorText("Specular");
	WrapperSlider("Specular Normal Soften", static_cast<int>(TweakSection::kWaterLighting));
	WrapperSlider("Specular Normal Blend Wave", static_cast<int>(TweakSection::kWaterLighting));
	WrapperSlider("Specular Diffuse", static_cast<int>(TweakSection::kWaterLighting));
	WrapperSlider("Specular Direct", static_cast<int>(TweakSection::kWaterLighting));
	WrapperSlider("Water Specular", static_cast<int>(TweakSection::kWaterLighting));
	WrapperSlider("Specular Intensity", static_cast<int>(TweakSection::kWaterLighting));
	WrapperSlider("Specular Add", static_cast<int>(TweakSection::kWaterLighting));
	WrapperSlider("Specular One", static_cast<int>(TweakSection::kWaterLighting));
	WrapperSlider("Specular Two", static_cast<int>(TweakSection::kWaterLighting));
	WrapperSlider("Specular Three", static_cast<int>(TweakSection::kWaterLighting));
}

void TweaksScreen::RenderShadowSection()
{
	WrapperSeparatorText("Feather");
	WrapperSlider("Feather Noon", static_cast<int>(TweakSection::kShadow));
	WrapperSlider("Feather Noon Offset", static_cast<int>(TweakSection::kShadow));
	WrapperSlider("Feather Sunset", static_cast<int>(TweakSection::kShadow));
	WrapperSlider("Feather Sunset Offset", static_cast<int>(TweakSection::kShadow));
	WrapperSlider("Feather Power", static_cast<int>(TweakSection::kShadow));
	WrapperSlider("Distance Falloff", static_cast<int>(TweakSection::kShadow));
	WrapperSlider("Blur Sigma", static_cast<int>(TweakSection::kShadow));
	WrapperSlider("Affect Ambient", static_cast<int>(TweakSection::kShadow));
	WrapperSlider("Height Fade Top", static_cast<int>(TweakSection::kShadow));
	WrapperSlider("Height Fade Bottom", static_cast<int>(TweakSection::kShadow));

	WrapperSeparatorText("Object Shadows");
	WrapperSlider("Render Multiplier", static_cast<int>(TweakSection::kShadow));
	WrapperSlider("Blur Multiplier", static_cast<int>(TweakSection::kShadow));
	WrapperSlider("Shadow Noon", static_cast<int>(TweakSection::kShadow));
	WrapperSlider("Shadow Sunset", static_cast<int>(TweakSection::kShadow));
	WrapperSlider("Sunset Stretch", static_cast<int>(TweakSection::kShadow));
	WrapperSlider("Blur Distance Noon", static_cast<int>(TweakSection::kShadow));
	WrapperSlider("Blur Distance Sunset", static_cast<int>(TweakSection::kShadow));
	WrapperSlider("Smoke Shadow Intensity", static_cast<int>(TweakSection::kShadow));
}

void TweaksScreen::RenderMiscSection()
{
	WrapperSlider("Misc Island Height", static_cast<int>(TweakSection::kMisc));
	WrapperSlider("Water Depth", static_cast<int>(TweakSection::kMisc));
	WrapperSlider("Water Terrain Height", static_cast<int>(TweakSection::kMisc));
	WrapperSlider("Water Terrain Fade", static_cast<int>(TweakSection::kMisc));
	WrapperSlider("Misc Depth Reflection Feather", static_cast<int>(TweakSection::kMisc));
	WrapperSlider("Misc0", static_cast<int>(TweakSection::kMisc));
}

void TweaksScreen::RenderHexShieldSection()
{
	WrapperSeparatorText("Edge");
	WrapperSlider("Grow", static_cast<int>(TweakSection::kHexShield));
	WrapperSlider("Edge Distance", static_cast<int>(TweakSection::kHexShield));
	WrapperSlider("Edge Power", static_cast<int>(TweakSection::kHexShield));
	WrapperSlider("Edge Multiplier", static_cast<int>(TweakSection::kHexShield));

	WrapperSeparatorText("Wave");
	WrapperSlider("Wave Multiplier", static_cast<int>(TweakSection::kHexShield));
	WrapperSlider("Wave Dot", static_cast<int>(TweakSection::kHexShield));
	WrapperSlider("Wave Intensity", static_cast<int>(TweakSection::kHexShield));
	WrapperSlider("Wave Intensity Power", static_cast<int>(TweakSection::kHexShield));
	WrapperSlider("Wave Falloff Power", static_cast<int>(TweakSection::kHexShield));

	WrapperSeparatorText("Direction");
	WrapperSlider("Direction Falloff Power", static_cast<int>(TweakSection::kHexShield));
	WrapperSlider("Direction Multiplier", static_cast<int>(TweakSection::kHexShield));
}

void TweaksScreen::RenderSmokeSection()
{
	WrapperSeparatorText("Decay");
	WrapperSlider("Smoke Max", static_cast<int>(TweakSection::kSmoke));
	WrapperSlider("Smoke Power", static_cast<int>(TweakSection::kSmoke));
	WrapperSlider("Smoke Decay", static_cast<int>(TweakSection::kSmoke));
	WrapperSlider("Smoke Decay Extra", static_cast<int>(TweakSection::kSmoke));
	WrapperSlider("Smoke Decay Extra Threshold", static_cast<int>(TweakSection::kSmoke));
	WrapperSlider("Smoke Edge Decay Distance", static_cast<int>(TweakSection::kSmoke));

	WrapperSeparatorText("Color");
	WrapperSlider("Smoke Color Min", static_cast<int>(TweakSection::kSmoke));
	WrapperSlider("Smoke Color Multiplier", static_cast<int>(TweakSection::kSmoke));
	WrapperSlider("Smoke Trails Falloff", static_cast<int>(TweakSection::kSmoke));

	WrapperSeparatorText("Wind/Noise");
	WrapperSlider("Smoke Wind Noise Scale", static_cast<int>(TweakSection::kSmoke));
	WrapperSlider("Smoke Wind Noise Quantity", static_cast<int>(TweakSection::kSmoke));
	WrapperSlider("Smoke Noise Quantity", static_cast<int>(TweakSection::kSmoke));
	WrapperSlider("Smoke Noise Scale One", static_cast<int>(TweakSection::kSmoke));
	WrapperSlider("Smoke Noise Scale Two", static_cast<int>(TweakSection::kSmoke));
}

} // namespace engine
