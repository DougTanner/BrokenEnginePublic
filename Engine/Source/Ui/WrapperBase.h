#pragma once

#include "Graphics/Managers/InstanceManager.h"

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

	float GetDefault () const
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

		DEBUG_BREAK();
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

inline Wrapper gFullscreen(true);
inline Wrapper gPresentMode(VK_PRESENT_MODE_FIFO_KHR, std::move(std::vector<VkPresentModeKHR> {VK_PRESENT_MODE_IMMEDIATE_KHR, VK_PRESENT_MODE_MAILBOX_KHR, VK_PRESENT_MODE_FIFO_KHR}));
inline Wrapper gReduceInputLag(true);
inline Wrapper gMultisampling(true);
inline Wrapper gSampleCount(VK_SAMPLE_COUNT_2_BIT, std::move(std::vector<VkSampleCountFlagBits> {VK_SAMPLE_COUNT_2_BIT, VK_SAMPLE_COUNT_4_BIT, VK_SAMPLE_COUNT_8_BIT, VK_SAMPLE_COUNT_16_BIT, VK_SAMPLE_COUNT_32_BIT, VK_SAMPLE_COUNT_64_BIT}));
inline Wrapper gAnisotropy(true);
inline Wrapper gMaxAnisotropy(16.0f, 1.0f, 16.0f);
inline Wrapper gSampleShading(true);
inline Wrapper gMinSampleShading(0.6f, 0.0f, 1.0f);
inline Wrapper gMipLodBias(0.75f, 0.0f, 3.0f);
inline Wrapper gFov(45.0f, 25.0f, 110.0f);
inline Wrapper gWireframe(true);

// DT: GAMELOGIC
inline Wrapper gPrimaryHoldToggle(0i64, std::move(std::vector<int64_t> {0, 1}));
inline Wrapper gDashMouseDirection(0i64, std::move(std::vector<int64_t> {0, 1}));
inline Wrapper gDashGamepadDirection(1i64, std::move(std::vector<int64_t> {0, 1}));

inline Wrapper gWorldDetail(1.0f / 8.0f, std::move(std::vector<float> {1.0f / 16.0f, 1.0f / 8.0f, 1.0f / 4.0f})); // Max divisor must match Graphics::WorldDetail()
inline Wrapper gTerrainElevationTextureMultiplier(0.5f, 0.25f, 1.0f);
inline Wrapper gTerrainColorTextureMultiplier(2.0f, 1.0f, 3.0f);
inline Wrapper gTerrainNormalTextureMultiplier(2.0f, 1.0f, 3.0f);
inline Wrapper gTerrainAmbientOcclusionTextureMultiplier(0.5f, 0.25f, 1.0f);
inline Wrapper gSmoke(true);
inline Wrapper gSmokeSimulationPixels(1.0f, 0.5f, 1.5f);
inline Wrapper gSmokeSimulationArea(1.0f, 0.8f, 1.2f);

inline Wrapper gSunAngleOverride(0.1f, 0.0f, DirectX::XM_PI);
inline constexpr float kfDefaultMinimumAmbient = 0.03f;
inline Wrapper gMinimumAmbient(kfDefaultMinimumAmbient, kfDefaultMinimumAmbient, 0.1f);

inline Wrapper gBaseHeight(6.0f, 0.0f, 20.0f);

inline Wrapper gMisc0(-50.0f, -60.0f, -40.0f);

// Gltf
inline Wrapper gGltfExposuse(3.0f, 0.0f, 10.0f);
inline Wrapper gGltfGamma(0.7f, 0.0f, 2.0f);
inline Wrapper gGltfIblAmbient(0.7f, 0.0f, 2.0f);
inline Wrapper gGltfDiffuse(0.0f, 0.0f, 3.0f);
inline Wrapper gGltfSpecular(3.0f, 0.0f, 10.0f);
inline Wrapper gGltfSmoke(0.5f, 0.0f, 1.0f);

