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

inline constexpr VkFormat keSmokeFormat = VK_FORMAT_R16_SFLOAT;
inline constexpr VkFormat keWindFormat = VK_FORMAT_R16G16_SFLOAT;
inline constexpr VkFormat keShadowFormat = VK_FORMAT_R16_UNORM;
inline constexpr VkFormat keCombineFormat = VK_FORMAT_R8G8B8A8_UNORM;
inline constexpr VkFormat keWaterDisplacementFormat = VK_FORMAT_R16G16B16A16_SFLOAT;

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

CONSTEXPR float kfPi = 3.141592654f;
CONSTEXPR float kfEpsilon = 1e-6f;

// Global Set-0 descriptor binding numbers. Single-sourced here (dual-language) so the C++ descriptor
// layout/writes (TextureDescriptors) and every shader's layout(set = 0, binding = N) qualifier stay in
// lockstep — a binding-number change in one place no longer silently mismatches the other.
CONSTEXPR int kiGlobalBindingGlobalUniform = 0;
CONSTEXPR int kiGlobalBindingMainUniform = 1;
CONSTEXPR int kiGlobalBindingSamplerRepeatModelData = 13;
CONSTEXPR int kiGlobalBindingSamplerRepeat = 3;
CONSTEXPR int kiGlobalBindingBindlessTextures = 4;
CONSTEXPR int kiGlobalBindingSamplerClamp = 12;

// Set-1 per-pipeline descriptor binding numbers. Single-sourced here (dual-language) like the Set-0
// block above, so the C++ pipeline setup (ModelPipeline kModel descriptor append, BufferManager mesh/joint
// storage-buffer updates, PipelineManager water displacement descriptors) and the GLSL
// layout(set = 1, binding = N) qualifiers (ModelCommon.h, Water.vert) stay in lockstep.
CONSTEXPR int kiWaterBindingDisplacement = 13;
CONSTEXPR int kiWaterBindingDisplacementNormal = 14;
CONSTEXPR int kiModelBindingMeshData = 15;
CONSTEXPR int kiModelBindingJointMatrix = 16;

// Water normal map atlas size. Shared by C++ (PipelineManager mppWaterNormalTextures array,
// TextureManager kpWaterNormalCrcs/kpWaterNormalNames tables) and the water shader
// (Water.frag pWaterNormalSamplers[] sampler array).
CONSTEXPR int kiWaterNormalCount = 17;

// Length of each per-octave-group mip-variance table uploaded for Water.frag's
// WATER_SPEC_AA_MIP_HANDOFF kernel. Must equal common::TextureHeader::kiMipVarianceCount
// (static_assert in TextureManager.cpp) — the tables are copied verbatim from texture chunk headers.
CONSTEXPR int kiWaterSpecAAMipTableSize = 10;

// Island bindless-slot ceiling: shared by C++ and the terrain shaders, it sizes the bindless
// sampler arrays (Terrain.frag / TerrainElevation.frag), their descriptor counts (PipelineManager),
// and the TextureManager render-target pointer vectors. Raised 64 -> 128 so island 01's 10 routes
// (65 kIsland chunks) fit. Bumping this only grows descriptor-array capacity + pointer vectors, not
// resident GPU textures (LRU-managed). Every kIsland template stays permanently resident (mesh +
// heightmap) regardless of this cap or LRU; that memory scaling is tracked in
// Documents/Plans/Graphics/IslandResidentMemoryScaling_Overview.md.
CONSTEXPR int kiMaxIslands = 128;

CONSTEXPR int kiMaxSpreadPasses = 40;
CONSTEXPR int kiMaxDebugTextures = kiMaxSpreadPasses + 16;

CONSTEXPR int kiDebugTextureFormatFloat16LightingDirectional = 0;
CONSTEXPR int kiDebugTextureFormatUnormLightingDirectional = 1;
CONSTEXPR int kiDebugTextureFormatFloat16Linear = 2;
CONSTEXPR int kiDebugTextureFormatFloat16LinearVisibleArea = 3;
CONSTEXPR int kiDebugTextureFormatFloat16DepositDirectionCombined = 4;
CONSTEXPR int kiDebugTextureFormatFloat16SpreadDirectionCombined = 5;
CONSTEXPR int kiDebugTextureFormatRgb = 6;
CONSTEXPR int kiDebugTextureFormatGrayscaleR = 7;
CONSTEXPR int kiDebugTextureFormatTerrainElevation = 8;

