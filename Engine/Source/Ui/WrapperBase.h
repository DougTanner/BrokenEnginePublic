#pragma once

namespace engine
{

class Wrapper
{
public:

	Wrapper() = delete;

	explicit Wrapper(float fValue, float fMin, float fMax)
	: mfDefault(fValue)
	, mfMin(fMin)
	, mfMax(fMax)
	, mfCurrent(mfDefault)
	, mfPrevious(mfCurrent)
	{
		ASSERT(mfMin != mfMax);
		ASSERT(fValue >= fMin && fValue <= fMax);
	}

	explicit Wrapper(bool bValue)
	: mfDefault(bValue ? 1.0f : 0.0f)
	, mfMin(0.0f)
	, mfMax(1.0f)
	, mfCurrent(mfDefault)
	, mfPrevious(mfCurrent)
	{
	}

	template <typename T>
	Wrapper(T value, const std::vector<T>& rAllowedValues)
	: mfDefault(static_cast<float>(value))
	, mfMin(0.0f)
	, mfMax(1.0f)
	, mfCurrent(mfDefault)
	, mfPrevious(mfCurrent)
	{
		mAllowed.reserve(rAllowedValues.size());
		for (const T& rValue : rAllowedValues)
		{
			mAllowed.push_back(static_cast<float>(rValue));
			mfMax = std::max(static_cast<float>(rValue), mfMax);
		}

		ASSERT(mfMin != mfMax);
	}

	~Wrapper() = default;

	template <typename T>
	std::tuple<T, T, bool> Changed()
	{
		std::tuple<T, T, bool> values = std::make_tuple(static_cast<T>(mfCurrent), static_cast<T>(mfPrevious), mfPrevious != mfCurrent);
		mfPrevious = mfCurrent;
		return values;
	}

	void Toggle()
	{
		mfCurrent = mfCurrent == 0.0f ? 1.0f : 0.0f;
	}

	float Get() const
	{
		return mfCurrent;
	}

	float GetMin() const
	{
		return mfMin;
	}

	float GetMax() const
	{
		return mfMax;
	}

	float GetDefault() const
	{
		return mfDefault;
	}

	template <typename T>
	T Get() const
	{
		static_assert(!std::is_same_v<T, float>);

		if constexpr (std::is_same_v<T, bool>)
		{
			return mfCurrent == 1.0f;
		}
		else
		{
			return static_cast<T>(mfCurrent);
		}
	}

	template <typename T>
	T GetDefault() const
	{
		static_assert(!std::is_same_v<T, float>);

		if constexpr (std::is_same_v<T, bool>)
		{
			return mfDefault == 1.0f;
		}
		else
		{
			return static_cast<T>(mfDefault);
		}
	}

	void Set(float fValue)
	{
		mfCurrent = std::clamp(fValue, mfMin, mfMax);
	}

	void Set(bool bValue)
	{
		mfCurrent = bValue ? 1.0f : 0.0f;
	}

	template <typename T>
	void Set(T value)
	{
		static_assert(!std::is_same_v<T, float> && !std::is_same_v<T, bool>);

		mfCurrent = static_cast<float>(value);
		GetIndex();
	}

	template <typename T>
	void operator=(T value) = delete;

	void Reset(float fValue)
	{
		mfCurrent = mfPrevious = fValue;
	}

	template <typename T>
	void Reset(T value)
	{
		static_assert(!std::is_same_v<T, float> && !std::is_same_v<T, bool>);

		mfCurrent = mfPrevious = static_cast<float>(value);
		GetIndex();
	}

	void ResetToDefault()
	{
		mfCurrent = mfDefault;
	}

	float Percent() const
	{
		return (Get() - mfMin) / (mfMax - mfMin);
	}

	void SetPercent(float fPercent)
	{
		Set(mfMin + fPercent * (mfMax - mfMin));
	}

	int64_t GetIndex() const
	{
		int64_t iIndex = 0;
		for (const float& rfValue : mAllowed)
		{
			if (Get() == rfValue)
			{
				return iIndex;
			}

			++iIndex;
		}

		DEBUG_BREAK();
		return 0;
	}

	void SetIndex(int64_t iIndex)
	{
		Set(mAllowed.at(iIndex));
	}

private:

	float mfDefault = 0.0f;
	float mfMin = 0.0f;
	float mfMax = 1.0f;

