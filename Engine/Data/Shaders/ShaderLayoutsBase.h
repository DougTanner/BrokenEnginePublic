// DT: TEMP
// #define DT_LIGHTING_ONLY

// #define ENABLE_SHADER_REALTIME_CLOCK_EXT
// #define ENABLE_DEBUG_PRINTF_EXT
#if defined(ENABLE_DEBUG_PRINTF_EXT)
	#define ENABLE_VULKAN_DEBUG_LAYERS
#endif

#if defined(BT_ENGINE)

#pragma once

#define CONSTEXPR inline constexpr
#define INLINE inline
#define INIT {}
#define STD std::

// Constexpr bool equivalents of shader defines for C++ code
#if defined(ENABLE_DEBUG_PRINTF_EXT)
inline constexpr bool kbDebugPrintf = true;
#else
inline constexpr bool kbDebugPrintf = false;
#endif

#if defined(ENABLE_SHADER_REALTIME_CLOCK_EXT)
inline constexpr bool kbShaderRealtimeClock = true;
#else
inline constexpr bool kbShaderRealtimeClock = false;
#endif

namespace shaders
{

inline constexpr VkFormat keElevationFormat = VK_FORMAT_R16_SFLOAT;

inline constexpr VkFormat keLightingFormat = VK_FORMAT_R16G16B16A16_SFLOAT;

inline constexpr VkFormat keSmokeFormat = VK_FORMAT_R32_SFLOAT;
inline constexpr VkFormat keWindFormat = VK_FORMAT_R16G16_SFLOAT;

struct vec2 : public XMFLOAT2
{
};

struct vec3 : public XMFLOAT3
{
};

struct vec4 : public XMFLOAT4
{
	vec4() = default;

	constexpr vec4(float fX, float fY, float fZ, float fW)
	: XMFLOAT4(fX, fY, fZ, fW)
	{
	}

	constexpr vec4(const XMFLOAT4& rOther)
	{
		x = rOther.x;
		y = rOther.y;
		z = rOther.z;
		w = rOther.w;
	}
};

struct ivec4
{
	int32_t x = 0, y = 0, z = 0, w = 0;
};

struct uvec4
{
	uint32_t x = 0, y = 0, z = 0, w = 0;
};

#else

#extension GL_ARB_separate_shader_objects : require
#extension GL_EXT_shader_explicit_arithmetic_types : require
#extension GL_EXT_scalar_block_layout : require
layout(scalar) uniform;
#if defined(ENABLE_DEBUG_PRINTF_EXT)
	#extension GL_EXT_debug_printf : require
#endif
#if defined(ENABLE_SHADER_REALTIME_CLOCK_EXT)
	#extension GL_EXT_shader_realtime_clock : require
#endif

#define UINT_MAX 0xFFFFFFFF
#define INT_MAX 2147483647
#define CONSTEXPR const
#define INLINE
#define INIT
#define STD

#define ELEVATION_FORMAT r16f

struct VkDrawIndexedIndirectCommand
{
	uint32_t indexCount;
	uint32_t instanceCount;
	uint32_t firstIndex;
	int32_t vertexOffset;
	uint32_t firstInstance;
};

struct VkDispatchIndirectCommand
{
	uint32_t x;
	uint32_t y;
	uint32_t z; 
};

#endif // BT_ENGINE

CONSTEXPR float fPi = 3.141592654f;

CONSTEXPR int kiLightingTextures = 11;
CONSTEXPR int kiBillboardTexturesCount = 3;

CONSTEXPR int kiMaxIslands = 64;

CONSTEXPR int kiMaxSpreadPasses = 40;
CONSTEXPR int kiMaxDebugTextures = 3 + kiMaxSpreadPasses;

CONSTEXPR int kiDebugTextureFormatFloat16LightingDirectional = 0;
CONSTEXPR int kiDebugTextureFormatUnormLightingDirectional = 1;
CONSTEXPR int kiDebugTextureFormatFloat16Linear = 2;
CONSTEXPR int kiDebugTextureFormatFloat16LinearVisibleArea = 3;
CONSTEXPR int kiDebugTextureFormatFloat16DepositDirectionCombined = 4;
CONSTEXPR int kiDebugTextureFormatFloat16SpreadDirectionCombined = 5;

CONSTEXPR int kiShadowTextureExecutionSize = 64;

CONSTEXPR int kiComputeTileSize = 8;
CONSTEXPR int kiOccupancyDilateGroupSize = 256;
CONSTEXPR int kiParticleUpdateGroupSize = 32;

CONSTEXPR int kiMaxAlphaMesh = 16;

CONSTEXPR float kfAmbient = 0.05f;

CONSTEXPR vec4 kf4MudColor = {99.0f / 255.0f, 75.0f / 255.0f, 53.0f / 255.0f, 0.0f};

struct PushConstantsLayout
{
	vec4 f4Pipeline INIT;
	vec4 f4Material INIT; // x: Material index
};

struct GlobalLayout
{
	int32_t iCommandBuffer INIT;
	int32_t iCameraFrame INIT;
	int32_t iTickCounter INIT;
	uint32_t uiRandomSeed INIT;