inline Wrapper gGltfBrdf(5.4f, 0.0f, 10.0f);
inline Wrapper gGltfBrdfPower(2.0f, 0.0f, 4.0f);
inline Wrapper gGltfIbl(2.0f, 0.0f, 4.0f);
inline Wrapper gGltfIblPower(1.5f, 0.0f, 4.0f);
inline Wrapper gGltfSun(1.0f, 0.0f, 10.0f);
inline Wrapper gGltfSunPower(1.0f, 0.0f, 4.0f);
inline Wrapper gGltfLighting(0.25f, 0.0f, 0.4f);
inline Wrapper gGltfLightingPower(0.9f, 0.0f, 2.0f);

// Sound
inline Wrapper gMasterVolume(1.0f, 0.0f, 1.0f);
inline Wrapper gMusicVolume(0.45f, 0.0f, 1.0f);
inline Wrapper gSoundVolume(1.0f, 0.0f, 1.0f);

// Islands & terrain
inline Wrapper gVisibleAreaExtraTop(0.046f, 0.0f, 1.0f);
inline Wrapper gVisibleAreaExtraBottom(0.16f, 0.0f, 1.0f);
inline Wrapper gIslandHeight(30.0f, 10.0f, 50.0f);
inline Wrapper gWaterDepth(5.0f, 1.0f, 20.0f);
inline Wrapper gIslandAmbientOcclusion(0.6f, 0.0f, 1.0f);

inline Wrapper gTerrainEarlyOut(-0.1f, -1.0f, 0.0f);
inline Wrapper gWaterEarlyOut(0.0f, -0.1f, 0.1f);

inline Wrapper gTerrainRockMultiplier(3.0f, 1.0f, 20.0f);
inline Wrapper gTerrainRockSize(0.15f, 0.01f, 0.4f);
inline Wrapper gTerrainRockBlend(0.6f, 0.0f, 1.0f);
inline Wrapper gTerrainRockNormalsSizeOne(0.04f, 0.01f, 0.5f);
inline Wrapper gTerrainRockNormalsSizeTwo(0.03f, 0.005f, 0.5f);
inline Wrapper gTerrainRockNormalsSizeThree(0.25f, 0.01f, 0.5f);
inline Wrapper gTerrainRockNormalsBlend(0.65f, 0.0f, 2.0f);

inline Wrapper gTerrainSnowMultiplier(2.4f, 1.0f, 5.0f);

inline Wrapper gTerrainBeachHeight(0.05f, 0.0f, 0.2f);
inline Wrapper gTerrainBeachSandSize(0.17f, 0.01f, 0.4f);
inline Wrapper gTerrainBeachSandBlend(0.6f, 0.0f, 1.0f);
inline Wrapper gTerrainBeachNormalsSizeOne(0.01f, 0.001f, 0.1f);
inline Wrapper gTerrainBeachNormalsSizeTwo(0.015f, 0.005f, 0.05f);
inline Wrapper gTerrainBeachNormalsSizeThree(0.14f, 0.01f, 0.5f);
inline Wrapper gTerrainBeachNormalsBlend(1.0f, 0.0f, 4.0f);

// Water
inline Wrapper gWaterTerrainHeight(3.5f, 1.0f, 8.0f);
inline Wrapper gWaterTerrainFade(0.025f, 0.001f, 0.04f);
inline Wrapper gWaterNoiseFrequency(0.007f, 0.0f, 0.02f);
inline Wrapper gWaterNoiseAmount(1.4f, 0.0f, 2.0f);
inline Wrapper gWaterColorNoiseFrequency(0.0013f, 0.0f, 0.01f);
inline Wrapper gWaterColorNoiseAmount(0.1f, 0.0f, 0.2f);

inline Wrapper gWaterDepthLutFeather(4.0f, 0.01f, 10.0f);
inline Wrapper gWaterDepthColorFeather(5.2f, 0.1f, 20.0f);
inline Wrapper gWaterDepthReflectionFeather(0.05f, 0.001f, 0.1f);
inline Wrapper gWaterFresnel(0.07f, 0.0f, 0.2f);
inline Wrapper gWaterFresnel2(0.8f, 0.0f, 4.0f);
inline Wrapper gWaterColorBottom(0.134f, -2.0f, 2.0f);
inline Wrapper gWaterColorHeight(1.86f, 0.0f, 4.0f);

