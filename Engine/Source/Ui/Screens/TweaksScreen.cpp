#include "TweaksScreen.h"

#include "Game.h"

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
	"Lighting",
	"Water Lighting",
	"Shadow",
	"Misc",
	"Hex Shield",
	"Smoke",
	"Wind",
	"Wind Dep",
};
static_assert(std::size(kpcSectionNames) == static_cast<size_t>(TweakSection::kCount));

using RenderSectionFunc = void (TweaksScreen::*)();
static constexpr RenderSectionFunc kRenderSectionFunctions[] =
{
	&TweaksScreen::RenderTestSection,
	&TweaksScreen::RenderPbrSection,
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
	&TweaksScreen::RenderWindSection,
	&TweaksScreen::RenderWindDepositsSection,
};
static_assert(std::size(kRenderSectionFunctions) == static_cast<size_t>(TweakSection::kCount));

// UI scale factor for TweaksScreen
static constexpr float kfUiScale = 1.5f;

// Slider lookup map for active slider rendering
static std::unordered_map<std::string_view, Wrapper*>& GetSliderMap()
{
	ScopedSuppressAllocationTracking suppressTracking;

	static std::unordered_map<std::string_view, Wrapper*> sSliderMap =
	{
		// Test
		{"Test One", &gTestOne},
		{"Test Two", &gTestTwo},
		// Pbr - Tone Mapping
		{"Exposure", &gPbrExposure},
		{"Gamma", &gPbrGamma},
		// Pbr - BRDF
		{"BRDF Diffuse", &gPbrBrdfDiffuse},
		{"BRDF Diffuse Power", &gPbrBrdfDiffusePower},
		{"BRDF Specular", &gPbrBrdfSpecular},
		{"BRDF Specular Power", &gPbrBrdfSpecularPower},
		{"IBL Ambient", &gPbrIblAmbient},
		{"IBL Diffuse", &gPbrIblDiffuse},
		{"IBL Diffuse Power", &gPbrIblDiffusePower},
		{"IBL Specular", &gPbrIblSpecular},
		{"IBL Specular Power", &gPbrIblSpecularPower},
		{"IBL Shadow Blend", &gPbrIblShadowBlend},
		{"IBL Ambient Color Blend", &gPbrIblAmbientColorBlend},
		{"Cubemap Lod Power", &gPbrCubemapLodPower},
		{"Cubemap Lod Offset", &gPbrCubemapLodOffset},
		{"Shadow Floor", &gPbrShadowFloor},
		// Pbr - Sun
		{"Day Brightness", &gPbrDayBrightness},
		{"Sun", &gPbrSun},
		{"Sun Power", &gPbrSunPower},
		// Pbr - Post Lighting
		{"Lighting Specular", &gPbrLightingSpecular},
		{"Lighting Specular Power", &gPbrLightingSpecularPower},
		{"Lighting", &gPbrLighting},
		{"Lighting Power", &gPbrLightingPower},
		// Pbr - Smoke
		{"Smoke", &gPbrSmoke},
		// Pbr - Emissive
		{"Emissive", &gPbrEmissive},
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
		{"Skybox Lod", &gLightingWaterSkyboxLod},
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
		{"Smoke Object Height", &gSmokeObjectHeight},
		// Smoke - Wind Mask
		{"Smoke Wind Mask Strength", &gSmokeWindMaskStrength},
		{"Smoke Wind Mask Scale", &gSmokeWindMaskScale},
		// Wind - Time & Global
		{"Wind Time Scale", &gWindTimeScale},
		// Wind - Propagation
		{"Wind Advection Scale", &gWindAdvectionScale},
		{"Wind Swirl Scale", &gWindSwirlScale},
		{"Wind Swirl Amount", &gWindSwirlAmount},
		{"Wind Decay High", &gWindDecayHigh},
		{"Wind Decay Low", &gWindDecayLow},
		{"Wind Momentum High", &gWindMomentumHigh},
		{"Wind Momentum Low", &gWindMomentumLow},
		{"Wind Threshold Low", &gWindThresholdLow},
		{"Wind Threshold High", &gWindThresholdHigh},
		{"Wind Threshold Power", &gWindThresholdPower},
		{"Wind Diffusion", &gWindDiffusion},
		// Wind - Integration
		{"Wind To Smoke Strength", &gWindToSmokeStrength},
		{"Wind Smoke Retention", &gWindSmokeRetention},
		{"Wind To Smoke Power", &gWindToSmokePower},
		// Wind - Deposit (Missiles)
		{"Wind Deposit Width", &gWindDepositWidth},
		{"Wind Deposit Intensity", &gWindDepositIntensity},
		{"Wind Deposit Length Multiplier", &gWindDepositLengthMultiplier},
		// Wind - Deposit (Player)
		{"Player Deposit Width", &gWindDepositPlayerWidth},
		{"Player Deposit Intensity", &gWindDepositPlayerIntensity},
		{"Player Deposit Length Multiplier", &gWindDepositPlayerLengthMultiplier},
		// Wind - Deposit (Spaceships)
		{"Spaceships Deposit Width", &gWindDepositSpaceshipsWidth},
		{"Spaceships Deposit Intensity", &gWindDepositSpaceshipsIntensity},
		{"Spaceships Deposit Length Multiplier", &gWindDepositSpaceshipsLengthMultiplier},
		// Wind - Deposit (Player Blasters)
		{"Player Blasters Deposit Width", &gWindDepositPlayerBlastersWidth},
		{"Player Blasters Deposit Intensity", &gWindDepositPlayerBlastersIntensity},
		{"Blasters Deposit Length Multiplier", &gWindDepositBlastersLengthMultiplier},
		// Wind - Deposit (Spaceships Blasters)
		{"Spaceships Blasters Deposit Width", &gWindDepositSpaceshipsBlastersWidth},
		{"Spaceships Blasters Deposit Intensity", &gWindDepositSpaceshipsBlastersIntensity},
		// Wind - Deposit (Explosions)
		{"Explosions Deposit Width", &gWindDepositExplosionsWidth},
		{"Explosions Deposit Intensity", &gWindDepositExplosionsIntensity},
	};
	return sSliderMap;
}