CONSTEXPR int kiShadowTextureExecutionSize = 64;

// Terrain-shadow window constants. The source and blur rectangles are derived backwards from the
// consumer-filtered final rectangle in PopulateShadowArea, keeping every separable-filter tap current.
CONSTEXPR int kiShadowBlurRadius = 5;
// This symmetric final-valid margin covers the linear neighbour plus worst-case visible-window snap
// and centered-rounding/parity reach, so consumers never reject a valid edge sample as stale.
CONSTEXPR int kiShadowConsumerFilterMargin = 3;

CONSTEXPR int kiComputeTileSize = 8;
CONSTEXPR int kiOccupancyDilateGroupSize = 256;
CONSTEXPR int kiParticleUpdateGroupSize = 32;

CONSTEXPR float kfAmbient = 0.05f;

CONSTEXPR vec4 kf4MudColor = {99.0f / 255.0f, 75.0f / 255.0f, 53.0f / 255.0f, 0.0f};

struct PushConstantsLayout
{
	vec4 f4Pipeline INIT; // Per-pipeline push payload; .w carries the model material index (read by Model.frag / ModelSkinned.vert)
};

// LightCombine.comp push constants — the combine dispatch's output dimensions. Dual-language so the
// C++ push (CommandBufferRecordMain) and the shader block stay structurally in sync (no uint->vec4 pun).
struct CombinePushConstantsLayout
{
	uint32_t uiWidth INIT;
	uint32_t uiHeight INIT;
};

struct GlobalLayout
{
	uint32_t uiRandomSeed INIT;

	float fElapsedTime INIT;
	float fBaseHeight INIT;
	float fBaseHeightInv INIT; // 1 / max(fBaseHeight, 0.001) (Terrain.frag below-base height ratio)
	float fAspectRatioInv INIT; // 1 / aspect ratio (Billboards.vert screen-space width divide, folded CPU-side)

	vec2 f2CameraPosition INIT;
	vec4 f4VisibleArea INIT;
	vec4 f4ShadowArea INIT;
	vec4 f4ShadowAreaExtra INIT;
	vec4 f4ShadowAreaPrevious INIT; // Previous-frame f4ShadowArea, for ShadowTemporal.comp reprojection

	vec4 f4SunMoonNormal INIT;
	vec4 f4SunColor INIT;
	vec4 f4MoonColor INIT;
	vec4 f4AmbientColor INIT;

	// Per-target sun/moon intensity multipliers (applied at shader read sites). Terrain/Water/Objects are
	// pre-folded CPU-side into the precomputed products below, so only Smoke remains as a raw multiplier.
	float fSunIntensitySmoke INIT;
	float fMoonIntensitySmoke INIT;