inline Wrapper gWaterHeightDarkenTop(0.0f, -0.1f, 0.05f);
inline Wrapper gWaterHeightDarkenBottom(-0.25f, -0.5f, 0.0f);
inline Wrapper gWaterHeightDarkenClamp(0.0f, 0.0f, 0.9f);

// Lighting
inline Wrapper gLightingTextureMultiplier(1.0f / 4.0f, 1.0f / 64.0f, 1.0f / 1.0f);
inline Wrapper gLightingBlurDownscale(0.8f, 0.5f, 0.9f);
inline Wrapper gLightingCombineIndex(0.4f, 0.0f, 10.4f);
inline Wrapper gLightingBlurDistance(0.1f, 0.0f, 0.5f);
inline Wrapper gLightingBlurDirectionality(1.0f, 0.0f, 1.0f);
inline Wrapper gLightingBlurJitter(0.1f, 0.0f, 0.2f);
inline Wrapper gLightingBlurFirstDivisor(1300.0f, 100.0f, 2000.0f);
inline Wrapper gLightingBlurDivisor(0.24f, 00.0f, 1.0f);
inline Wrapper gLightingCombineDecay(0.7f, 0.5f, 1.0f);
inline Wrapper gLightingCombinePower(0.5f, 0.1f, 2.0f);

inline Wrapper gLightingDirectional(1.5f, 0.0f, 2.0f);
inline Wrapper gLightingIndirect(0.8f, 0.0f, 1.0f);
inline Wrapper gLightingTerrain(0.6f, 0.0f, 2.0f);
inline Wrapper gLightingAddTerrain(0.2f, 0.0f, 1.0f);
inline Wrapper gLightingObjects(3.0f, 0.0f, 8.0f);
inline Wrapper gLightingObjectsAdd(0.2f, 0.0f, 1.0f);

inline Wrapper gLightingSampledNormalsSize(0.3f, 0.05f, 0.5f);
inline Wrapper gLightingSampledNormalsSizeMod(-0.005f, -0.02f, 0.02f);
inline Wrapper gLightingSampledNormalsSpeed(0.03f, 0.0f, 0.05f);

inline Wrapper gLightingTimeOfDayMultiplier(0.5f, 0.0f, 1.0f);

// Water skybox
inline Wrapper gLightingWaterSkyboxSunBias(2.0f, 0.0f, 4.0f);
inline Wrapper gLightingWaterSkyboxNormalSoften(0.45f, 0.0f, 1.0f);
inline Wrapper gLightingWaterSkyboxNormalBlendWave(0.05f, 0.0f, 0.2f);
inline Wrapper gLightingWaterSkyboxIntensity(0.001f, 0.0005f, 0.004f);
inline Wrapper gLightingWaterSkyboxAdd(0.9f, 0.0f, 2.0f);
inline Wrapper gLightingWaterSkyboxOne(900.0f, 0.0f, 3000.0f);
inline Wrapper gLightingWaterSkyboxOnePower(200.0f, 50.0f, 400.0f);
inline Wrapper gLightingWaterSkyboxTwo(280.0f, 0.0f, 400.0f);
inline Wrapper gLightingWaterSkyboxTwoPower(8.0f, 2.0f, 10.0f);
inline Wrapper gLightingWaterSkyboxThree(207.0f, 1.0f, 800.0f);
inline Wrapper gLightingWaterSkyboxThreePower(1.3f, 0.01f, 2.0f);

// Water night specular lighting
inline Wrapper gLightingWaterSpecularNormalSoften(0.1f, 0.0f, 0.5f);
inline Wrapper gLightingWaterSpecularNormalBlendWave(0.2f, 0.0f, 0.5f);

inline Wrapper gLightingWaterSpecularDiffuse(1.5f, 0.0f, 4.0f);
inline Wrapper gLightingWaterSpecularDirect(9.0f, 0.0f, 40.0f);
inline Wrapper gLightingWaterSpecular(0.7f, 0.0f, 4.0f);
inline Wrapper gLightingWaterSpecularIntensity(0.004f, 0.001f, 0.01f);
inline Wrapper gLightingWaterSpecularAdd(1.0f, 0.0f, 1.0f);
inline Wrapper gLightingWaterSpecularOne(600.0f, 50.0f, 1000.0f);
inline Wrapper gLightingWaterSpecularOnePower(5.0f, 1.0f, 20.0f);
inline Wrapper gLightingWaterSpecularTwo(32.0f, 1.0f, 50.0f);
inline Wrapper gLightingWaterSpecularTwoPower(1.3f, 0.5f, 5.0f);
inline Wrapper gLightingWaterSpecularThree(40.0f, 1.0f, 100.0f);
inline Wrapper gLightingWaterSpecularThreePower(0.1f, 0.05f, 0.5f);