	float fElapsedTime INIT;
	float fBaseHeight INIT;
	float fAspectRatio INIT;
	float fDetailTextureAspectRatio INIT;

	vec2 f2CameraPosition INIT;
	vec4 f4VisibleArea INIT;
	vec4 f4VisibleAreaShadowsExtra INIT;

	vec4 f4SunMoonNormal INIT;
	vec4 f4SunColor INIT;
	vec4 f4MoonColor INIT;
	vec4 f4AmbientColor INIT;

	// Per-target sun/moon intensity multipliers (applied at shader read sites)
	float fSunIntensityTerrain INIT;
	float fSunIntensityWater INIT;
	float fSunIntensityObjects INIT;
	float fSunIntensitySmoke INIT;
	float fMoonIntensityTerrain INIT;
	float fMoonIntensityWater INIT;
	float fMoonIntensityObjects INIT;
	float fMoonIntensitySmoke INIT;

	// Smoke
	vec4 f4SmokeArea INIT;
	vec4 f4PreviousSmokeArea INIT;
	float fSmokeMax INIT;
	float fSmokePower INIT;
	float fSmokeDecay INIT;
	float fSmokeColorMin INIT;
	float fSmokeColorMultiplier INIT;
	float fSmokeLightingMultiplier INIT;
	float fSmokeIntensityFalloff INIT;
	float fSmokeWindNoiseScale INIT;
	float fSmokeWindNoiseQuantity INIT;
	float fSmokeNoiseQuantity INIT;
	float fSmokeNoiseScaleOne INIT;
	float fSmokeNoiseScaleTwo INIT;
	float fSmokeObjectHeightInv INIT;
	float fSmokeEdgeDecayDistanceInv INIT;
	float fSmokeNoiseInfluence INIT;
	uint32_t uiSmokeTilesX INIT;
	uint32_t uiSmokeTilesY INIT;
	float fSmokeDepositTileScale INIT;
	float fWindDisplacementNoiseScale INIT;
	float fWindSmokeAdvection INIT;
	// Wind
	float fWindAdvectionScaleHigh INIT;
	float fWindAdvectionScaleLow INIT;
	float fWindSwirlScaleHigh INIT;
	float fWindSwirlScaleLow INIT;
	float fWindSwirlAmountHigh INIT;
	float fWindSwirlAmountLow INIT;
	float fWindSwirlSpeedHigh INIT;
	float fWindSwirlSpeedLow INIT;
	float fWindVorticityConfinementHigh INIT;
	float fWindVorticityConfinementLow INIT;
	float fWindDecayHigh INIT;
	float fWindDecayLow INIT;
	float fWindMomentumHigh INIT;
	float fWindMomentumLow INIT;
	float fWindThresholdLow INIT;
	float fWindThresholdHigh INIT;
	float fWindToSmokeStrength INIT;
	float fWindTimeScale INIT;
	float fWindTexelSize INIT;
	float fWindTime INIT;
	float fWindSmokeRetention INIT;
	float fWindToSmokePower INIT;
	float fWindDiffusionHigh INIT;
	float fWindDiffusionLow INIT;
	float fWindTextureIndex INIT; // Blend factor: 0.0 = TextureOne, 1.0 = TextureTwo (continuous for interpolation)
	uint32_t uiWindTilesX INIT;
	uint32_t uiWindTilesY INIT;