	// Precomputed day-cycle products (CPU-folded uniform-only terms; GlobalUniforms.cpp
	// PopulateDayCycleColors / PopulateSunMoonDirection / PopulateShadowParameters). Every operand is
	// invocation-invariant, so folding them removes the equivalent per-pixel multiplies/dots/normalizes.
	vec4 f4SunColorTerrain INIT;      // fSunIntensityTerrain * f4SunColor (SunLighting)
	vec4 f4MoonColorTerrain INIT;     // fMoonIntensityTerrain * f4MoonColor (SunLighting)
	vec4 f4TerrainSnowSunNormal INIT; // fTerrainSnowBlend * f4SunMoonNormal (Terrain.frag snow sun-normal tilt); w unused
	vec4 f4WaterBiasedSunNormal INIT; // normalize((0,0,fLightingWaterSkyboxSunBias) + f4SunMoonNormal) (Water.frag skybox specular); w unused
	vec4 f4SmokeBaseLighting INIT;    // max(fSunIntensitySmoke*f4SunColor, fMoonIntensitySmoke*f4MoonColor) + f4AmbientColor (AddSmoke/BlendSmokePrecomputed); w unused
	vec4 f4WaterSunOrMoon INIT;       // max(fSunIntensityWater*f4SunColor, fMoonIntensityWater*f4MoonColor) (Water.frag skybox sun tint); w unused
	vec4 f4AmbientUnshadowed INIT;    // (1 - fShadowAffectAmbient) * f4AmbientColor (SunLighting ambient, unshadowed half); w unused
	vec4 f4AmbientShadowed INIT;      // fShadowAffectAmbient * f4AmbientColor (SunLighting ambient, shadowed half); w unused
	float fSunMagTerrain INIT;        // Rec.601 luma of f4SunColorTerrain (SunLighting ambient-shadow blend)
	float fMoonMagTerrain INIT;       // Rec.601 luma of f4MoonColorTerrain (SunLighting ambient-shadow blend)
	float fSunMoonMagSumInvTerrain INIT; // 1 / max(fSunMagTerrain + fMoonMagTerrain, 0.001)
	float fWaterSunScalar INIT;       // (f4WaterSunOrMoon.x+y+z)/3 (Water.frag sun contribution)
	float fWaterAmbientScalar INIT;   // (f4AmbientColor.x+y+z)/3 (Water.frag ambient contribution)
	float fWaterSunWeight INIT;       // Rec.601 luma of fSunIntensityWater*f4SunColor (Water.frag effective-shadow blend)
	float fWaterMoonWeight INIT;      // Rec.601 luma of fMoonIntensityWater*f4MoonColor (Water.frag effective-shadow blend)
	float fWaterShadowWeightSumInv INIT; // 1 / max(fWaterSunWeight + fWaterMoonWeight, 0.001)
	float fSmokeLightingCombinedMultiplier INIT; // fSmokeLightingMultiplier * fLightingTimeOfDayMultiplier (AddSmoke/BlendSmokePrecomputed)

	// Objects (Model.frag) precomputed sun/moon products. Direct BRDF and IBL specular are kept as separate
	// fields so the IBL path stays linear in fPbrSun (its Rec.709 luminance uses the UNSCALED color; folding
	// fPbrSun into that luminance would compound to fPbrSun^3). w unused.
	vec4 f4PbrSunColorObjects INIT;     // fSunIntensityObjects * fPbrSun * f4SunColor (Model.frag direct BRDF sun term)
	vec4 f4PbrMoonColorObjects INIT;    // fMoonIntensityObjects * fPbrSun * f4MoonColor (Model.frag direct BRDF moon term)
	vec4 f4PbrSunColorObjectsIbl INIT;  // (dot(f4SunColor.rgb, kRec709) / fPbrDayBrightness) * f4PbrSunColorObjects (Model.frag IBL specular sun term)
	vec4 f4PbrMoonColorObjectsIbl INIT; // (dot(f4MoonColor.rgb, kRec709) / fPbrDayBrightness) * f4PbrMoonColorObjects (Model.frag IBL specular moon term)

	// Smoke
	vec4 f4SmokeArea INIT;
	vec4 f4PreviousSmokeArea INIT;
	vec2 f2PreviousSmokeAreaSizeInv INIT; // (1/(z-x), 1/(w-y)) of f4PreviousSmokeArea (Smoke/Wind OccupancyDilate previous-area remap)
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
	vec2 f2WindTilesInv INIT; // (1/uiWindTilesX, 1/uiWindTilesY) (WindOccupancyDilate output-tile-center UV)