TweaksScreen::TweaksScreen()
{
	// Initialize staggered window positions (Y set to 0, will use mfToggleBarBottom at runtime)
	constexpr float kfStartX = 10.0f;
	constexpr float kfOffsetX = 30.0f;

	for (size_t i = 0; i < static_cast<size_t>(TweakSection::kCount); ++i)
	{
		mpWindowPositions[i] = ImVec2(kfStartX + i * kfOffsetX, 0.0f);
	}
}

void TweaksScreen::WrapperSlider(std::string_view label, int iSection, float fWidthMultiplier)
{
	std::unordered_map<std::string_view, Wrapper*>& rSliderMap = GetSliderMap();
	auto it = rSliderMap.find(label);
	if (it == rSliderMap.end())
	{
		return;
	}

	// Render non-active sliders with alpha=0 to preserve layout
	bool bIsActiveSlider = (mActiveSlider.empty() || label == mActiveSlider);
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
	if (ImGui::SliderFloat(label.data(), &fValue, pWrapper->GetMin(), pWrapper->GetMax(), "%.6f"))
	{
		pWrapper->Set(fValue);
	}
	if (ImGui::IsItemActive())
	{
		mActiveSlider = label.data();
		miActiveSliderSection = iSection;
	}

	if (!bIsActiveSlider)
	{
		ImGui::PopStyleVar();
	}
}

void TweaksScreen::Render()
{
	if constexpr (kbEnableDebugInput)
	{
		if (!game::gpGame->mbShowImGui)
		{
			return;
		}

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
		for (int i = 0; i < static_cast<int>(TweakSection::kCount); ++i)
		{
			if (!mActiveSlider.empty())
			{
				if (i == miActiveSliderSection)
				{
					RenderSectionWindow(static_cast<TweakSection>(i));
				}
			}
			else if (mpSectionVisible[i])
			{
				RenderSectionWindow(static_cast<TweakSection>(i));
			}
		}

		ImGui::PopStyleVar(4);
	}
}