// Smoke
inline Wrapper gSmokeDecay(0.996f, 0.990f, 1.0f);
inline Wrapper gSmokeDecayExtra(0.99f, 0.95f, 1.0f);
inline Wrapper gSmokeDecayExtraThreshold(0.00025f, 0.0f, 0.0005f);
inline Wrapper gSmokeEdgeDecayDistance(0.05f, 0.0f, 1.0f);
inline Wrapper gSmokeTrailsQuantity(400.0f, 0.0f, 2000.0f);
inline Wrapper gSmokeTrailsWidthCurrent(0.06f, 0.0f, 0.2f);
inline Wrapper gSmokeTrailsWidthPrevious(0.015f, 0.0f, 0.2f);
inline Wrapper gSmokeTrailsLength(1.5f, 0.0f, 10.0f);
inline Wrapper gSmokeTrailsLengthJitter(1.0f, 0.0f, 10.0f);
inline Wrapper gSmokeTrailsSideJitter(1.5f, 0.0f, 3.0f);
inline Wrapper gSmokeTrailsFalloff(3.0f, 0.1f, 10.0f);
inline Wrapper gSmokeTrailsFollow(0.55f, 0.0f, 1.0f);

inline Wrapper gSmokeWindNoiseScale(0.06f, 0.001f, 0.1f);
inline Wrapper gSmokeWindNoiseQuantity(0.00006f, 0.0f, 0.0001f);
inline Wrapper gSmokeNoiseScaleOne(3.0f, 0.1f, 8.0f);
inline Wrapper gSmokeNoiseScaleTwo(0.2f, 0.01f, 1.0f);
inline Wrapper gSmokeNoiseQuantity(0.000055f, 0.00001f, 0.0002f);
inline Wrapper gSmokeMax(0.1f, 0.0f, 1.0f);
inline Wrapper gSmokePower(0.18f, 0.1f, 1.0f);
inline Wrapper gSmokeColorMin(0.45f, 0.0f, 1.0f);
inline Wrapper gSmokeColorMultiplier(2.0f, 0.1f, 4.0f);
inline Wrapper gSmokeTrailPower(1.0f, 0.1f, 10.0f);
inline Wrapper gSmokeTrailAlpha(0.8f, 0.0f, 1.0f);

// Low frequency waves
inline Wrapper gLowCount(255i64, std::move(std::vector<int64_t> {15, 31, 63, 127, 255}));
inline Wrapper gLowMax(200.0f, 0.0f, 255.0f);
inline Wrapper gLowAngle(4.95f, 0.0f, DirectX::XM_2PI);
inline Wrapper gLowWavelength(8.5f, 1.0f, 20.0f);
inline Wrapper gLowAmplitude(0.03f, 0.0f, 0.1f);
inline Wrapper gLowSpeed(0.25f, 0.0f, 1.0f);
inline Wrapper gLowSteepness(0.4f, 0.0f, 1.0f);

inline Wrapper gLowAngleAdjust(0.2f, 0.0f, 2.0f);
inline Wrapper gLowWavelengthAdjust(-0.95f, -1.0f, 0.0f);
inline Wrapper gLowAmplitudeAdjust(1.5f, 0.0f, 2.0f);
inline Wrapper gLowSpeedAdjust(1.0f, 0.0f, 2.0f);

// Medium frequency waves
inline Wrapper gMediumCount(31i64, std::move(std::vector<int64_t> {15, 31, 63, 127, 255}));
inline Wrapper gMediumWavelength(3.27f, 0.01f, 10.0f);
inline Wrapper gMediumAmplitude(0.0137f, 0.0f, 0.1f);
inline Wrapper gMediumSpeed(0.05f, 0.001f, 0.4f);
inline Wrapper gMediumSteepness(3.0f, 0.0f, 10.0f);