	// Lighting
	float fLightingObjectsAdd INIT;
	float fLightingDepositThreshold INIT;
	float fLightingDepositCompress INIT;
	// Uchimura tone curve inputs (LightCombine.comp / DebugTexture.frag). fCombineLinearLength folds CPU-side into
	// fCombineS0/fCombineS1; fCombineCP is the segment CP constant (epsilon-guarded on P - S1 in LightingUniforms.cpp).
	float fCombineMaxBrightness INIT;
	float fCombineContrast INIT;
	float fCombineLinearStart INIT;
	float fCombineToe INIT;
	float fCombineBlackTightness INIT;
	float fCombineS0 INIT;
	float fCombineS1 INIT;
	float fCombineCP INIT;
	float fCombineHuePreserve INIT;
	// Pass normalization/exposure scaling precomputed: fCombinePassNormScale = passNorm * passScale (DebugTexture.frag),
	// fCombinePassTotalScale = fCombinePassNormScale / passCount (LightCombine.comp averaging).
	float fCombinePassNormScale INIT;
	float fCombinePassTotalScale INIT;
	float pfCombineCurvePoints[kiMaxSpreadPasses] INIT;
	float fLightingTerrain INIT;
	float fLightingObjects INIT;

	float fLightingAddTerrain INIT;
	float fSpreadDirectionalityStart INIT;
	vec4 f4LightingArea INIT;
	vec4 f4LightingAreaPrevious INIT; // Previous-frame f4LightingArea, for LightingTemporal.comp reprojection
	vec2 f2LightingAreaExtentInv INIT; // 1 / lighting-area extent (LightingSpread.frag world->texcoord)
	vec2 f2CombineMargin INIT; // 2-texel bilinear window margin in visible-area UV; LightCombine.comp + LightingTemporal.comp share the combine extent
	float fLightingTemporalBlend INIT; // EMA weight toward current; 1.0 on the first frame so seeded history is never shown
	vec2 f2LightingDepositSizeInv INIT; // 1 / (lightTiles * kiComputeTileSize) (LightingDepositEdgeFade)

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
	float fSpreadHeightEndHeightInv INIT; // 1 / max(spreadHeightEndHeight, 0.001) (LightingSpread.frag height fade)
	float fSpreadHeightPower INIT;

	vec2 f2SpreadMargin INIT; // max(spreadDistanceStart, spreadDistanceEnd) / visible extent (LightingSpread.frag window early-out)

	// Shadow
	float fShadowWidthScale INIT;
	float fShadowDirectionMultiplier INIT;
	float fShadowMoonMultiplier INIT;
	float fShadowFeather INIT;
	float fShadowDistanceFalloff INIT;
	float fShadowDistanceFalloffInv INIT;
	float pfShadowBlurWeights[kiShadowBlurRadius + 1] INIT; // Symmetric Gaussian half-kernel: ShadowBlurH/V index by abs(offset), scale by fShadowBlurWeightSumInv.
	float fShadowBlurWeightSumInv INIT;
	float fShadowAngleOffsetSum INIT; // Shadow sun angle + noon + sunset feather offsets, summed CPU-side (Shadow.comp feather term).
	float fObjectShadowsBlurSigma INIT;
	float fObjectShadowsIntensity INIT;
	float fObjectShadowsGrow INIT;
	int32_t iObjectShadowsBlurRadius INIT;
	float fShadowSunriseStretchCubed INIT; // (fSunriseStretch * gObjectShadowsSunsetStretch)^3 (ShadowStretchProjection)
	float fShadowSunsetStretchCubed INIT;  // (fSunsetStretch  * gObjectShadowsSunsetStretch)^3 (ShadowStretchProjection)
	vec2 f2ShadowStretchTranslation INIT;  // (sunrise+sunset cubes) * -f4SunMoonNormal.xy (ShadowStretchProjection base translation)
	float fShadowAffectAmbient INIT;
	float fWaterReducedNoiseOriginX INIT;
	int32_t iShadowElevationSize INIT;
	int32_t iShadowIncrement INIT;
	int32_t iShadowStartOffset INIT;
	float fWaterReducedNoiseOriginY INIT;
	int32_t iShadowTextureWidth INIT;  // Integer extent for texel-index clamps (ShadowLinearFootprintInsideBounds, ShadowTemporal); f2ShadowTextureSizeInv serves the UV scales.
	int32_t iShadowTextureHeight INIT;
	int32_t iShadowVisibleMinX INIT;
	int32_t iShadowVisibleMinY INIT;
	int32_t iShadowVisibleMaxX INIT;
	int32_t iShadowVisibleMaxY INIT;
	int32_t iShadowSourceMinX INIT;
	int32_t iShadowSourceMinY INIT;
	int32_t iShadowSourceMaxX INIT;
	int32_t iShadowSourceMaxY INIT;
	int32_t iShadowBlurHMinX INIT;
	int32_t iShadowBlurHMinY INIT;
	int32_t iShadowBlurHMaxX INIT;
	int32_t iShadowBlurHMaxY INIT;
	int32_t iShadowFinalMinX INIT;
	int32_t iShadowFinalMinY INIT;
	int32_t iShadowFinalMaxX INIT;
	int32_t iShadowFinalMaxY INIT;
	int32_t iShadowVisiblePreviousMinX INIT;
	int32_t iShadowVisiblePreviousMinY INIT;
	int32_t iShadowVisiblePreviousMaxX INIT;
	int32_t iShadowVisiblePreviousMaxY INIT;
	float fShadowTemporalBlend INIT; // ShadowTemporal.comp: weight of the current frame (1.0 = no history)