	// Lighting
	float fLightingObjectsAdd INIT;
	float fLightingDepositThreshold INIT;
	float fLightingDepositCompress INIT;
	float fCombineMaxBrightness INIT;
	float fCombineContrast INIT;
	float fCombineLinearStart INIT;
	float fCombineLinearLength INIT;
	float fCombineToe INIT;
	float fCombineBlackTightness INIT;
	float fCombinePassNormalize INIT;
	float fCombineExposurePassScale INIT;
	float fCombineHuePreserve INIT;
	float pfCombineCurvePoints[kiMaxSpreadPasses] INIT;
	float fLightingTerrain INIT;
	float fLightingObjects INIT;

	float fLightingAddTerrain INIT;
	float fSpreadDirectionalityStart INIT;
	vec4 f4LightingArea INIT;
	uint32_t uiLightTilesX INIT;
	uint32_t uiLightTilesY INIT;

	// Spread Start (radial directional spread)
	float fSpreadDirectionCountStart INIT;
	float fSpreadDistanceStart INIT;
	float fSpreadRingCountStart INIT;
	float fSpreadJitterStart INIT;
	float fSpreadSampleJitterRangeStart INIT;
	float fSpreadSampleJitterClusteringStart INIT;
	float fSpreadDecayStart INIT;
	float fSpreadAccumulationDecayStart INIT;
	float fSpreadDistanceFalloffStart INIT;
	float fSpreadOutputThresholdStart INIT;
	float fSpreadOutputCompressStart INIT;
	float fSpreadPassCount INIT;
	float pfSpreadRingRotations[kiMaxSpreadPasses] INIT;

	// Spread End (interpolation targets for last spread pass)
	float fSpreadDirectionalityEnd INIT;
	float fSpreadDirectionCountEnd INIT;
	float fSpreadDistanceEnd INIT;
	float fSpreadRingCountEnd INIT;
	float fSpreadJitterEnd INIT;
	float fSpreadSampleJitterRangeEnd INIT;
	float fSpreadSampleJitterClusteringEnd INIT;
	float fSpreadDecayEnd INIT;
	float fSpreadAccumulationDecayEnd INIT;
	float fSpreadDistanceFalloffEnd INIT;
	float fSpreadOutputThresholdEnd INIT;
	float fSpreadOutputCompressEnd INIT;

	// Spread Height Fade
	float fSpreadHeightMultiplier INIT;
	float fSpreadHeightEndHeight INIT;
	float fSpreadHeightPower INIT;

	// Shadow
	float fShadowWidthScale INIT;
	float fShadowSunAngle INIT;
	float fShadowDirectionMultiplier INIT;
	float fShadowMoonMultiplier INIT;
	float fShadowFeather INIT;
	float fShadowNoonOffset INIT;
	float fShadowDistanceFalloff INIT;
	float fShadowBlurSigma INIT;
	float fObjectShadowsBlurDistance INIT;
	float fObjectShadowsBlurSigma INIT;
	float fObjectShadowsIntensity INIT;
	float fShadowSunsetOffset INIT;
	float fShadowSunriseStretch INIT;
	float fShadowSunsetStretch INIT;
	float fShadowAffectAmbient INIT;
	float fWaterReducedNoiseOriginX INIT;
	int32_t iShadowElevationSize INIT;
	int32_t iShadowIncrement INIT;
	int32_t iShadowStartOffset INIT;
	float fWaterReducedNoiseOriginY INIT;
	int32_t iShadowTextureWidth INIT;
	int32_t iShadowTextureHeight INIT;
	int32_t iObjectShadowTextureWidth INIT;
	int32_t iObjectShadowTextureHeight INIT;