	float mfCurrent = 0.0f;
	float mfPrevious = 0.0f;
	std::vector<float> mAllowed;
};

extern Wrapper gFullscreen;
extern Wrapper gPresentMode;
extern Wrapper gMultisampling;
extern Wrapper gSampleCount;
extern Wrapper gAnisotropy;
extern Wrapper gMaxAnisotropy;
extern Wrapper gSampleShading;
extern Wrapper gMinSampleShading;
extern Wrapper gMipLodBias;
extern Wrapper gFov;
extern Wrapper gWireframe;

extern Wrapper gWorldDetail;
extern Wrapper gTerrainElevationTextureMultiplier;
extern Wrapper gTerrainColorTextureMultiplier;
extern Wrapper gTerrainNormalTextureMultiplier;
extern Wrapper gTerrainAmbientOcclusionTextureMultiplier;
extern Wrapper gSmoke;
extern Wrapper gSmokeSimulationPixels;
extern Wrapper gSmokeSimulationArea;

extern Wrapper gSunAngleOverride;
inline constexpr float kfDefaultMinimumAmbient = 0.03f;
extern Wrapper gMinimumAmbient;

extern Wrapper gBaseHeight;

// Pbr - Engine Variables
extern Wrapper gPbrDayBrightness;
extern Wrapper gPbrSun;
extern Wrapper gPbrSunPower;
// Pbr - BRDF
extern Wrapper gPbrBrdfDiffuse;
extern Wrapper gPbrBrdfDiffusePower;
extern Wrapper gPbrBrdfSpecular;
extern Wrapper gPbrBrdfSpecularPower;
// Pbr - Tone Mapping
extern Wrapper gPbrExposure;
extern Wrapper gPbrGamma;
// Pbr - IBL
extern Wrapper gPbrIblAmbient;
extern Wrapper gPbrIblDiffuse;
extern Wrapper gPbrIblDiffusePower;
extern Wrapper gPbrIblSpecular;
extern Wrapper gPbrIblSpecularPower;
extern Wrapper gPbrIblShadowBlend;
extern Wrapper gPbrIblAmbientColorBlend;
extern Wrapper gPbrCubemapLodPower;
extern Wrapper gPbrCubemapLodOffset;
extern Wrapper gPbrShadowFloor;
// Pbr - Post Lighting
extern Wrapper gPbrLightingSpecular;
extern Wrapper gPbrLightingSpecularPower;
extern Wrapper gPbrLighting;
extern Wrapper gPbrLightingPower;
// Pbr - Smoke
extern Wrapper gPbrSmoke;
// Pbr - Emissive
extern Wrapper gPbrEmissive;

// Sound
extern Wrapper gMasterVolume;
extern Wrapper gMusicVolume;
extern Wrapper gSoundVolume;

// Islands & terrain
extern Wrapper gVisibleAreaExtraTop;
extern Wrapper gVisibleAreaExtraBottom;
extern Wrapper gIslandHeight;
extern Wrapper gWaterDepth;
extern Wrapper gIslandAmbientOcclusion;

extern Wrapper gTerrainEarlyOut;
extern Wrapper gWaterEarlyOut;

extern Wrapper gTerrainRockMultiplier;
extern Wrapper gTerrainRockSize;
extern Wrapper gTerrainRockBlend;
extern Wrapper gTerrainRockNormalsSizeOne;
extern Wrapper gTerrainRockNormalsSizeTwo;
extern Wrapper gTerrainRockNormalsSizeThree;
extern Wrapper gTerrainRockNormalsBlend;

extern Wrapper gTerrainSnowMultiplier;

extern Wrapper gTerrainBeachHeight;
extern Wrapper gTerrainBeachSandSize;
extern Wrapper gTerrainBeachSandBlend;
extern Wrapper gTerrainBeachNormalsSizeOne;
extern Wrapper gTerrainBeachNormalsSizeTwo;
extern Wrapper gTerrainBeachNormalsSizeThree;
extern Wrapper gTerrainBeachNormalsBlend;

// Water
extern Wrapper gWaterHeight;
extern Wrapper gWaterTerrainHeight;
extern Wrapper gWaterTerrainFade;
extern Wrapper gWaterTerrainFadeClamp;
extern Wrapper gWaterNoiseFrequency;
extern Wrapper gWaterNoiseAmount;
extern Wrapper gWaterColorNoiseFrequency;
extern Wrapper gWaterColorNoiseAmount;

extern Wrapper gWaterDepthLutFeather;
extern Wrapper gWaterDepthColorFeather;
extern Wrapper gWaterDepthReflectionFeather;
extern Wrapper gWaterFresnel;
extern Wrapper gWaterFresnel2;
extern Wrapper gWaterColorBottom;
extern Wrapper gWaterColorHeight;

extern Wrapper gWaterHeightDarkenTop;
extern Wrapper gWaterHeightDarkenBottom;
extern Wrapper gWaterHeightDarkenClamp;

// Lighting (section order matches tweaks screen layout)
// Pre-Blur
extern Wrapper gLightingBlurSigma;
extern Wrapper gLightingBlurSampleCount;
extern Wrapper gLightingBlurEdgeFalloff;

// Deposit
extern Wrapper gLightingDepositTextureMultiplier;

// Spread - Pixel Multiplier
extern Wrapper gSpreadTextureMultiplier;
extern Wrapper gSpreadPassCount;

// Spread Start
extern Wrapper gSpreadDirectionality;
extern Wrapper gSpreadDirectionCount;
extern Wrapper gSpreadDistance;
extern Wrapper gSpreadRingCount;
extern Wrapper gSpreadJitter;
extern Wrapper gSpreadDecay;
extern Wrapper gSpreadAccumulationDecay;

// Spread End (interpolation targets for last spread pass)
extern Wrapper gSpreadDirectionalityEnd;
extern Wrapper gSpreadDirectionCountEnd;
extern Wrapper gSpreadDistanceEnd;
extern Wrapper gSpreadRingCountEnd;
extern Wrapper gSpreadJitterEnd;
extern Wrapper gSpreadDecayEnd;
extern Wrapper gSpreadAccumulationDecayEnd;

// Spread Height
extern Wrapper gSpreadHeightDistance;
extern Wrapper gSpreadHeightIntensity;
extern Wrapper gSpreadHeightIntensityTarget;

// Combine
extern Wrapper gCombineExposure;
extern Wrapper gCombinePower;
extern Wrapper gCombinePassNormalize;
extern Wrapper gCombineExposurePassScale;
extern Wrapper gLightingSampledNormalsSize;
extern Wrapper gLightingSampledNormalsSizeMod;
extern Wrapper gLightingSampledNormalsSpeed;

// New Lighting
extern Wrapper gLightingNewDirectional;
extern Wrapper gLightingNewDirectionalPower;
extern Wrapper gLightingNewAmbient;
extern Wrapper gLightingNewAmbientPower;
extern Wrapper gLightingTerrain;
extern Wrapper gLightingAddTerrain;
extern Wrapper gLightingTerrainBelowBaseMultiplier;
extern Wrapper gLightingTerrainBelowBasePower;
extern Wrapper gLightingObjects;
extern Wrapper gLightingObjectsAdd;
extern Wrapper gLightingTimeOfDayMultiplier;

// Water specular lighting
extern Wrapper gLightingWaterNormalSoften;
extern Wrapper gLightingWaterNormalBlendWave;
extern Wrapper gLightingWaterIntensity;
extern Wrapper gLightingWaterAdd;
extern Wrapper gLightingWaterOne;
extern Wrapper gLightingWaterOnePower;
extern Wrapper gLightingWaterTwo;
extern Wrapper gLightingWaterTwoPower;
extern Wrapper gLightingWaterThree;
extern Wrapper gLightingWaterThreePower;

// Water skybox
extern Wrapper gLightingWaterSkyboxSunBias;
extern Wrapper gLightingWaterSkyboxNormalSoften;
extern Wrapper gLightingWaterSkyboxNormalBlendWave;
extern Wrapper gLightingWaterSkyboxIntensity;
extern Wrapper gLightingWaterSkyboxAdd;
extern Wrapper gLightingWaterSkyboxOne;
extern Wrapper gLightingWaterSkyboxOnePower;
extern Wrapper gLightingWaterSkyboxTwo;
extern Wrapper gLightingWaterSkyboxTwoPower;
extern Wrapper gLightingWaterSkyboxThree;
extern Wrapper gLightingWaterSkyboxThreePower;
extern Wrapper gLightingWaterSkyboxLod;

// Smoke
extern Wrapper gSmokeDecay;
extern Wrapper gSmokeEdgeDecayDistance;
extern Wrapper gSmokeTrailsQuantity;
extern Wrapper gSmokeTrailsWidthCurrent;
extern Wrapper gSmokeTrailsWidthPrevious;
extern Wrapper gSmokeTrailsLength;
extern Wrapper gSmokeTrailsLengthJitter;
extern Wrapper gSmokeTrailsSideJitter;
extern Wrapper gSmokeIntensityFalloff;
extern Wrapper gSmokeTrailsFollow;

extern Wrapper gSmokeWindNoiseScale;
extern Wrapper gSmokeWindNoiseQuantity;
extern Wrapper gSmokeNoiseScaleOne;
extern Wrapper gSmokeNoiseScaleTwo;
extern Wrapper gSmokeNoiseQuantity;
extern Wrapper gSmokeNoiseInfluence;
extern Wrapper gSmokeMax;
extern Wrapper gSmokePower;
extern Wrapper gSmokeColorMin;
extern Wrapper gSmokeColorMultiplier;
extern Wrapper gSmokeTrailPower;
extern Wrapper gSmokeTrailAlpha;
extern Wrapper gSmokeObjectHeight;

// Wind - Time & Global
extern Wrapper gWind;
extern Wrapper gWindTimeScale;
extern Wrapper gWindThresholdLow;
extern Wrapper gWindThresholdHigh;
// Wind - Propagation
extern Wrapper gWindAdvectionScaleHigh;
extern Wrapper gWindAdvectionScaleLow;
extern Wrapper gWindSwirlScaleHigh;
extern Wrapper gWindSwirlScaleLow;
extern Wrapper gWindSwirlAmountHigh;
extern Wrapper gWindSwirlAmountLow;
extern Wrapper gWindSwirlSpeedHigh;
extern Wrapper gWindSwirlSpeedLow;
extern Wrapper gWindVorticityConfinementHigh;
extern Wrapper gWindVorticityConfinementLow;
extern Wrapper gWindDecayHigh;
extern Wrapper gWindDecayLow;
extern Wrapper gWindMomentumHigh;
extern Wrapper gWindMomentumLow;
extern Wrapper gWindDiffusionHigh;
extern Wrapper gWindDiffusionLow;
// Wind - Integration
extern Wrapper gWindToSmokeStrength;
extern Wrapper gWindSmokeRetention;
extern Wrapper gWindToSmokePower;
// Wind - Displacement
extern Wrapper gWindDisplacementNoiseScale;
extern Wrapper gWindSmokeAdvection;
// Low frequency waves
extern Wrapper gLowCount;
extern Wrapper gLowMax;
extern Wrapper gLowAngle;
extern Wrapper gLowWavelength;
extern Wrapper gLowAmplitude;
extern Wrapper gLowSpeed;
extern Wrapper gLowSteepness;

extern Wrapper gLowAngleAdjust;
extern Wrapper gLowWavelengthAdjust;
extern Wrapper gLowAmplitudeAdjust;
extern Wrapper gLowSpeedAdjust;

// Medium frequency waves
extern Wrapper gMediumCount;
extern Wrapper gMediumWavelength;
extern Wrapper gMediumAmplitude;
extern Wrapper gMediumSpeed;
extern Wrapper gMediumSteepness;

extern Wrapper gMediumAngleAdjust;
extern Wrapper gMediumWavelengthAdjust;
extern Wrapper gMediumAmplitudeAdjust;
extern Wrapper gMediumSpeedAdjust;

// High frequency waves & Water
extern Wrapper gHighMultiplier;
extern Wrapper gHighScaleOne;
extern Wrapper gHighScaleTwo;

extern Wrapper gBeachDirectionalFadeBottom;
extern Wrapper gBeachDirectionalFadeHeight;

// Shadow
extern Wrapper gShadowFeatherNoon;
extern Wrapper gShadowFeatherNoonOffset;
extern Wrapper gShadowFeatherSunset;
extern Wrapper gShadowFeatherSunsetOffset;
extern Wrapper gShadowFeatherPower;
extern Wrapper gShadowDistanceFallof;
extern Wrapper gShadowBlurSigma;
extern Wrapper gShadowAffectAmbient;
extern Wrapper gShadowHeightFadeTop;
extern Wrapper gShadowHeightFadeBottom;

extern Wrapper gObjectShadowsRenderMultiplier;
extern Wrapper gObjectShadowsBlurMultiplier;
extern Wrapper gObjectShadowsNoon;
extern Wrapper gObjectShadowsSunset;
extern Wrapper gObjectShadowsSunsetStretch;
extern Wrapper gObjectShadowsBlurDistanceNoon;
extern Wrapper gObjectShadowsBlurDistanceSunset;
extern Wrapper gObjectShadowsBlurSigma;

extern Wrapper gSmokeShadowIntensity;

// Particles
extern Wrapper gParticlesWindStrength;

// Debug
extern Wrapper gDebugTexture;
extern Wrapper gDebugTextureIndex;
extern Wrapper gDebugTextureLinearRange;

// Test
extern Wrapper gTestOne;
extern Wrapper gTestTwo;

} // namespace engine