	// Terrain. Heightmap pixels carry absolute meters directly — fIslandHeight / fWaterDepth
	// scale factors retired with the meters-everywhere refactor.
	float fIslandAmbientOcclusion INIT;
	float fWaterEarlyOut INIT;
	float fWaterReducedNormalOriginX INIT;
	float fWaterReducedNormalOriginY INIT;
	float fWaterReducedNormalOriginTwoX INIT;
	float fWaterReducedNormalOriginTwoY INIT;
	float fWaterReducedNormalOriginThreeX INIT;
	float fWaterReducedNormalOriginThreeY INIT;
	vec4 f4WaterNormalRotationOne INIT;
	vec4 f4WaterNormalRotationTwo INIT;
	vec4 f4WaterNormalRotationThree INIT;

	// Water
	int32_t iWaterLowCount INIT;
	int32_t iWaterMediumCount INIT;
	int32_t iWaterActiveQuadX INIT;
	int32_t iWaterActiveQuadY INIT;
	float fWaterOriginX INIT;
	float fWaterOriginY INIT;
	float fWaterHeight INIT;
	float fWaterTerrainHeight INIT;
	float fWaterTerrainFadeInv INIT; // 1 / gWaterTerrainFade (Water.frag alpha fade); unguarded (reproduces the shader's original divide)
	float fWaterTerrainFadeClamp INIT;
	float fWaterDepthLutFeather INIT;
	float fWaterDepthColorFeather INIT;
	float fWaterDepthColorFloor INIT;
	float fWaterUnderseaCompressionInv INIT; // 1 / gWaterUnderseaCompression = TerrainElevation.frag undersea depth-curve pow() exponent
	float fWaterDepthReflectionFeatherInv INIT; // 1 / (fDayPercent * gWaterDepthReflectionFeather); unguarded (night dayPercent=0 -> +inf absorbed by the shader clamp)
	float fWaterColorNoiseFrequency INIT;
	float fWaterDepthLutSunsetFade INIT;
	float fWaterFresnel INIT;
	float fWaterColorBottom INIT;
	float fWaterColorHeightInv INIT;
	float fWaterColorNoiseAmount INIT;
	float fWaterColorNoiseWeightOne INIT;
	float fWaterColorNoiseWeightTwo INIT;
	float fWaterColorNoiseMultiplierOne INIT;
	float fWaterColorNoiseMultiplierTwo INIT;
	float fWaterDirectional INIT;
	float fBeachFadeTop INIT;
	float fBeachFadeInvRange INIT;
	float fWaterLowSteepness INIT;
	float fWaterMediumSteepness INIT;
	float fWaterWaveNormalBlend INIT;
	float fWaterLowAmplitude INIT;
	float fWaterMediumAmplitude INIT;
	float fWaterZOffsetTemp INIT; // DT: TEMP
	float fWaterReducedNormalTimeX INIT;
	float fWaterReducedNormalTimeY INIT;
	float fWaterReducedNormalTimeTwoX INIT;
	float fWaterReducedNormalTimeTwoY INIT;
	float fWaterReducedNormalTimeThreeX INIT;
	float fWaterReducedNormalTimeThreeY INIT;