	// Terrain
	float fIslandHeight INIT;
	float fIslandAmbientOcclusion INIT;
	float fTerrainEarlyOut INIT;
	float fWaterEarlyOut INIT;
	float fWaterDepth INIT;
	float fTerrainSunBrightness INIT;
	float fWaterReducedNormalOriginX INIT;
	float fWaterReducedNormalOriginY INIT;
	float fWaterReducedNormalOriginTwoX INIT;
	float fWaterReducedNormalOriginTwoY INIT;
	float fWaterReducedNormalOriginThreeX INIT;
	float fWaterReducedNormalOriginThreeY INIT;

	// Water
	int32_t iWaterLowCount INIT;
	int32_t iWaterMediumCount INIT;
	float fWaterOriginX INIT;
	float fWaterOriginY INIT;
	float fWaterHeight INIT;
	float fWaterTerrainHeight INIT;
	float fWaterTerrainFade INIT;
	float fWaterTerrainFadeClamp INIT;
	float fWaterNoiseFrequency INIT;
	float fWaterNoiseAmount INIT;
	float fWaterDepthLutFeather INIT;
	float fWaterDepthColorFeather INIT;
	float fWaterDepthReflectionFeather INIT;
	float fWaterColorNoiseFrequency INIT;
	float fWaterHighMultiplier INIT;
	float fWaterHighScaleOne INIT;
	float fWaterHighScaleTwo INIT;
	float fWaterSunVisibility INIT;
	float fWaterFresnel INIT;
	float fWaterColorBottom INIT;
	float fWaterColorHeightInv INIT;
	float fWaterColorNoiseAmount INIT;
	float fWaterColorNoiseWeightOne INIT;
	float fWaterColorNoiseWeightTwo INIT;
	float fWaterColorNoiseMultiplierOne INIT;
	float fWaterColorNoiseMultiplierTwo INIT;
	float fWaterDirectional INIT;
	float fWaterFresnel2 INIT;
	float fBeachFadeTop INIT;
	float fBeachFadeInvRange INIT;
	float fWaterLowSteepness INIT;
	float fWaterMediumSteepness INIT;
	float fWaterReducedNormalTime INIT;
	float fWaterReducedNormalTimeTwo INIT;
	float fWaterReducedNormalTimeThree INIT;

	// Particles
	float fParticlesStretchVelocityStart INIT;
	float fParticlesStretchVelocityEnd INIT;
	float fParticlesStretchVelocityMultiplier INIT;

	// Shadow
	float fShadowTextureSizeWidth INIT;
	float fShadowTextureSizeHeight INIT;
	float fShadowElevationTextureSizeWidth INIT;
	float fShadowElevationTextureSizeHeight INIT;
	float fShadowHeightFadeTop INIT;
	float fShadowHeightFadeBottom INIT;

	// Terrain
	float fTerrainNormalXMultiplier INIT;
	float fTerrainNormalYMultiplier INIT;

	float fTerrainSnowMultiplier INIT;

	float fTerrainRockMultiplier INIT;
	float fTerrainRockSize INIT;
	float fTerrainRockBlend INIT;
	float fTerrainRockNormalsSizeOne INIT;
	float fTerrainRockNormalsSizeTwo INIT;
	float fTerrainRockNormalsSizeThree INIT;
	float fTerrainRockNormalsBlend INIT;

	float fTerrainBeachHeight INIT;
	float fTerrainBeachSandSize INIT;
	float fTerrainBeachSandBlend INIT;
	float fTerrainBeachNormalsSizeOne INIT;
	float fTerrainBeachNormalsSizeTwo INIT;
	float fTerrainBeachNormalsSizeThree INIT;
	float fTerrainBeachNormalsBlend INIT;

	// Time of day
	float fLightingTimeOfDayMultiplier INIT;
	float fLightingNightMultiplier INIT;
	float fLightingWaterSkyboxOne INIT;

