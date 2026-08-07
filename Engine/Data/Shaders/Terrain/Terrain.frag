#version 460

#extension GL_EXT_nonuniform_qualifier : require

#include "ShaderLayouts.h"
#include "ShaderFunctions.h"

// Uniform buffers
layout (set = 0, binding = kiGlobalBindingGlobalUniform) uniform globalUniform
{
	GlobalLayout globalLayout;
};

layout (set = 0, binding = kiGlobalBindingMainUniform) uniform mainUniform
{
	MainLayout mainLayout;
};

layout (set = 1, binding = 2) uniform sampler2D pLightingSamplers[3];
layout (set = 1, binding = 3) uniform sampler2D shadowTextureSampler;
layout (set = 1, binding = 4) uniform sampler2D objectShadowsTextureSampler;
layout (set = 1, binding = 5) uniform sampler2D elevationTextureSampler;
// Bindless per-island arrays; each is indexed
// per-fragment by the flat-interpolated `uiInTextureSlot` forwarded from Terrain.vert.
layout (set = 1, binding = 6) uniform sampler2D colorTextureSamplers[kiMaxIslands];
layout (set = 1, binding = 7) uniform sampler2D normalTextureSamplers[kiMaxIslands];
layout (set = 1, binding = 8) uniform sampler2D ambientOcclusionTextureSamplers[kiMaxIslands];
layout (set = 1, binding = 9) uniform sampler2D smokeSampler;
layout (set = 1, binding = 10) uniform sampler2D rockSampler;
layout (set = 1, binding = 11) uniform sampler2D sandNormalsSampler0;
layout (set = 1, binding = 12) uniform sampler2D sandNormalsSampler1;
layout (set = 1, binding = 13) uniform sampler2D sandNormalsSampler2;
layout (set = 1, binding = 14) uniform sampler2D sandSampler;
layout (set = 1, binding = 15) uniform sampler2D rockNormalsSampler0;
layout (set = 1, binding = 16) uniform sampler2D rockNormalsSampler1;
layout (set = 1, binding = 17) uniform sampler2D rockNormalsSampler2;
layout (set = 1, binding = 18) uniform sampler2D ambientLightingSampler;
// Per-island material masks (BC7 RGBA). R=Rock, G=Sand, B=Snow, A=Flow (reserved) — drives material
// detail blending. Bound at binding 20 — the SSBO at
// binding 19 is owned by Terrain.vert (AxisAlignedQuadLayout instance buffer).
layout (set = 1, binding = 20) uniform sampler2D masksTextureSamplers[kiMaxIslands];

// Input
layout (location = 0) in vec2 f2InVisibleAreaTexcoord;
layout (location = 1) in vec2 f2InIslandTexcoord;
layout (location = 2) in flat uint uiInTextureSlot;
layout (location = 3) in flat vec2 f2InRotationCosSin;

// Output
layout (location = 0) out vec4 f4OutColor;