	// Particles
	float fParticlesStretchVelocityStart INIT;
	float fParticlesStretchVelocityMultiplier INIT;
	float fParticlesStretchRangeInv INIT; // 1 / max(stretchVelocityEnd - stretchVelocityStart, kfEpsilon) (LongParticlesRender.vert length ramp)

	// Shadow
	vec2 f2ShadowTextureSizeInv INIT; // 1 / shadow texture extent (ShadowBlurH/V, ShadowTemporal).
	vec2 f2ShadowElevationTextureSizeInv INIT; // 1 / elevation texture extent (Shadow.comp).
	float fShadowHeightFadeBottom INIT;
	float fShadowHeightFadeRangeInv INIT; // 1 / (fade top - fade bottom); no guard, reproduces Shadow.comp's original divide.

	// Terrain
	float fTerrainSnowAmbientOcclusionExclusion INIT;

	float fTerrainRockSize INIT;
	float fTerrainRockBlend INIT;
	float fTerrainRockNormalsSizeOne INIT;
	float fTerrainRockNormalsSizeTwo INIT;
	float fTerrainRockNormalsSizeThree INIT;
	float fTerrainRockNormalsBlend INIT;

	float fTerrainBeachSandSize INIT;
	float fTerrainBeachSandBlend INIT;
	float fTerrainBeachNormalsSizeOne INIT;
	float fTerrainBeachNormalsSizeTwo INIT;
	float fTerrainBeachNormalsSizeThree INIT;
	float fTerrainBeachNormalsBlend INIT;

	// Time of day
	float fLightingTimeOfDayMultiplier INIT;
	float fLightingWaterSkyboxOne INIT;
	float fLightingWaterSkyboxNormalSoften INIT;

	// Debug
	float fDebugTextureIndex INIT;
	float fDebugTextureFormat INIT;
	float fDebugTextureLinearRange INIT;
	float fSeaFloorElevation INIT;
	float fSeaFloorElevationInv INIT; // 1 / fSeaFloorElevation (TerrainElevation.frag undersea depth normalization)
	float fUnderwaterMaskThreshold INIT;
	float fDebugTerrainElevationHigh INIT;
};

struct MainLayout
{
	int32_t iFrameNumber INIT;
	int32_t iRenderNumber INIT;

	vec4 f4x4ViewProjection[4] INIT;

	vec4 f4EyePosition INIT;
	vec4 f4ToEyeNormal INIT;
	// Camera-facing billboard basis derived CPU-side from f4ToEyeNormal (DebugRenderBillboard.vert); forward
	// stays f4ToEyeNormal. Mirrors the shader's worldUp-select (0.999 threshold, +X fallback). w unused.
	vec4 f4BillboardRight INIT;
	vec4 f4BillboardUp INIT;

	vec4 pf4LowWavesOne[256] INIT;
	vec4 pf4LowWavesTwo[256] INIT;

	vec4 pf4MediumWavesOne[256] INIT;
	vec4 pf4MediumWavesTwo[256] INIT;

	// Lighting — water normal map atlas (3 weighted samples)
	float fLightingSampledNormalsOneSize INIT;
	float fLightingSampledNormalsTwoSize INIT;
	float fLightingSampledNormalsThreeSize INIT;
	uint32_t uiWaterNormalIndexOne INIT;
	uint32_t uiWaterNormalIndexTwo INIT;
	uint32_t uiWaterNormalIndexThree INIT;
	float fWaterNormalWeightOne INIT;
	float fWaterNormalWeightTwo INIT;
	float fWaterNormalWeightThree INIT;
	float fWaterNormalWRelSqOne INIT;    // (fWaterNormalWeightOne / weightTotal)^2 (Water.frag MIP_HANDOFF variance share)
	float fWaterNormalWRelSqTwo INIT;
	float fWaterNormalWRelSqThree INIT;
	float fWaterNormalWeightSumInv INIT; // 1 / max(3*(fWaterNormalWeightOne+Two+Three), kfEpsilon) (Water.frag mode-3 agreement)
	float fWaterHeightDarkenBottom INIT;
	float fWaterHeightDarkenRangeInv INIT; // 1 / (gWaterHeightDarkenTop - fWaterHeightDarkenBottom) (Water.frag height darken); unguarded
	float fWaterHeightDarkenTarget INIT;
	float fWaterHeightDarkenSource INIT;
	float fWaterHeightDarkenLighting INIT;