	// Debug
	float fDebugTextureIndex INIT;
	float fDebugTextureFormat INIT;
	float fDebugTextureLinearRange INIT;
};

struct MainLayout
{
	int32_t iFrameNumber INIT;
	int32_t iRenderNumber INIT;

	vec4 f4x4ViewProjection[4] INIT;

	vec4 f4EyePosition INIT;
	vec4 f4ToEyeNormal INIT;

	vec4 pf4LowWavesOne[256] INIT;
	vec4 pf4LowWavesTwo[256] INIT;

	vec4 pf4MediumWavesOne[256] INIT;
	vec4 pf4MediumWavesTwo[256] INIT;

	// Lighting — water normal map atlas (3 weighted samples)
	float fLightingSampledNormalsOneSize INIT;
	float fLightingSampledNormalsTwoSize INIT;
	float fLightingSampledNormalsThreeSize INIT;
	float fLightingSampledNormalsSpeedMin INIT;
	float fLightingSampledNormalsSpeedMax INIT;
	uint32_t uiWaterNormalIndexOne INIT;
	uint32_t uiWaterNormalIndexTwo INIT;
	uint32_t uiWaterNormalIndexThree INIT;
	float fWaterNormalWeightOneMin INIT;
	float fWaterNormalWeightOneMax INIT;
	float fWaterNormalWeightTwoMin INIT;
	float fWaterNormalWeightTwoMax INIT;
	float fWaterNormalWeightThreeMin INIT;
	float fWaterNormalWeightThreeMax INIT;
	float fWaterNormalRotationOne INIT;
	float fWaterNormalRotationTwo INIT;
	float fWaterNormalRotationThree INIT;
	float fCameraHeightZoomFactor INIT;
	float fWaterHeightDarkenTop INIT;
	float fWaterHeightDarkenBottom INIT;
	float fWaterHeightDarkenClamp INIT;

	float fLightingWaterSkyboxSunBias INIT;
	float fLightingWaterSkyboxNormalSoften INIT;
	float fLightingWaterSkyboxNormalBlendWave INIT;
	float fLightingWaterSkyboxIntensity INIT;
	float fLightingWaterSkyboxAdd INIT;
	float fLightingWaterSkyboxOnePower INIT;
	float fLightingWaterSkyboxTwo INIT;
	float fLightingWaterSkyboxTwoPower INIT;
	float fLightingWaterSkyboxThree INIT;
	float fLightingWaterSkyboxThreePower INIT;
	float fLightingWaterSkyboxLod INIT;

	float fLightingWaterReflectedAmount INIT;
	float fLightingWaterReflectedNormalBlendWave INIT;
	float fLightingWaterReflectedDistortion INIT;
	float fLightingWaterReflectedFalloffStart INIT;
	float fLightingWaterReflectedFalloffPower INIT;
	float fLightingWaterReflectedFresnel INIT;
	float fLightingWaterReflectedIntensity INIT;

	float fLightingWaterNormalSoften INIT;
	float fLightingWaterNormalBlendWave INIT;
	float fLightingWaterIntensity INIT;
	float fLightingWaterAdd INIT;
	float fLightingWaterOne INIT;
	float fLightingWaterOnePower INIT;
	float fLightingWaterTwo INIT;
	float fLightingWaterTwoPower INIT;
	float fLightingWaterThree INIT;
	float fLightingWaterThreePower INIT;
	float fLightingWaterPowerMode INIT;

	float fLightingNewDirectional INIT;
	float fLightingNewDirectionalPower INIT;
	float fLightingDirectionalPowerMode INIT;
	float fLightingNewAmbient INIT;
	float fLightingNewAmbientPower INIT;
	float fLightingAmbientPowerMode INIT;
	float fLightingWaterAmbientPower INIT;
	float fLightingWaterAmbientPowerMode INIT;
	float fLightingWaterNewAmbient INIT;
	float fLightingWaterNewAmbientPower INIT;
	float fLightingWaterNewAmbientPowerMode INIT;
	float fLightingTerrainBelowBaseMultiplier INIT;
	float fLightingTerrainBelowBasePower INIT;