inline Wrapper gMediumAngleAdjust(9.46f, 0.0f, 20.0f);
inline Wrapper gMediumWavelengthAdjust(0.77f, 0.0f, 10.0f);
inline Wrapper gMediumAmplitudeAdjust(0.87f, 0.0f, 5.0f);
inline Wrapper gMediumSpeedAdjust(2.46f, 0.0f, 5.0f);

// High frequency waves & Water
inline Wrapper gHighMultiplier(0.204f, 0.0f, 0.5f);
inline Wrapper gHighScaleOne(0.85f, 0.0f, 2.0f);
inline Wrapper gHighScaleTwo(1.2f, 0.0f, 2.0f);

inline Wrapper gBeachDirectionalFadeBottom(0.25f, 0.0f, 2.0f);
inline Wrapper gBeachDirectionalFadeHeight(0.7f, 0.0f, 1.0f);

// Shadow
inline Wrapper gShadowFeatherNoon(2.5f, 0.0f, 8.0f);
inline Wrapper gShadowFeatherNoonOffset(0.92f, 0.0f, 5.0f);
inline Wrapper gShadowFeatherSunset(0.05f, 0.0f, 0.5f);
inline Wrapper gShadowFeatherSunsetOffset(-0.1f, -0.5f, 0.1f);
inline Wrapper gShadowFeatherPower(1.7f, 0.1f, 10.0f);
inline Wrapper gShadowDistanceFallof(120.0f, 10.0f, 400.0f);
inline Wrapper gShadowBlurSigma(9.0f, 1.0f, 20.0f);
inline Wrapper gShadowAffectAmbient(0.75f, 0.0f, 1.0f);
inline Wrapper gShadowHeightFadeTop(4.0f, 0.0f, 20.0f);
inline Wrapper gShadowHeightFadeBottom(0.0f, -20.0f, 0.0f);

inline Wrapper gObjectShadowsRenderMultiplier(2.0f, 0.25f, 4.0f);
inline Wrapper gObjectShadowsBlurMultiplier(0.5f, 0.125f, 1.0f);
inline Wrapper gObjectShadowsNoon(0.6f, 0.1f, 1.0f);
inline Wrapper gObjectShadowsSunset(0.7f, 0.1f, 1.0f);
inline Wrapper gObjectShadowsSunsetStretch(2.5f, 0.0f, 10.0f);
inline Wrapper gObjectShadowsBlurDistanceNoon(0.0001f, 0.00005f, 0.001f);
inline Wrapper gObjectShadowsBlurDistanceSunset(0.0008f, 0.0001f, 0.002f);

inline Wrapper gSmokeShadowIntensity(0.6f, 0.0f, 1.0f);

// Hex shield
inline Wrapper gHexShieldGrow(3.5f, 1.0f, 15.0f);
inline Wrapper gHexShieldEdgeDistance(18.9f, 18.8f, 19.1f);
inline Wrapper gHexShieldEdgePower(1.0f, 1.0f, 30.0f);
inline Wrapper gHexShieldEdgeMultiplier(1.3f, 0.5f, 5.0f);

inline Wrapper gHexShieldWaveMultiplier(7.0f, 0.0f, 20.0f);
inline Wrapper gHexShieldWaveDotMultiplier(5.0f, 0.5f, 10.0f);
inline Wrapper gHexShieldWaveIntensityMultiplier(12.0f, 0.5f, 20.0f);
inline Wrapper gHexShieldWaveIntensityPower(1.6f, 0.25f, 4.0f);
inline Wrapper gHexShieldWaveFalloffPower(2.1f, 0.25f, 4.0f);

inline Wrapper gHexShieldDirectionFalloffPower(5.3f, 2.0f, 10.0f);
inline Wrapper gHexShieldDirectionMultiplier(5.0f, 0.5f, 8.0f);

// Test
inline Wrapper gTestOne(0.0f, -10.0f, 10.0f);
inline Wrapper gTestTwo(0.0f, -10.0f, 10.0f);

} // namespace engine
