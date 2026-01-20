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
	"Lighting",
	"Water Lighting",
	"Shadow",
	"Misc",
	"Hex Shield",
	"Smoke",
};
static_assert(std::size(kpcSectionNames) == static_cast<size_t>(TweakSection::kCount));

// UI scale factor for TweaksScreen
static constexpr float kfUiScale = 2.0f;

struct SliderConfig
{
	Wrapper* pWrapper = nullptr;
	float fMin = 0.0f;
	float fMax = 0.0f;
};

// Slider lookup map for active slider rendering
static std::unordered_map<std::string_view, SliderConfig>& GetSliderMap()
{
	static std::unordered_map<std::string_view, SliderConfig> sSliderMap =
	{
		// Test
		{"Test One", {&gTestOne, -10.0f, 10.0f}},
		{"Test Two", {&gTestTwo, -10.0f, 10.0f}},
		// glTF
		{"Sun Angle", {&gSunAngleOverride, 0.0f, XM_PI}},
		{"Exposure", {&gGltfExposure, 0.0f, 10.0f}},
		{"Gamma", {&gGltfGamma, 0.0f, 2.0f}},
		{"Ambient (IBL)", {&gGltfIblAmbient, 0.0f, 2.0f}},
		{"Diffuse", {&gGltfDiffuse, 0.0f, 3.0f}},
		{"Specular", {&gGltfSpecular, 0.0f, 10.0f}},
		{"Smoke", {&gGltfSmoke, 0.0f, 1.0f}},
		{"BRDF", {&gGltfBrdf, 0.0f, 10.0f}},
		{"BRDF Power", {&gGltfBrdfPower, 0.0f, 4.0f}},
		{"IBL", {&gGltfIbl, 0.0f, 4.0f}},
		{"IBL Power", {&gGltfIblPower, 0.0f, 4.0f}},
		{"Sun", {&gGltfSun, 0.0f, 10.0f}},
		{"Sun Power", {&gGltfSunPower, 0.0f, 4.0f}},
		{"Lighting", {&gGltfLighting, 0.0f, 0.4f}},
		{"Lighting Power", {&gGltfLightingPower, 0.0f, 2.0f}},
		// Terrain - Beach
		{"Snow Multiplier", {&gTerrainSnowMultiplier, 1.0f, 5.0f}},
		{"Beach Height", {&gTerrainBeachHeight, 0.0f, 0.2f}},
		{"Beach Sand Size", {&gTerrainBeachSandSize, 0.01f, 0.4f}},
		{"Beach Sand Blend", {&gTerrainBeachSandBlend, 0.0f, 1.0f}},
		{"Beach Normals Size 1", {&gTerrainBeachNormalsSizeOne, 0.001f, 0.1f}},
		{"Beach Normals Size 2", {&gTerrainBeachNormalsSizeTwo, 0.005f, 0.05f}},
		{"Beach Normals Size 3", {&gTerrainBeachNormalsSizeThree, 0.01f, 0.5f}},
		{"Beach Normals Blend", {&gTerrainBeachNormalsBlend, 0.0f, 4.0f}},
		// Terrain - Rock
		{"Island Height", {&gIslandHeight, 10.0f, 50.0f}},
		{"Rock Multiplier", {&gTerrainRockMultiplier, 1.0f, 20.0f}},
		{"Rock Size", {&gTerrainRockSize, 0.01f, 0.4f}},
		{"Rock Blend", {&gTerrainRockBlend, 0.0f, 1.0f}},
		{"Rock Normals Size 1", {&gTerrainRockNormalsSizeOne, 0.01f, 0.5f}},
		{"Rock Normals Size 2", {&gTerrainRockNormalsSizeTwo, 0.005f, 0.5f}},
		{"Rock Normals Size 3", {&gTerrainRockNormalsSizeThree, 0.01f, 0.5f}},
		{"Rock Normals Blend", {&gTerrainRockNormalsBlend, 0.0f, 2.0f}},
		// Water Specular - Normals
		{"Sampled Normals Size", {&gLightingSampledNormalsSize, 0.05f, 0.5f}},
		{"Sampled Normals Size Mod", {&gLightingSampledNormalsSizeMod, -0.02f, 0.02f}},
		{"Sampled Normals Speed", {&gLightingSampledNormalsSpeed, 0.0f, 0.05f}},
		{"Depth Reflection Feather", {&gWaterDepthReflectionFeather, 0.001f, 0.1f}},
		// Water Specular - Skybox
		{"Sun Bias", {&gLightingWaterSkyboxSunBias, 0.0f, 4.0f}},
		{"Normal Soften", {&gLightingWaterSkyboxNormalSoften, 0.0f, 1.0f}},
		{"Normal Blend Wave", {&gLightingWaterSkyboxNormalBlendWave, 0.0f, 0.2f}},
		{"Intensity", {&gLightingWaterSkyboxIntensity, 0.0005f, 0.004f}},
		{"Add", {&gLightingWaterSkyboxAdd, 0.0f, 2.0f}},
		{"Skybox 1", {&gLightingWaterSkyboxOne, 0.0f, 3000.0f}},
		{"Skybox 1 Power", {&gLightingWaterSkyboxOnePower, 50.0f, 400.0f}},
		{"Skybox 2", {&gLightingWaterSkyboxTwo, 0.0f, 400.0f}},
		{"Skybox 2 Power", {&gLightingWaterSkyboxTwoPower, 2.0f, 10.0f}},
		{"Skybox 3", {&gLightingWaterSkyboxThree, 1.0f, 800.0f}},
		{"Skybox 3 Power", {&gLightingWaterSkyboxThreePower, 0.01f, 2.0f}},
		// Water Specular - Height Darken
		{"Height Darken Top", {&gWaterHeightDarkenTop, -0.1f, 0.05f}},
		{"Height Darken Bottom", {&gWaterHeightDarkenBottom, -0.5f, 0.0f}},
		{"Height Darken Clamp", {&gWaterHeightDarkenClamp, 0.0f, 0.9f}},
		// Water Low - Wave
		{"Low Max", {&gLowMax, 0.0f, 255.0f}},
		{"Angle", {&gLowAngle, 0.0f, XM_2PI}},
		{"Wavelength", {&gLowWavelength, 1.0f, 20.0f}},
		{"Amplitude", {&gLowAmplitude, 0.0f, 0.1f}},
		{"Speed", {&gLowSpeed, 0.0f, 1.0f}},
		{"Steepness", {&gLowSteepness, 0.0f, 1.0f}},
		// Water Low - Adjustments
		{"Angle Adjust", {&gLowAngleAdjust, 0.0f, 2.0f}},
		{"Wavelength Adjust", {&gLowWavelengthAdjust, -1.0f, 0.0f}},
		{"Amplitude Adjust", {&gLowAmplitudeAdjust, 0.0f, 2.0f}},
		{"Speed Adjust", {&gLowSpeedAdjust, 0.0f, 2.0f}},
		// Water Low - Beach Fade
		{"Beach Directional Fade Bottom", {&gBeachDirectionalFadeBottom, 0.0f, 2.0f}},
		{"Beach Directional Fade Height", {&gBeachDirectionalFadeHeight, 0.0f, 1.0f}},
		// Lighting - Blur
		{"Texture Multiplier", {&gLightingTextureMultiplier, 1.0f / 64.0f, 1.0f}},
		{"Blur Distance", {&gLightingBlurDistance, 0.0f, 0.5f}},
		{"Blur Directionality", {&gLightingBlurDirectionality, 0.0f, 1.0f}},
		{"Blur Jitter", {&gLightingBlurJitter, 0.0f, 0.2f}},
		{"Downscale", {&gLightingBlurDownscale, 0.5f, 0.9f}},
		// Lighting - Combine
		{"Combine Index", {&gLightingCombineIndex, 0.0f, 10.4f}},
		{"Blur First Divisor", {&gLightingBlurFirstDivisor, 100.0f, 2000.0f}},
		{"Blur Divisor", {&gLightingBlurDivisor, 0.0f, 1.0f}},
		{"Combine Decay", {&gLightingCombineDecay, 0.5f, 1.0f}},
		{"Combine Power", {&gLightingCombinePower, 0.1f, 2.0f}},
		// Lighting - Directional
		{"Directional", {&gLightingDirectional, 1.0f, 3.0f}},
		{"Indirect", {&gLightingIndirect, 0.5f, 2.0f}},
		{"Terrain", {&gLightingTerrain, 0.0f, 2.0f}},
		{"Terrain Add", {&gLightingAddTerrain, 0.0f, 1.0f}},
		{"Objects", {&gLightingObjects, 0.0f, 8.0f}},
		{"Objects Add", {&gLightingObjectsAdd, 0.0f, 1.0f}},
		{"Time of Day Multiplier", {&gLightingTimeOfDayMultiplier, 0.0f, 1.0f}},
		// Water Lighting - Specular
		{"Specular Normal Soften", {&gLightingWaterSpecularNormalSoften, 0.0f, 0.5f}},
		{"Specular Normal Blend Wave", {&gLightingWaterSpecularNormalBlendWave, 0.0f, 0.5f}},
		{"Specular Diffuse", {&gLightingWaterSpecularDiffuse, 0.0f, 4.0f}},
		{"Specular Direct", {&gLightingWaterSpecularDirect, 0.0f, 40.0f}},
		{"Water Specular", {&gLightingWaterSpecular, 0.0f, 8.0f}},
		{"Specular Intensity", {&gLightingWaterSpecularIntensity, 0.0f, 0.02f}},
		{"Specular Add", {&gLightingWaterSpecularAdd, 0.0f, 2.0f}},
		{"Specular One", {&gLightingWaterSpecularOne, 0.0f, 600.0f}},
		{"Specular Two", {&gLightingWaterSpecularTwo, 0.0f, 20.0f}},
		{"Specular Three", {&gLightingWaterSpecularThree, 0.0f, 10.0f}},
		// Shadow - Feather
		{"Feather Noon", {&gShadowFeatherNoon, 0.0f, 8.0f}},
		{"Feather Noon Offset", {&gShadowFeatherNoonOffset, 0.0f, 5.0f}},
		{"Feather Sunset", {&gShadowFeatherSunset, 0.0f, 0.5f}},
		{"Feather Sunset Offset", {&gShadowFeatherSunsetOffset, -0.5f, 0.1f}},
		{"Feather Power", {&gShadowFeatherPower, 0.1f, 10.0f}},
		{"Distance Falloff", {&gShadowDistanceFallof, 10.0f, 400.0f}},
		{"Blur Sigma", {&gShadowBlurSigma, 1.0f, 20.0f}},
		{"Affect Ambient", {&gShadowAffectAmbient, 0.0f, 1.0f}},
		{"Height Fade Top", {&gShadowHeightFadeTop, 0.0f, 20.0f}},
		{"Height Fade Bottom", {&gShadowHeightFadeBottom, -20.0f, 0.0f}},
		// Shadow - Object Shadows
		{"Render Multiplier", {&gObjectShadowsRenderMultiplier, 0.25f, 4.0f}},
		{"Blur Multiplier", {&gObjectShadowsBlurMultiplier, 0.125f, 1.0f}},
		{"Shadow Noon", {&gObjectShadowsNoon, 0.1f, 1.0f}},
		{"Shadow Sunset", {&gObjectShadowsSunset, 0.1f, 1.0f}},
		{"Sunset Stretch", {&gObjectShadowsSunsetStretch, 0.0f, 10.0f}},
		{"Blur Distance Noon", {&gObjectShadowsBlurDistanceNoon, 0.00005f, 0.001f}},
		{"Blur Distance Sunset", {&gObjectShadowsBlurDistanceSunset, 0.0001f, 0.002f}},
		{"Smoke Shadow Intensity", {&gSmokeShadowIntensity, 0.0f, 1.0f}},
		// Misc
		{"Misc Island Height", {&gIslandHeight, 10.0f, 50.0f}},
		{"Water Depth", {&gWaterDepth, 1.0f, 20.0f}},
		{"Water Terrain Height", {&gWaterTerrainHeight, 1.0f, 8.0f}},
		{"Water Terrain Fade", {&gWaterTerrainFade, 0.001f, 0.04f}},
		{"Misc Depth Reflection Feather", {&gWaterDepthReflectionFeather, 0.001f, 0.1f}},
		{"Misc0", {&gMisc0, -60.0f, -40.0f}},
		// Hex Shield - Edge
		{"Grow", {&gHexShieldGrow, 1.0f, 4.0f}},
		{"Edge Distance", {&gHexShieldEdgeDistance, 18.0f, 19.1f}},
		{"Edge Power", {&gHexShieldEdgePower, 0.5f, 2.0f}},
		{"Edge Multiplier", {&gHexShieldEdgeMultiplier, 0.25f, 1.0f}},
		// Hex Shield - Wave
		{"Wave Multiplier", {&gHexShieldWaveMultiplier, 0.0f, 20.0f}},
		{"Wave Dot", {&gHexShieldWaveDotMultiplier, 0.5f, 10.0f}},
		{"Wave Intensity", {&gHexShieldWaveIntensityMultiplier, 0.5f, 20.0f}},
		{"Wave Intensity Power", {&gHexShieldWaveIntensityPower, 0.25f, 4.0f}},
		{"Wave Falloff Power", {&gHexShieldWaveFalloffPower, 0.25f, 4.0f}},
		// Hex Shield - Direction
		{"Direction Falloff Power", {&gHexShieldDirectionFalloffPower, 2.0f, 10.0f}},
		{"Direction Multiplier", {&gHexShieldDirectionMultiplier, 0.5f, 8.0f}},
		// Smoke - Decay
		{"Smoke Max", {&gSmokeMax, 0.0f, 1.0f}},
		{"Smoke Power", {&gSmokePower, 0.1f, 1.0f}},
		{"Smoke Decay", {&gSmokeDecay, 0.990f, 1.0f}},
		{"Smoke Decay Extra", {&gSmokeDecayExtra, 0.95f, 1.0f}},
		{"Smoke Decay Extra Threshold", {&gSmokeDecayExtraThreshold, 0.0f, 0.0005f}},
		{"Smoke Edge Decay Distance", {&gSmokeEdgeDecayDistance, 0.0f, 1.0f}},
		// Smoke - Color
		{"Smoke Color Min", {&gSmokeColorMin, 0.0f, 1.0f}},
		{"Smoke Color Multiplier", {&gSmokeColorMultiplier, 0.1f, 4.0f}},
		{"Smoke Trails Falloff", {&gSmokeTrailsFalloff, 0.1f, 10.0f}},
		// Smoke - Wind/Noise
		{"Smoke Wind Noise Scale", {&gSmokeWindNoiseScale, 0.001f, 0.1f}},
		{"Smoke Wind Noise Quantity", {&gSmokeWindNoiseQuantity, 0.0f, 0.0001f}},
		{"Smoke Noise Quantity", {&gSmokeNoiseQuantity, 0.00001f, 0.0002f}},
		{"Smoke Noise Scale One", {&gSmokeNoiseScaleOne, 0.1f, 8.0f}},
		{"Smoke Noise Scale Two", {&gSmokeNoiseScaleTwo, 0.01f, 1.0f}},
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
	// Skip rendering if another slider is active
	if (mpcActiveSlider != nullptr && label != mpcActiveSlider)
	{
		return;
	}

	auto& rSliderMap = GetSliderMap();
	auto it = rSliderMap.find(label);
	if (it == rSliderMap.end())
	{
		return;
	}

	SliderConfig& rConfig = it->second;
	float fValue = rConfig.pWrapper->Get();
	if (ImGui::SliderFloat(label.data(), &fValue, rConfig.fMin, rConfig.fMax))
	{
		rConfig.pWrapper->Set(fValue);
	}
	if (ImGui::IsItemActive())
	{
		// Capture position when slider first becomes active
		if (mpcActiveSlider == nullptr)
		{
			mActiveSliderPos = ImGui::GetItemRectMin();
			mActiveSliderWindowOffset = ImVec2(mActiveSliderPos.x - ImGui::GetWindowPos().x, mActiveSliderPos.y - ImGui::GetWindowPos().y);
		}
		mpcActiveSlider = label.data();
		miActiveSliderSection = iSection;
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

	// When active slider is in toggle bar, only render toggle bar
	if (mpcActiveSlider != nullptr && miActiveSliderSection == -1)
	{
		RenderToggleBar();
		ImGui::PopStyleVar(4);
		return;
	}

	// Render toggle bar unless active slider is in a section
	if (mpcActiveSlider == nullptr)
	{
		RenderToggleBar();
	}

	// Render visible section windows (or just the one with active slider)
	for (int i = 0; i < static_cast<int>(TweakSection::kCount); ++i)
	{
		if (mpcActiveSlider != nullptr)
		{
			// Only render section containing active slider
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
	// Create window first to set font scale before measuring text
	ImGui::SetNextWindowPos(ImVec2(0.0f, 10.0f), ImGuiCond_Always);
	ImGui::Begin("Tweaks", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize);
	ImGui::SetWindowFontScale(kfUiScale);

	// Find widest label (must be after font scale is set)
	float fMaxWidth = 0.0f;
	for (int i = 0; i < static_cast<int>(TweakSection::kCount); ++i)
	{
		float fWidth = ImGui::CalcTextSize(kpcSectionNames[i]).x;
		fMaxWidth = std::max(fMaxWidth, fWidth);
	}

	// Add frame padding for the selectable
	float fButtonWidth = fMaxWidth + ImGui::GetStyle().FramePadding.x * 2.0f;

	// Calculate total bar width
	float fTotalWidth = fButtonWidth * static_cast<int>(TweakSection::kCount)
	                  + ImGui::GetStyle().ItemSpacing.x * (static_cast<int>(TweakSection::kCount) - 1);

	// Position window: use captured position for active slider, otherwise center horizontally
	if (mpcActiveSlider != nullptr && miActiveSliderSection == -1)
	{
		ImGui::SetWindowPos(ImVec2(mActiveSliderPos.x - mActiveSliderWindowOffset.x, mActiveSliderPos.y - mActiveSliderWindowOffset.y));
	}
	else
	{
		ImGuiIO& rIo = ImGui::GetIO();
		float fCenterX = (rIo.DisplaySize.x - fTotalWidth) * 0.5f;
		ImGui::SetWindowPos(ImVec2(fCenterX, 10.0f));
	}

	// Skip toggle buttons when a slider is being dragged
	if (mpcActiveSlider == nullptr)
	{
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
	}

	// Sun angle slider spans full width
	ImGui::SetNextItemWidth(fTotalWidth);
	WrapperSlider("Sun Angle", -1);

	ImGui::End();
}

void TweaksScreen::RenderSectionWindow(TweakSection eSection)
{
	int iSection = static_cast<int>(eSection);

	// Position window: use captured position for active slider, otherwise use stored position
	if (mpcActiveSlider != nullptr && miActiveSliderSection == iSection)
	{
		ImGui::SetNextWindowPos(ImVec2(mActiveSliderPos.x - mActiveSliderWindowOffset.x, mActiveSliderPos.y - mActiveSliderWindowOffset.y), ImGuiCond_Always);
	}
	else
	{
		ImGui::SetNextWindowPos(mWindowPositions[iSection], ImGuiCond_FirstUseEver);
	}
	ImGui::Begin(kpcSectionNames[iSection], &mSectionVisible[iSection], ImGuiWindowFlags_AlwaysAutoResize);
	ImGui::SetWindowFontScale(kfUiScale);
	mWindowPositions[iSection] = ImGui::GetWindowPos();

	switch (eSection)
	{
		case TweakSection::kTest:          RenderTestSection(); break;
		case TweakSection::kGltf:          RenderGltfSection(); break;
		case TweakSection::kTerrain:       RenderTerrainSection(); break;
		case TweakSection::kWaterSpecular: RenderWaterSpecularSection(); break;
		case TweakSection::kWaterLow:      RenderWaterLowSection(); break;
		case TweakSection::kLighting:      RenderLightingSection(); break;
		case TweakSection::kWaterLighting: RenderWaterLightingSection(); break;
		case TweakSection::kShadow:        RenderShadowSection(); break;
		case TweakSection::kMisc:          RenderMiscSection(); break;
		case TweakSection::kHexShield:     RenderHexShieldSection(); break;
		case TweakSection::kSmoke:         RenderSmokeSection(); break;
		default: break;
	}

	ImGui::End();
}

void TweaksScreen::RenderTestSection()
{
	WrapperSlider("Test One", static_cast<int>(TweakSection::kTest));
	WrapperSlider("Test Two", static_cast<int>(TweakSection::kTest));
}

void TweaksScreen::RenderGltfSection()
{
	ImGui::SeparatorText("Tone Mapping");
	WrapperSlider("Exposure", static_cast<int>(TweakSection::kGltf));
	WrapperSlider("Gamma", static_cast<int>(TweakSection::kGltf));

	ImGui::SeparatorText("Lighting");
	WrapperSlider("Ambient (IBL)", static_cast<int>(TweakSection::kGltf));
	WrapperSlider("Diffuse", static_cast<int>(TweakSection::kGltf));
	WrapperSlider("Specular", static_cast<int>(TweakSection::kGltf));
	WrapperSlider("Smoke", static_cast<int>(TweakSection::kGltf));

	ImGui::SeparatorText("BRDF");
	WrapperSlider("BRDF", static_cast<int>(TweakSection::kGltf));
	WrapperSlider("BRDF Power", static_cast<int>(TweakSection::kGltf));

	ImGui::SeparatorText("IBL");
	WrapperSlider("IBL", static_cast<int>(TweakSection::kGltf));
	WrapperSlider("IBL Power", static_cast<int>(TweakSection::kGltf));

	ImGui::SeparatorText("Sun");
	WrapperSlider("Sun", static_cast<int>(TweakSection::kGltf));
	WrapperSlider("Sun Power", static_cast<int>(TweakSection::kGltf));

	ImGui::SeparatorText("Post Lighting");
	WrapperSlider("Lighting", static_cast<int>(TweakSection::kGltf));
	WrapperSlider("Lighting Power", static_cast<int>(TweakSection::kGltf));
}

void TweaksScreen::RenderTerrainSection()
{
	ImGui::SeparatorText("Beach");
	WrapperSlider("Snow Multiplier", static_cast<int>(TweakSection::kTerrain));
	WrapperSlider("Beach Height", static_cast<int>(TweakSection::kTerrain));
	WrapperSlider("Beach Sand Size", static_cast<int>(TweakSection::kTerrain));
	WrapperSlider("Beach Sand Blend", static_cast<int>(TweakSection::kTerrain));
	WrapperSlider("Beach Normals Size 1", static_cast<int>(TweakSection::kTerrain));
	WrapperSlider("Beach Normals Size 2", static_cast<int>(TweakSection::kTerrain));
	WrapperSlider("Beach Normals Size 3", static_cast<int>(TweakSection::kTerrain));
	WrapperSlider("Beach Normals Blend", static_cast<int>(TweakSection::kTerrain));

	ImGui::SeparatorText("Rock");
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
	ImGui::SeparatorText("Normals");
	WrapperSlider("Sampled Normals Size", static_cast<int>(TweakSection::kWaterSpecular));
	WrapperSlider("Sampled Normals Size Mod", static_cast<int>(TweakSection::kWaterSpecular));
	WrapperSlider("Sampled Normals Speed", static_cast<int>(TweakSection::kWaterSpecular));
	WrapperSlider("Depth Reflection Feather", static_cast<int>(TweakSection::kWaterSpecular));

	ImGui::SeparatorText("Skybox");
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

	ImGui::SeparatorText("Height Darken");
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

	ImGui::SeparatorText("Wave");
	WrapperSlider("Low Max", static_cast<int>(TweakSection::kWaterLow));
	WrapperSlider("Angle", static_cast<int>(TweakSection::kWaterLow));
	WrapperSlider("Wavelength", static_cast<int>(TweakSection::kWaterLow));
	WrapperSlider("Amplitude", static_cast<int>(TweakSection::kWaterLow));
	WrapperSlider("Speed", static_cast<int>(TweakSection::kWaterLow));
	WrapperSlider("Steepness", static_cast<int>(TweakSection::kWaterLow));

	ImGui::SeparatorText("Adjustments");
	WrapperSlider("Angle Adjust", static_cast<int>(TweakSection::kWaterLow));
	WrapperSlider("Wavelength Adjust", static_cast<int>(TweakSection::kWaterLow));
	WrapperSlider("Amplitude Adjust", static_cast<int>(TweakSection::kWaterLow));
	WrapperSlider("Speed Adjust", static_cast<int>(TweakSection::kWaterLow));

	ImGui::SeparatorText("Beach Fade");
	WrapperSlider("Beach Directional Fade Bottom", static_cast<int>(TweakSection::kWaterLow));
	WrapperSlider("Beach Directional Fade Height", static_cast<int>(TweakSection::kWaterLow));
}

void TweaksScreen::RenderLightingSection()
{
	ImGui::SeparatorText("Blur");
	WrapperSlider("Texture Multiplier", static_cast<int>(TweakSection::kLighting));
	WrapperSlider("Blur Distance", static_cast<int>(TweakSection::kLighting));
	WrapperSlider("Blur Directionality", static_cast<int>(TweakSection::kLighting));
	WrapperSlider("Blur Jitter", static_cast<int>(TweakSection::kLighting));
	WrapperSlider("Downscale", static_cast<int>(TweakSection::kLighting));

	ImGui::SeparatorText("Combine");
	WrapperSlider("Combine Index", static_cast<int>(TweakSection::kLighting));
	WrapperSlider("Blur First Divisor", static_cast<int>(TweakSection::kLighting));
	WrapperSlider("Blur Divisor", static_cast<int>(TweakSection::kLighting));
	WrapperSlider("Combine Decay", static_cast<int>(TweakSection::kLighting));
	WrapperSlider("Combine Power", static_cast<int>(TweakSection::kLighting));

	ImGui::SeparatorText("Directional");
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
	ImGui::SeparatorText("Specular");
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
	ImGui::SeparatorText("Feather");
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

	ImGui::SeparatorText("Object Shadows");
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
	ImGui::SeparatorText("Edge");
	WrapperSlider("Grow", static_cast<int>(TweakSection::kHexShield));
	WrapperSlider("Edge Distance", static_cast<int>(TweakSection::kHexShield));
	WrapperSlider("Edge Power", static_cast<int>(TweakSection::kHexShield));
	WrapperSlider("Edge Multiplier", static_cast<int>(TweakSection::kHexShield));

	ImGui::SeparatorText("Wave");
	WrapperSlider("Wave Multiplier", static_cast<int>(TweakSection::kHexShield));
	WrapperSlider("Wave Dot", static_cast<int>(TweakSection::kHexShield));
	WrapperSlider("Wave Intensity", static_cast<int>(TweakSection::kHexShield));
	WrapperSlider("Wave Intensity Power", static_cast<int>(TweakSection::kHexShield));
	WrapperSlider("Wave Falloff Power", static_cast<int>(TweakSection::kHexShield));

	ImGui::SeparatorText("Direction");
	WrapperSlider("Direction Falloff Power", static_cast<int>(TweakSection::kHexShield));
	WrapperSlider("Direction Multiplier", static_cast<int>(TweakSection::kHexShield));
}

void TweaksScreen::RenderSmokeSection()
{
	ImGui::SeparatorText("Decay");
	WrapperSlider("Smoke Max", static_cast<int>(TweakSection::kSmoke));
	WrapperSlider("Smoke Power", static_cast<int>(TweakSection::kSmoke));
	WrapperSlider("Smoke Decay", static_cast<int>(TweakSection::kSmoke));
	WrapperSlider("Smoke Decay Extra", static_cast<int>(TweakSection::kSmoke));
	WrapperSlider("Smoke Decay Extra Threshold", static_cast<int>(TweakSection::kSmoke));
	WrapperSlider("Smoke Edge Decay Distance", static_cast<int>(TweakSection::kSmoke));

	ImGui::SeparatorText("Color");
	WrapperSlider("Smoke Color Min", static_cast<int>(TweakSection::kSmoke));
	WrapperSlider("Smoke Color Multiplier", static_cast<int>(TweakSection::kSmoke));
	WrapperSlider("Smoke Trails Falloff", static_cast<int>(TweakSection::kSmoke));

	ImGui::SeparatorText("Wind/Noise");
	WrapperSlider("Smoke Wind Noise Scale", static_cast<int>(TweakSection::kSmoke));
	WrapperSlider("Smoke Wind Noise Quantity", static_cast<int>(TweakSection::kSmoke));
	WrapperSlider("Smoke Noise Quantity", static_cast<int>(TweakSection::kSmoke));
	WrapperSlider("Smoke Noise Scale One", static_cast<int>(TweakSection::kSmoke));
	WrapperSlider("Smoke Noise Scale Two", static_cast<int>(TweakSection::kSmoke));
}

} // namespace engine
