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

	template<typename T>
	Wrapper(T value, const std::vector<T>& rAllowedValues)
	: mfDefault(static_cast<float>(value))
	, mfMin(0.0f)
	, mfMax(1.0f)
	, mfCurrent(mfDefault)
	, mfPrevious(mfCurrent)
	{
		for (const T& rValue : rAllowedValues)
		{
			mAllowed.push_back(static_cast<float>(rValue));
			mfMax = std::max(static_cast<float>(rValue), mfMax);
		}

		ASSERT(mfMin != mfMax);
	}

	~Wrapper() = default;

	template<typename T>
	std::tuple<T, T, bool> Changed()
	{
		auto values = std::make_tuple(static_cast<T>(mfCurrent), static_cast<T>(mfPrevious), mfPrevious != mfCurrent);
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

	template<typename T>
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

	template<typename T>
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

	template<typename T>
	void Set(T value)
	{
		static_assert(!std::is_same_v<T, float> && !std::is_same_v<T, bool>);

		mfCurrent = static_cast<float>(value);
		GetIndex();
	}

	template<typename T>
	void operator=(T value) = delete;

	void Reset(float fValue)
	{
		mfCurrent = mfPrevious = fValue;
	}

	template<typename T>
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
		// LOG("SetPercent: {} -> {}", fPercent, Get());
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

		common::DebugBreak();
		return 0;
	}

	void SetIndex(int64_t iIndex)
	{
		Set(mAllowed[iIndex]);
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

extern Wrapper gMisc0;

// Gltf - Engine Variables
extern Wrapper gGltfDayBrightness;
extern Wrapper gGltfSun;
extern Wrapper gGltfSunPower;
// Gltf - BRDF
extern Wrapper gGltfBrdfDiffuse;
extern Wrapper gGltfBrdfDiffusePower;
extern Wrapper gGltfBrdfSpecular;
extern Wrapper gGltfBrdfSpecularPower;
// Gltf - Tone Mapping
extern Wrapper gGltfExposure;
extern Wrapper gGltfGamma;
// Gltf - IBL
extern Wrapper gGltfIblAmbient;
extern Wrapper gGltfIblDiffuse;
extern Wrapper gGltfIblDiffusePower;
extern Wrapper gGltfIblSpecular;
extern Wrapper gGltfIblSpecularPower;
extern Wrapper gGltfIblShadowBlend;
extern Wrapper gGltfIblAmbientColorBlend;
extern Wrapper gGltfShadowFloor;
// Gltf - Post Lighting
extern Wrapper gGltfLightingSpecular;
extern Wrapper gGltfLightingSpecularPower;
extern Wrapper gGltfLighting;
extern Wrapper gGltfLightingPower;
// Gltf - Smoke
extern Wrapper gGltfSmoke;
// Gltf - Emissive
extern Wrapper gGltfEmissive;

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
extern Wrapper gWaterTerrainHeight;
extern Wrapper gWaterTerrainFade;
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

// Lighting
extern Wrapper gLightingTextureMultiplier;
extern Wrapper gLightingBlurDownscale;
extern Wrapper gLightingCombineIndex;
extern Wrapper gLightingBlurDistance;
extern Wrapper gLightingBlurDirectionality;
extern Wrapper gLightingBlurJitter;
extern Wrapper gLightingBlurFirstDivisor;
extern Wrapper gLightingBlurDivisor;
extern Wrapper gLightingCombineDecay;
extern Wrapper gLightingCombinePower;

extern Wrapper gLightingDirectional;
extern Wrapper gLightingIndirect;
extern Wrapper gLightingTerrain;
extern Wrapper gLightingAddTerrain;
extern Wrapper gLightingObjects;
extern Wrapper gLightingObjectsAdd;

extern Wrapper gLightingSampledNormalsSize;
extern Wrapper gLightingSampledNormalsSizeMod;
extern Wrapper gLightingSampledNormalsSpeed;

extern Wrapper gLightingTimeOfDayMultiplier;

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

// Water specular lighting
extern Wrapper gLightingWaterSpecularNormalSoften;
extern Wrapper gLightingWaterSpecularNormalBlendWave;

extern Wrapper gLightingWaterSpecularDiffuse;
extern Wrapper gLightingWaterSpecularDirect;
extern Wrapper gLightingWaterSpecular;
extern Wrapper gLightingWaterSpecularIntensity;
extern Wrapper gLightingWaterSpecularAdd;
extern Wrapper gLightingWaterSpecularOne;
extern Wrapper gLightingWaterSpecularOnePower;
extern Wrapper gLightingWaterSpecularTwo;
extern Wrapper gLightingWaterSpecularTwoPower;
extern Wrapper gLightingWaterSpecularThree;
extern Wrapper gLightingWaterSpecularThreePower;

// Smoke
extern Wrapper gSmokeDecay;
extern Wrapper gSmokeDecayExtra;
extern Wrapper gSmokeDecayExtraThreshold;
extern Wrapper gSmokeEdgeDecayDistance;
extern Wrapper gSmokeTrailsQuantity;
extern Wrapper gSmokeTrailsWidthCurrent;
extern Wrapper gSmokeTrailsWidthPrevious;
extern Wrapper gSmokeTrailsLength;
extern Wrapper gSmokeTrailsLengthJitter;
extern Wrapper gSmokeTrailsSideJitter;
extern Wrapper gSmokeTrailsFalloff;
extern Wrapper gSmokeTrailsFollow;

extern Wrapper gSmokeWindNoiseScale;
extern Wrapper gSmokeWindNoiseQuantity;
extern Wrapper gSmokeNoiseScaleOne;
extern Wrapper gSmokeNoiseScaleTwo;
extern Wrapper gSmokeNoiseQuantity;
extern Wrapper gSmokeMax;
extern Wrapper gSmokePower;
extern Wrapper gSmokeColorMin;
extern Wrapper gSmokeColorMultiplier;
extern Wrapper gSmokeTrailPower;
extern Wrapper gSmokeTrailAlpha;
extern Wrapper gSmokeObjectHeight;

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

extern Wrapper gSmokeShadowIntensity;

// Hex shield
extern Wrapper gHexShieldGrow;
extern Wrapper gHexShieldEdgeDistance;
extern Wrapper gHexShieldEdgePower;
extern Wrapper gHexShieldEdgeMultiplier;

extern Wrapper gHexShieldWaveMultiplier;
extern Wrapper gHexShieldWaveDotMultiplier;
extern Wrapper gHexShieldWaveIntensityMultiplier;
extern Wrapper gHexShieldWaveIntensityPower;
extern Wrapper gHexShieldWaveFalloffPower;

extern Wrapper gHexShieldDirectionFalloffPower;
extern Wrapper gHexShieldDirectionMultiplier;

// Test
extern Wrapper gTestOne;
extern Wrapper gTestTwo;

} // namespace engine