void main()
{
	vec3 f3InPosition = vec3
	(
		VisibleAreaToWorld(f2InVisibleAreaTexcoord, globalLayout.f4VisibleArea),
		texture(elevationTextureSampler, f2InVisibleAreaTexcoord).x
	);

	// No terrain early-out discard here: the per-island Gaea Mesher mesh only draws where geometry
	// exists (including the underwater skirt), so a discard would black-out valid underwater content.
	// The mesh is cropped to the heightmap bbox at bake time (ProcessBakedRegion.cpp), so no fringe
	// overshoot survives to sample neighbouring composite content.

	vec3 f3Color = texture(colorTextureSamplers[nonuniformEXT(uiInTextureSlot)], f2InIslandTexcoord).xyz;

	// BC5 normal: source stores RG (tangent X, Y) only. Decode `2x-1` to [-1, +1], rotate by the
	// per-island (cos, sin), reconstruct Z.
	vec2 f2NormalRG = texture(normalTextureSamplers[nonuniformEXT(uiInTextureSlot)], f2InIslandTexcoord).rg;
	vec2 f2NormalXY = 2.0f * f2NormalRG - 1.0f;
	float fNormalCos = f2InRotationCosSin.x;
	float fNormalSin = f2InRotationCosSin.y;
	vec2 f2NormalRot = vec2(f2NormalXY.x * fNormalCos - f2NormalXY.y * fNormalSin,
	                        f2NormalXY.x * fNormalSin + f2NormalXY.y * fNormalCos);
	float fNormalZ = sqrt(clamp(1.0f - dot(f2NormalRot, f2NormalRot), 0.0f, 1.0f));
	vec3 f3Normal = normalize(vec3(f2NormalRot, fNormalZ));

	// Material masks: R=Rock, G=Sand, B=Snow, A=Flow (reserved). DataPacker packs four Gaea-authored
	// grayscale masks into a single BC7 RGBA texture per island, driving material detail blending.
	// Snow takes priority over rock/sand — snow-painted pixels suppress those blends proportionally.
	vec4 f4Masks = texture(masksTextureSamplers[nonuniformEXT(uiInTextureSlot)], f2InIslandTexcoord);
	float fSnowPercent  = f4Masks.b;
	float fRockPercent  = f4Masks.r * (1.0f - fSnowPercent);
	float fBeachPercent = f4Masks.g * (1.0f - fSnowPercent);
	vec2 f2PositionGradX = dFdx(f3InPosition.xy);
	vec2 f2PositionGradY = dFdy(f3InPosition.xy);
	vec2 f2RockPositionGradX = dFdx(f3InPosition.xy + f3InPosition.z);
	vec2 f2RockPositionGradY = dFdy(f3InPosition.xy + f3InPosition.z);

	if (fRockPercent > 0.001f)
	{
		if (globalLayout.fTerrainRockNormalsBlend > 0.0f)
		{
			vec3 f3RockNormalSum = SampleNormal(globalLayout, rockNormalsSampler0, f3InPosition.xy + f3InPosition.z, globalLayout.fTerrainRockNormalsSizeOne, 0.0f, vec2(0.0f, 0.0f), f2RockPositionGradX, f2RockPositionGradY) + SampleNormal(globalLayout, rockNormalsSampler1, f3InPosition.xy + f3InPosition.z, globalLayout.fTerrainRockNormalsSizeTwo, 0.0f, vec2(0.0f, 0.0f), f2RockPositionGradX, f2RockPositionGradY) + SampleNormal(globalLayout, rockNormalsSampler2, f3InPosition.xy + f3InPosition.z, globalLayout.fTerrainRockNormalsSizeThree, 0.0f, vec2(0.0f, 0.0f), f2RockPositionGradX, f2RockPositionGradY);
			vec3 f3RockNormal = normalize(f3RockNormalSum);
			f3Normal = normalize(f3Normal + globalLayout.fTerrainRockNormalsBlend * fRockPercent * f3RockNormal);
		}

		f3Color = mix(f3Color, textureGrad(rockSampler, globalLayout.fTerrainRockSize * f3InPosition.xy, globalLayout.fTerrainRockSize * f2PositionGradX, globalLayout.fTerrainRockSize * f2PositionGradY).xyz, globalLayout.fTerrainRockBlend * fRockPercent);
	}

	if (fBeachPercent > 0.001f)
	{
		if (globalLayout.fTerrainBeachNormalsBlend > 0.0f)
		{
			vec3 f3BeachNormalSum = 2.0f * SampleNormal(globalLayout, sandNormalsSampler0, f3InPosition.xy, globalLayout.fTerrainBeachNormalsSizeOne, 0.0f, vec2(0.0f, 0.0f), f2PositionGradX, f2PositionGradY) +
			                            0.5f * SampleNormal(globalLayout, sandNormalsSampler1, f3InPosition.yx, globalLayout.fTerrainBeachNormalsSizeTwo, 0.0f, vec2(0.0f, 0.0f), f2PositionGradX.yx, f2PositionGradY.yx) +
			                            1.0f * SampleNormal(globalLayout, sandNormalsSampler2, f3InPosition.yx, globalLayout.fTerrainBeachNormalsSizeThree, 0.0f, vec2(0.0f, 0.0f), f2PositionGradX.yx, f2PositionGradY.yx);
			vec3 f3BeachNormal = f3BeachNormalSum;
			f3BeachNormal.z = 0.0f;
			f3Normal = normalize(f3Normal + globalLayout.fTerrainBeachNormalsBlend * fBeachPercent * f3BeachNormal);
		}

		f3Color = mix(f3Color, textureGrad(sandSampler, globalLayout.fTerrainBeachSandSize * f3InPosition.xy, globalLayout.fTerrainBeachSandSize * f2PositionGradX, globalLayout.fTerrainBeachSandSize * f2PositionGradY).xyz, globalLayout.fTerrainBeachSandBlend * fBeachPercent);
	}

	// f4TerrainSnowSunNormal = fTerrainSnowBlend * f4SunMoonNormal (uniform product folded CPU-side).
	vec3 f3SunNormal = normalize(f3Normal + fSnowPercent * globalLayout.f4TerrainSnowSunNormal.xyz);

	// Sample lighting texture, at world x/y and at projected base-height x/y
	vec2 f2LightingTexcoord = WorldToVisibleArea(f3InPosition, globalLayout.f4LightingArea);
	vec4 pf4Lighting[3];
	ReadLighting(pf4Lighting, pLightingSamplers, f2LightingTexcoord);
	vec2 f2PositionAtBaseHeight = BaseHeightPosition(globalLayout, mainLayout, f3InPosition);
	vec2 f2LightingTexcoordBaseHeight = WorldToVisibleArea(vec3(f2PositionAtBaseHeight, 0.0f), globalLayout.f4LightingArea);
	// Direction-averaged base-height lighting (single fetch replaces three EWNS samples — feeds both ambient and BlendSmoke).
	vec3 f3AmbientSum = ReadAmbientLighting(ambientLightingSampler, f2LightingTexcoordBaseHeight);

	// Apply directional and ambient lighting
	vec3 f3Directional = DirectionalLighting(pf4Lighting, f3Normal, mainLayout.fLightingDirectionalIntensity, mainLayout.fLightingDirectionalPower, mainLayout.fLightingDirectionalPowerMode);
	vec3 f3Ambient = AmbientLightingPrecomputed(f3AmbientSum, mainLayout.fLightingAmbientIntensity, mainLayout.fLightingAmbientPower, mainLayout.fLightingAmbientPowerMode);
	vec3 f3Lighting = globalLayout.fLightingTerrain * globalLayout.fLightingTimeOfDayMultiplier * (f3Directional + f3Ambient);
	float fHeightRatio = clamp(f3InPosition.z * globalLayout.fBaseHeightInv, 0.0, 1.0);
	f3Lighting *= mix(mainLayout.fLightingTerrainBelowBaseMultiplier, 1.0, pow(fHeightRatio, mainLayout.fLightingTerrainBelowBasePower));

	// DT: TEMP — show only lighting texture contributions (with normals and base color)
#ifdef DT_LIGHTING_ONLY
	f4OutColor = vec4(f3Lighting * mix(f3Color, vec3(1.0f), globalLayout.fLightingAddTerrain), 1.0f);
	return;
#endif

	// Shadow with smoke at world position. Moon bypasses the terrain ray-march shadow only;
	// object shadows and smoke volumetric attenuation still apply to both lights.
	float fShadowMoon = SmokeShadow(globalLayout, f3InPosition, smokeSampler, mainLayout.fSmokeShadowIntensity) * texture(objectShadowsTextureSampler, f2InVisibleAreaTexcoord).x;
	float fShadowSun  = fShadowMoon * SampleTerrainShadow(shadowTextureSampler, WorldToVisibleArea(f3InPosition, globalLayout.f4ShadowArea));
	// AO: sample the per-island occlusion and fold `fIslandAmbientOcclusion` into the SunLighting
	// occlusion factor below.
	float fAmbientOcclusionRaw = texture(ambientOcclusionTextureSamplers[nonuniformEXT(uiInTextureSlot)], f2InIslandTexcoord).x;
	// Snow pixels skip AO darkening so accumulated snow looks fresh / bright rather than crevice-shaded.
	float fAmbientOcclusionFactor = 1.0f - (1.0f - fSnowPercent * globalLayout.fTerrainSnowAmbientOcclusionExclusion) * globalLayout.fIslandAmbientOcclusion * (1.0f - fAmbientOcclusionRaw);
	f4OutColor = vec4(SunLighting(f3Color, globalLayout, vec4(f3InPosition, 1.0f), f3SunNormal, fShadowSun, fShadowMoon, fAmbientOcclusionFactor), 1.0f);
	f4OutColor.xyz += f3Lighting * mix(f3Color, vec3(1.0f), globalLayout.fLightingAddTerrain);

	// Sample smoke at base-height projected position, affected by lighting at base-height projected position
	vec2 f2SmokeTexcoord = WorldToSmokeTexcoord(globalLayout.f4SmokeArea, f2PositionAtBaseHeight);
	float fSmokeRaw = globalLayout.fSmokeMax * texture(smokeSampler, f2SmokeTexcoord).x;
	float fSmokePow = clamp(pow(fSmokeRaw, globalLayout.fSmokePower), 0.0f, 1.0f);
	// The 4.0f un-averages the ambient target: Lighting/LightCombine.comp stores 0.25 * (E+W+N+S) and
	// BlendSmokePrecomputed expects the un-averaged sum. Water/Water.frag carries the identical scale.
	f4OutColor.xyz = BlendSmokePrecomputed(f4OutColor.xyz, fSmokePow, 4.0f * f3AmbientSum, globalLayout);
}