void TweaksScreen::RenderToggleBar()
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
		if (ImGui::Selectable(kpcSectionNames[i], mpSectionVisible[i], 0, ImVec2(fButtonWidth, 0.0f)))
		{
			mpSectionVisible[i] = !mpSectionVisible[i];
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

void TweaksScreen::WrapperSeparatorText(std::string_view label)
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

void TweaksScreen::RenderSectionWindow(TweakSection eSection)
{
	int iSection = static_cast<int>(eSection);
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
	ImVec2 initialPos(kfStartX, mfToggleBarBottom);
	ImGui::SetNextWindowPos(initialPos, ImGuiCond_FirstUseEver);
	ImGui::Begin(kpcSectionNames[iSection], bHasActiveSlider ? nullptr : &mpSectionVisible[iSection], ImGuiWindowFlags_AlwaysAutoResize);
	ImGui::SetWindowFontScale(kfUiScale);
	mpWindowPositions[iSection] = ImGui::GetWindowPos();

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

void TweaksScreen::RenderPbrSection()
{
	static constexpr int kiSection = static_cast<int>(TweakSection::kModel);

	if (ImGui::BeginTable("PbrColumns", 2))
	{
		// Left column
		ImGui::TableNextColumn();

		WrapperSeparatorText("Engine Variables");
		WrapperSlider("Sun", kiSection, 1.0f);

		WrapperSeparatorText("BRDF");
		WrapperSlider("BRDF Diffuse", kiSection, 1.0f);
		WrapperSlider("BRDF Diffuse Power", kiSection, 1.0f);
		WrapperSlider("BRDF Specular", kiSection, 1.0f);
		WrapperSlider("BRDF Specular Power", kiSection, 1.0f);

		WrapperSeparatorText("Tone Mapping");
		WrapperSlider("Exposure", kiSection, 1.0f);
		WrapperSlider("Gamma", kiSection, 1.0f);

		WrapperSeparatorText("Post Lighting");
		WrapperSlider("Lighting Specular", kiSection, 1.0f);
		WrapperSlider("Lighting Specular Power", kiSection, 1.0f);
		WrapperSlider("Lighting", kiSection, 1.0f);
		WrapperSlider("Lighting Power", kiSection, 1.0f);

		// Right column
		ImGui::TableNextColumn();

		WrapperSeparatorText("IBL");
		WrapperSlider("IBL Ambient", kiSection, 1.0f);
		WrapperSlider("IBL Diffuse", kiSection, 1.0f);
		WrapperSlider("IBL Diffuse Power", kiSection, 1.0f);
		WrapperSlider("IBL Specular", kiSection, 1.0f);
		WrapperSlider("IBL Specular Power", kiSection, 1.0f);
		WrapperSlider("IBL Shadow Blend", kiSection, 1.0f);
		WrapperSlider("IBL Ambient Color Blend", kiSection, 1.0f);
		WrapperSlider("Cubemap Lod Power", kiSection, 1.0f);
		WrapperSlider("Cubemap Lod Offset", kiSection, 1.0f);
		WrapperSlider("Shadow Floor", kiSection, 1.0f);

		WrapperSeparatorText("Smoke");
		WrapperSlider("Smoke", kiSection, 1.0f);

		WrapperSeparatorText("Emissive");
		WrapperSlider("Emissive", kiSection, 1.0f);

		ImGui::EndTable();
	}
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
	WrapperSlider("Skybox Lod", static_cast<int>(TweakSection::kWaterSpecular));

	WrapperSeparatorText("Height Darken");
	WrapperSlider("Height Darken Top", static_cast<int>(TweakSection::kWaterSpecular));
	WrapperSlider("Height Darken Bottom", static_cast<int>(TweakSection::kWaterSpecular));
	WrapperSlider("Height Darken Clamp", static_cast<int>(TweakSection::kWaterSpecular));
}

void TweaksScreen::RenderWaterLowSection()
{
	// Radio buttons for wave count selection (skip when slider is active)
	if (mActiveSlider.empty())
	{
		ImGui::Text("Wave Count");
		ImGui::SameLine();
		int64_t iCurrent = gLowCount.Get<int64_t>();
		for (const std::pair<const char*, int64_t>& rPair : std::initializer_list<std::pair<const char*, int64_t>>{{"15", 15}, {"31", 31}, {"63", 63}, {"127", 127}, {"255", 255}})
		{
			if (ImGui::RadioButton(rPair.first, iCurrent == rPair.second))
			{
				gLowCount.Set(rPair.second);
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
	if (mActiveSlider.empty())
	{
		ImGui::Text("Wave Count");
		ImGui::SameLine();
		int64_t iCurrent = gMediumCount.Get<int64_t>();
		for (const std::pair<const char*, int64_t>& rPair : std::initializer_list<std::pair<const char*, int64_t>>{{"15", 15}, {"31", 31}, {"63", 63}, {"127", 127}, {"255", 255}})
		{
			if (ImGui::RadioButton(rPair.first, iCurrent == rPair.second))
			{
				gMediumCount.Set(rPair.second);
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
	WrapperSlider("Smoke Wind Mask Strength", static_cast<int>(TweakSection::kSmoke));
	WrapperSlider("Smoke Wind Mask Scale", static_cast<int>(TweakSection::kSmoke));
}

void TweaksScreen::RenderWindSection()
{
	static constexpr int kiSection = static_cast<int>(TweakSection::kWind);

	WrapperSeparatorText("Time & Global");
	WrapperSlider("Wind Time Scale", kiSection);

	WrapperSeparatorText("Propagation");
	WrapperSlider("Wind Advection Scale", kiSection);
	WrapperSlider("Wind Swirl Scale", kiSection);
	WrapperSlider("Wind Swirl Amount", kiSection);
	WrapperSlider("Wind Decay High", kiSection);
	WrapperSlider("Wind Decay Low", kiSection);
	WrapperSlider("Wind Momentum High", kiSection);
	WrapperSlider("Wind Momentum Low", kiSection);
	WrapperSlider("Wind Threshold Low", kiSection);
	WrapperSlider("Wind Threshold High", kiSection);
	WrapperSlider("Wind Threshold Power", kiSection);
	WrapperSlider("Wind Diffusion", kiSection);
}

void TweaksScreen::RenderWindDepositsSection()
{
	static constexpr int kiSection = static_cast<int>(TweakSection::kWindDeposits);

	WrapperSeparatorText("Integration");
	WrapperSlider("Wind To Smoke Strength", kiSection);
	WrapperSlider("Wind Smoke Retention", kiSection);
	WrapperSlider("Wind To Smoke Power", kiSection);

	WrapperSeparatorText("Missiles");
	WrapperSlider("Wind Deposit Width", kiSection);
	WrapperSlider("Wind Deposit Intensity", kiSection);
	WrapperSlider("Wind Deposit Length Multiplier", kiSection);

	WrapperSeparatorText("Player");
	WrapperSlider("Player Deposit Width", kiSection);
	WrapperSlider("Player Deposit Intensity", kiSection);
	WrapperSlider("Player Deposit Length Multiplier", kiSection);

	WrapperSeparatorText("Spaceships");
	WrapperSlider("Spaceships Deposit Width", kiSection);
	WrapperSlider("Spaceships Deposit Intensity", kiSection);
	WrapperSlider("Spaceships Deposit Length Multiplier", kiSection);

	WrapperSeparatorText("Player Blasters");
	WrapperSlider("Player Blasters Deposit Width", kiSection);
	WrapperSlider("Player Blasters Deposit Intensity", kiSection);
	WrapperSlider("Blasters Deposit Length Multiplier", kiSection);

	WrapperSeparatorText("Spaceships Blasters");
	WrapperSlider("Spaceships Blasters Deposit Width", kiSection);
	WrapperSlider("Spaceships Blasters Deposit Intensity", kiSection);

	WrapperSeparatorText("Explosions");
	WrapperSlider("Explosions Deposit Width", kiSection);
	WrapperSlider("Explosions Deposit Intensity", kiSection);
}

} // namespace engine