	float fLightingWaterSkyboxNormalBlendWave INIT;
	float fLightingWaterSkyboxIntensity INIT;
	float fLightingWaterSkyboxAdd INIT;
	float fLightingWaterSkyboxOnePower INIT;
	float fLightingWaterSkyboxTwo INIT;
	float fLightingWaterSkyboxTwoPower INIT;
	float fLightingWaterSkyboxThree INIT;
	float fLightingWaterSkyboxThreePower INIT;
	// Per-lobe FilteredPowerLobe (Water.frag WATER_SPEC_AA_MODE 2/3) constants folded from the skybox lobe
	// powers: xyz = lobes One/Two/Three. The varying (1+p') numerator math stays in-shader.
	vec4 f4WaterSkyboxLobeAlphaSq INIT;        // 2/(power+2) per lobe (Beckmann-equivalent kernel base)
	vec4 f4WaterSkyboxLobeOnePlusPowerInv INIT; // 1/(1+power) per lobe (amplitude normalization)
	float fLightingWaterSkyboxLod INIT;
	// Specular-AA tuning for Water.frag's WATER_SPEC_AA_MODE variants (variance: modes 1-3; threshold: modes 2-3)
	float fWaterSpecAAVariance INIT;
	float fWaterSpecAAThreshold INIT;
	// WATER_SPEC_AA_MIP_HANDOFF inputs: per-mip Toksvig variance tables for the three selected
	// octave-group textures (DataPacker-baked into TextureHeader, padded past the real chain with
	// the last value), the handoff scale slider, the water-normal sampler's mip LOD bias (added to
	// the shader's analytic LOD so the table lookup tracks the hardware fetch), and the three
	// near-camera full-detail weights (WATER_SPEC_AA_FADE_HANDOFF reference; ratio-clamped in-shader)
	float pfWaterSpecAAMipVariance[3 * kiWaterSpecAAMipTableSize] INIT;
	float fWaterSpecAAMipScale INIT;
	float fWaterNormalMipBias INIT;
	float fWaterNormalWeightFullOne INIT;
	float fWaterNormalWeightFullTwo INIT;
	float fWaterNormalWeightFullThree INIT;

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

	float fLightingDirectionalIntensity INIT;
	float fLightingDirectionalPower INIT;
	float fLightingDirectionalPowerMode INIT;
	float fLightingAmbientIntensity INIT;
	float fLightingAmbientPower INIT;
	float fLightingAmbientPowerMode INIT;
	float fLightingWaterEwnsPow INIT;
	float fLightingWaterEwnsPowMode INIT;
	float fLightingWaterAmbientIntensity INIT;
	float fLightingWaterAmbientPower INIT;
	float fLightingWaterAmbientPowerMode INIT;
	float fLightingTerrainBelowBaseMultiplier INIT;
	float fLightingTerrainBelowBasePower INIT;

	// Pbr
	float fPbrExposure INIT;
	float fPbrGammaInv INIT; // 1 / gamma, applied in HdrResolve.frag
	float fColorGradingSaturation INIT;
	float fColorGradingContrast INIT;
	float fColorGradingTemperature INIT;
	float fPbrDayBrightness INIT;
	float fPbrAmbient INIT;

	float fPbrMipCount INIT;
	float fPbrSmoke INIT;

	float fPbrBrdfDiffuse INIT;
	float fPbrBrdfDiffusePower INIT;
	float fPbrBrdfSpecular INIT;
	float fPbrBrdfSpecularPower INIT;
	float fPbrIblDiffuse INIT;
	float fPbrIblDiffusePower INIT;
	float fPbrIblSpecular INIT;
	float fPbrIblSpecularPower INIT;
	float fPbrSun INIT;
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
	uint32_t uiTextureSlot INIT; // per-instance bindless texture-array index; 0 for non-island quads (templates assign at AcquireTextureSlot).
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