	// Pbr
	float fPbrExposure INIT;
	float fPbrGamma INIT;
	float fPbrDayBrightness INIT;
	float fPbrAmbient INIT;

	float fPbrMipCount INIT;
	float fPbrSmoke INIT;
	float fPbrDebugViewInputs INIT;
	float fPbrDebugViewEquation INIT;

	float fPbrBrdfDiffuse INIT;
	float fPbrBrdfDiffusePower INIT;
	float fPbrBrdfSpecular INIT;
	float fPbrBrdfSpecularPower INIT;
	float fPbrIblDiffuse INIT;
	float fPbrIblDiffusePower INIT;
	float fPbrIblSpecular INIT;
	float fPbrIblSpecularPower INIT;
	float fPbrSun INIT;
	float fPbrSunPower INIT;
	float fPbrLighting INIT;
	float fPbrLightingPower INIT;
	float fPbrLightingSpecular INIT;
	float fPbrLightingSpecularPower INIT;
	float fPbrEmissive INIT;
	float fPbrIblShadowBlend INIT;
	float fPbrIblAmbientColorBlend INIT;
	float fPbrShadowFloor INIT;
	float fPbrCubemapLodPower INIT;
	float fPbrCubemapLodOffset INIT;

	// Shadow
	float fSmokeShadowIntensity INIT;

	// Hex shield
	float fHexShieldGrow INIT;
	float fHexShieldEdgeDistance INIT;
	float fHexShieldEdgePower INIT;
	float fHexShieldEdgeMultiplier INIT;

	float fHexShieldWaveMultiplier INIT;
	float fHexShieldWaveDotMultiplier INIT;
	float fHexShieldWaveIntensityMultiplier INIT;
	float fHexShieldWaveIntensityPower INIT;
	float fHexShieldWaveFalloffPower INIT;

	float fHexShieldDirectionFalloffPower INIT;
	float fHexShieldDirectionMultiplier INIT;
};

struct AxisAlignedQuadLayout
{
	vec4 f4VertexRect INIT;
	vec4 f4TextureRect INIT;
	vec4 f4Params INIT;
	float fRotation INIT; // radians; 0 = identity (no rotation). Vertex shader rotates corners around quad center.
	uint32_t uiColor INIT;
};

struct BillboardLayout
{
	vec4 f4Position INIT;
	float fSize INIT;
	float fTextureIndex INIT;
	float fRotation INIT;
	float fAlpha INIT;
};

struct DebugRenderLayout
{
	vec4 f3x4Transform[3] INIT;
	vec4 f4Color INIT;
};

struct QuadLayout
{
	vec4 pf4VerticesTexcoords[4] INIT;
	vec4 pf4Params[4] INIT;
	vec4 f4Params INIT;
	uint32_t uiColor INIT;
};

struct VisibleLightQuadLayout
{
	vec4 pf4Vertices[4] INIT;
	vec4 pf4Texcoords[4] INIT;
	uint32_t puiColors[4] INIT;

	float fIntensity INIT;
	float fRotation INIT;

	uint32_t uiTextureIndex INIT;
};

struct ObjectLayout
{
	vec4 f4Position INIT;
	uint32_t uiColor INIT;
	vec4 f3x4Transform[3] INIT;
	vec4 f3x4TransformNormal[3] INIT;
};

struct PbrMaterialLayout
{
	// First 9 fields must exactly match MaterialShaderData (from f4BaseColorFactor onward)
	// PBR material properties per glTF 2.0 metallic-roughness specification
	vec4 f4BaseColorFactor INIT;
	vec4 f4EmissiveFactor INIT;
	int32_t iColorTextureSet INIT;
	int32_t iPhysicalDescriptorTextureSet INIT;
	int32_t iNormalTextureSet INIT;
	int32_t iOcclusionTextureSet INIT;
	int32_t iEmissiveTextureSet INIT;
	float fMetallicFactor INIT;
	float fRoughnessFactor INIT;
	float fAlphaMask INIT;
	float fAlphaMaskCutoff INIT;

