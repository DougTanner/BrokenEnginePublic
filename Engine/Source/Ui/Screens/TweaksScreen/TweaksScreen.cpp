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
	// Heap: static unordered_map built once on first call, lives forever. Can't use workbuffer (data lost on Pop)
	// and can't pre-allocate (STL map manages its own hash buckets internally)
	ScopedSuppressAllocationTracking suppressAllocationTracking;

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
		{"Smoke Intensity Falloff", &gSmokeIntensityFalloff},
		{"Smoke Trails Quantity", &gSmokeTrailsQuantity},
		{"Smoke Trails Width Current", &gSmokeTrailsWidthCurrent},
		{"Smoke Trails Width Previous", &gSmokeTrailsWidthPrevious},
		{"Smoke Trails Length", &gSmokeTrailsLength},
		{"Smoke Trails Length Jitter", &gSmokeTrailsLengthJitter},
		{"Smoke Trails Side Jitter", &gSmokeTrailsSideJitter},
		{"Smoke Trails Follow", &gSmokeTrailsFollow},
		// Smoke - Noise
		{"Smoke Noise Scale One", &gSmokeNoiseScaleOne},
		{"Smoke Noise Scale Two", &gSmokeNoiseScaleTwo},
		{"Smoke Wind Noise Scale", &gSmokeWindNoiseScale},
		{"Smoke Noise Quantity", &gSmokeNoiseQuantity},
		{"Smoke Wind Noise Quantity", &gSmokeWindNoiseQuantity},
		{"Smoke Object Height", &gSmokeObjectHeight},
		// Smoke - Wind Displacement
		{"Wind To Smoke Strength", &gWindToSmokeStrength},
		{"Wind To Smoke Power", &gWindToSmokePower},
		{"Wind Displacement Noise Scale", &gWindDisplacementNoiseScale},
		{"Wind Displacement Swirl Scale", &gWindDisplacementSwirlScale},
		{"Wind Displacement Swirl Power", &gWindDisplacementSwirlPower},
		{"Wind Smoke Retention", &gWindSmokeRetention},
		// Wind - Time & Global
		{"Wind Time Scale", &gWindTimeScale},
		{"Wind Threshold Low", &gWindThresholdLow},
		{"Wind Threshold High", &gWindThresholdHigh},
		// Wind - Propagation
		{"Wind Advection Scale High", &gWindAdvectionScaleHigh},
		{"Wind Advection Scale Low", &gWindAdvectionScaleLow},
		{"Wind Swirl Scale High", &gWindSwirlScaleHigh},
		{"Wind Swirl Scale Low", &gWindSwirlScaleLow},
		{"Wind Swirl Amount High", &gWindSwirlAmountHigh},
		{"Wind Swirl Amount Low", &gWindSwirlAmountLow},
		{"Wind Swirl Speed High", &gWindSwirlSpeedHigh},
		{"Wind Swirl Speed Low", &gWindSwirlSpeedLow},
		{"Wind Vorticity Confinement High", &gWindVorticityConfinementHigh},
		{"Wind Vorticity Confinement Low", &gWindVorticityConfinementLow},
		{"Wind Decay High", &gWindDecayHigh},
		{"Wind Decay Low", &gWindDecayLow},
		{"Wind Momentum High", &gWindMomentumHigh},
		{"Wind Momentum Low", &gWindMomentumLow},
		{"Wind Diffusion High", &gWindDiffusionHigh},
		{"Wind Diffusion Low", &gWindDiffusionLow},
		// Wind - Particles
		{"Particles Wind Strength", &gParticlesWindStrength},
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

} // namespace engine