	// Bindless texture array indices (populated by CrcToIndex() on CPU)
	float fColorTextureIndex INIT;
	float fPhysicalDescriptorTextureIndex INIT;
	float fNormalTextureIndex INIT;
	float fOcclusionTextureIndex INIT;
	float fEmissiveTextureIndex INIT;
};

struct ModelLayout
{
	vec4 f4Position INIT;
	vec4 f3x4Transform[3] INIT;
	vec4 f3x4TransformNormal[3] INIT;
	vec4 f4ColorAdd INIT;
	uint32_t uiMeshDataBase INIT;     // Base index into meshData[] for this object
	uint32_t uiMaterialCount INIT;    // Number of materials (slots in meshData)
};

struct ModelCustomLayout
{
	vec4 f4Position INIT;
	vec4 f3x4Transform[3] INIT;
	vec4 f3x4TransformNormal[3] INIT;
	vec4 f3x4TransformCustom[3] INIT;
	vec4 f3x4TransformCustomNormal[3] INIT;
	vec4 f4ColorAdd INIT;
};

CONSTEXPR int32_t kiHexShieldDirections = 4 * 4;
#if defined(BT_ENGINE)
static_assert((kiHexShieldDirections % 4) == 0);
#endif

struct HexShieldLayout
{
#if defined(BT_ENGINE)
	bool operator==(const HexShieldLayout&) const = default;
#endif

	vec4 f4Position INIT;
	vec4 f3x4Transform[3] INIT;
	vec4 f3x4TransformNormal[3] INIT;
	vec4 f4Color INIT;
	vec4 f4LightingColor INIT;
	vec4 pf4Directions[kiHexShieldDirections] INIT;
	float pfVertIntensities[kiHexShieldDirections] INIT;
	float pfFragIntensities[kiHexShieldDirections] INIT;

	float fLightingIntensity INIT;
	float fSize INIT;
	float fColorMix INIT;
	float fMinimumIntensity INIT;
};

CONSTEXPR float kFalloffThreshold = 0.45f;
CONSTEXPR float kFalloffThresholdInv = 1.0f / kFalloffThreshold;

// Particles
CONSTEXPR int kiMaxParticlesSpawn = 8 * 1024;
CONSTEXPR int kiMaxParticles = 16 * 1024;

struct ParticleLayout
{
	int32_t iColor INIT;
	int32_t iCookie INIT;

	float fVelocityDecay INIT;
	float fGravity INIT;
	float fIntensityDecay INIT;

	float fSize INIT;
	float fLength INIT;
	float fVisibleIntensity INIT;
	float fIntensityPower INIT;

	float fSizeDecay INIT;
	float fRotationDelta INIT;
	float fRotation INIT;
	float fRotationDeltaDecay INIT;

	vec4 f4Position INIT;
	vec4 f4Velocity INIT;
};

struct ParticlesSpawnLayout
{
	int32_t iCount INIT;
	int32_t iReset INIT;
	ParticleLayout pParticles[kiMaxParticlesSpawn] INIT;
};

#define ENABLE_32_BIT_BOOL

struct ParticlesLayout
{
	int32_t iLastCount INIT;
	int32_t iMinFreeIndex INIT;

	float fLastUpdateTime INIT;
	float fDeltaTime INIT;

	ParticleLayout pParticles[kiMaxParticles] INIT;
#if defined(ENABLE_32_BIT_BOOL)
	uint32_t puiAllocated[kiMaxParticles / 32 + 1] INIT;
#else
	uint16_t pbAllocated[kiMaxParticles] INIT;
#endif
};

#if defined(BT_ENGINE)

// NOTE: Uniform buffer structs (GlobalLayout, MainLayout) use scalar layout (no padding requirements)
static_assert(sizeof(GlobalLayout) <= 65536);

} // namespace shaders

#endif
