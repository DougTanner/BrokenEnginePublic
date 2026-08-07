// Cook-Torrance microfacet BRDF with metallic-roughness workflow
// References:
// - glTF 2.0 PBR specification (Appendix B)
// - Google Filament documentation
// - LearnOpenGL PBR Theory

#version 460

#extension GL_EXT_nonuniform_qualifier : require

#include "ShaderLayouts.h"
#include "ShaderFunctions.h"

// Debug toggles for lighting contributions
#define ENABLE_BRDF 1
#define ENABLE_IBL 1
#define ENABLE_EMISSIVE 1

#define ENABLE_SPECULAR_LIGHTING 1
#define ENABLE_DIRECTIONAL_LIGHTING 1
#define ENABLE_SMOKE 1

// Push constants
layout(push_constant) uniform pushConstants
{
	PushConstantsLayout pushConstantsLayout;
};

// Uniforms
layout (set = 0, binding = kiGlobalBindingGlobalUniform) uniform globalUniform
{
	GlobalLayout globalLayout;
};

layout (set = 0, binding = kiGlobalBindingMainUniform) uniform mainUniform
{
	MainLayout mainLayout;
};

layout (scalar, set = 1, binding = 2) buffer readonly modelsUniform
{
	ModelLayout pModels[];
};

// Bindless texture array
layout (set = 0, binding = kiGlobalBindingSamplerRepeatModelData) uniform sampler samplerRepeatModelData;
layout (set = 0, binding = kiGlobalBindingSamplerRepeat) uniform sampler samplerRepeat;
layout (set = 0, binding = kiGlobalBindingBindlessTextures) uniform texture2D pTextures[];

// IBL textures
layout (set = 1, binding = 5) uniform samplerCube samplerIrradiance;
layout (set = 1, binding = 6) uniform samplerCube prefilteredMap;
layout (set = 1, binding = 7) uniform sampler2D samplerBRDFLUT;

// Material buffer (per-material, Set 2)
layout (scalar, set = 2, binding = 8) buffer readonly pbrMaterialsUniform
{
	PbrMaterialLayout pMaterials[];
};

// Engine-specific textures
layout (set = 1, binding = 9) uniform sampler2D pLightingSamplers[3];
layout (set = 1, binding = 10) uniform sampler2D shadowTextureSampler;
layout (set = 1, binding = 11) uniform sampler2D smokeSampler;

// Vertex inputs
layout (location = 0) in vec3 f3InWorldPosition;
layout (location = 1) in vec3 f3InNormal;
layout (location = 2) in vec2 f2InUV;
layout (location = 3) in vec2 f2InUV1;
layout (location = 4) in vec2 f2InUV2;
layout (location = 5) in vec2 f2InUV3;
layout (location = 6) in vec2 f2InUV4;
layout (location = 7) in vec4 f4InColorAdd;

// Fragment output
layout (location = 0) out vec4 f4OutColor;

// Constants
const float kMinRoughness = 0.04;
const float kfIblSpecularMax = 16.0; // Firefly clamp: prefiltered sun-disc texels reach F16 max at sharp mips

// Select UV based on texture set index
vec2 getUV(int textureSet)
{
	if (textureSet <= 0) return f2InUV;
	if (textureSet == 1) return f2InUV1;
	if (textureSet == 2) return f2InUV2;
	if (textureSet == 3) return f2InUV3;
	return f2InUV4;
}

// sRGB to linear color space conversion
vec4 SRGBtoLinear(vec4 srgb)
{
	return vec4(pow(srgb.rgb, vec3(2.2)), srgb.a);
}

vec3 SRGBtoLinear(vec3 srgb)
{
	return pow(srgb, vec3(2.2));
}

// GGX/Trowbridge-Reitz normal distribution function
float D_GGX(float NdotH, float alphaRoughness)
{
	float a2 = alphaRoughness * alphaRoughness;
	float f = (NdotH * NdotH) * (a2 - 1.0) + 1.0;
	return a2 / (kfPi * f * f);
}

// Schlick Fresnel approximation
vec3 F_Schlick(float VdotH, vec3 F0, vec3 F90)
{
	float x = clamp(1.0 - VdotH, 0.0, 1.0);
	float x2 = x * x;
	return F0 + (F90 - F0) * (x2 * x2 * x);
}

// Smith-GGX geometry visibility function (separable form)
float V_SmithGGXCorrelated(float NdotL, float NdotV, float alphaRoughness)
{
	float a2 = alphaRoughness * alphaRoughness;
	float GGXV = NdotL * sqrt(NdotV * NdotV * (1.0 - a2) + a2);
	float GGXL = NdotV * sqrt(NdotL * NdotL * (1.0 - a2) + a2);
	float GGX = GGXV + GGXL;
	return GGX > 0.0 ? 0.5 / GGX : 0.0;
}

// Lambertian diffuse BRDF
vec3 DiffuseLambert(vec3 diffuseColor)
{
	return diffuseColor / kfPi;
}

// Compute perturbed normal from normal map using screen-space derivatives
vec3 GetNormal(PbrMaterialLayout material)
{
	vec3 N = normalize(f3InNormal);

	if (material.iNormalTextureSet < 0)
	{
		return N;
	}

	vec3 pos_dx = dFdx(f3InWorldPosition);
	vec3 pos_dy = dFdy(f3InWorldPosition);
	vec2 tex_dx = dFdx(f2InUV);
	vec2 tex_dy = dFdy(f2InUV);

	float det = tex_dx.s * tex_dy.t - tex_dy.s * tex_dx.t;
	if (abs(det) < 1e-8)
	{
		return N;
	}

	vec3 T = (tex_dy.t * pos_dx - tex_dx.t * pos_dy) / det;
	vec3 Tperp = T - N * dot(N, T);
	if (dot(Tperp, Tperp) < kfEpsilon * kfEpsilon)
	{
		return N;
	}
	T = normalize(Tperp);
	// cross of two perpendicular unit vectors is already unit (N is unit; T is unit and Gram-Schmidt-orthogonalized to N above)
	vec3 B = cross(N, T);
	mat3 TBN = mat3(T, B, N);

	// BC5 normal map: only XY stored, reconstruct Z = sqrt(1 - X^2 - Y^2).
	vec2 nXY = texture(sampler2D(pTextures[nonuniformEXT(int(material.fNormalTextureIndex))], samplerRepeatModelData), getUV(material.iNormalTextureSet)).rg * 2.0 - 1.0;
	vec3 tangentNormal = vec3(nXY, sqrt(clamp(1.0 - dot(nXY, nXY), 0.0, 1.0)));
	return normalize(TBN * tangentNormal);
}

// Convert cubemap coordinates from Z-up engine space to Y-up cubemap space
vec3 ToCubemapCoord(vec3 worldNormal)
{
	return vec3(worldNormal.x, worldNormal.z, worldNormal.y);
}

// IBL contribution using split-sum approximation
void GetIBLContribution(float NdotV, float perceptualRoughness, vec3 diffuseColor, vec3 specularColor,
                        vec3 n, vec3 reflection, out vec3 f3Diffuse, out vec3 f3Specular)
{
	float lod = pow(perceptualRoughness, mainLayout.fPbrCubemapLodPower) * mainLayout.fPbrMipCount
	            + perceptualRoughness * mainLayout.fPbrCubemapLodOffset;
	vec3 brdf = texture(samplerBRDFLUT, vec2(NdotV, 1.0 - perceptualRoughness)).rgb;
	vec3 diffuseLight = SRGBtoLinear(texture(samplerIrradiance, ToCubemapCoord(n))).rgb;
	vec3 specularLight = min(SRGBtoLinear(textureLod(prefilteredMap, ToCubemapCoord(reflection), lod)).rgb, vec3(kfIblSpecularMax));
	f3Diffuse = diffuseLight * diffuseColor;
	f3Specular = specularLight * (specularColor * brdf.x + brdf.y);
}

void main()
{
	PbrMaterialLayout material = pMaterials[int32_t(pushConstantsLayout.f4Pipeline.w)];

	// Sample base color
	vec4 baseColor = material.f4BaseColorFactor;
	if (material.iColorTextureSet > -1)
	{
		baseColor *= SRGBtoLinear(texture(sampler2D(pTextures[nonuniformEXT(int(material.fColorTextureIndex))], samplerRepeat), getUV(material.iColorTextureSet)));
	}

	// Metallic-Roughness workflow
	float metallic = material.fMetallicFactor;
	float perceptualRoughness = material.fRoughnessFactor;
	if (material.iPhysicalDescriptorTextureSet > -1)
	{
		vec4 mrSample = texture(sampler2D(pTextures[nonuniformEXT(int(material.fPhysicalDescriptorTextureIndex))], samplerRepeatModelData), getUV(material.iPhysicalDescriptorTextureSet));
		perceptualRoughness *= mrSample.g;
		metallic *= mrSample.b;
	}
	metallic = clamp(metallic, 0.0, 1.0);
	perceptualRoughness = clamp(perceptualRoughness, kMinRoughness, 1.0);

	float alphaRoughness = perceptualRoughness * perceptualRoughness;

	// Energy-conserving diffuse (accounts for light reflected as specular)
	vec3 f0 = vec3(0.04);
	vec3 diffuseColor = baseColor.rgb * (vec3(1.0) - f0);
	diffuseColor *= 1.0 - metallic;

	// Specular color: F0 for dielectrics, baseColor for metals
	vec3 specularColor = mix(f0, baseColor.rgb, metallic);

	// Compute vectors
	vec3 n = GetNormal(material);
	vec3 v = normalize(mainLayout.f4EyePosition.xyz - f3InWorldPosition);
	// f4SunMoonNormal is CPU-normalized (GlobalUniforms.cpp PopulateSunMoonDirection); normalization-skip rule in Engine/Data/Shaders/AGENTS.md.
	vec3 l = globalLayout.f4SunMoonNormal.xyz;
	vec3 h = normalize(l + v);
	vec3 reflection = -reflect(v, n);

	// Compute dot products
	float NdotL = clamp(dot(n, l), 0.001, 1.0);
	float NdotV = clamp(abs(dot(n, v)), 0.001, 1.0);
	float NdotH = clamp(dot(n, h), 0.0, 1.0);
	float LdotH = clamp(dot(l, h), 0.0, 1.0);
	float VdotH = clamp(dot(v, h), 0.0, 1.0);

	// Reflectance at grazing angle
	vec3 reflectance90 = vec3(clamp(max(max(specularColor.r, specularColor.g), specularColor.b) * 25.0, 0.0, 1.0));

	// Sample ambient occlusion
	float ao = 1.0;
	if (material.iOcclusionTextureSet > -1)
	{
		ao = texture(sampler2D(pTextures[nonuniformEXT(int(material.fOcclusionTextureIndex))], samplerRepeat), getUV(material.iOcclusionTextureSet)).r;
	}

	// Engine-specific lighting. Sun and moon split: moon bypasses shadow attenuation (Model.frag samples only
	// the terrain shadow texture), sun still receives it. The sun/moon color terms are precomputed CPU-side
	// (GlobalUniforms.cpp PopulateDayCycleColors): the per-target Objects intensity and fPbrSun fold into
	// f4PbrSun/MoonColorObjects, and the IBL specular path multiplies in the Rec.709 luminance / fPbrDayBrightness
	// via the separate f4PbrSun/MoonColorObjectsIbl. Kept as two fields so the IBL path stays linear in fPbrSun
	// (its luminance comes from the UNSCALED color; folding fPbrSun into the luminance would compound to fPbrSun^3).
	vec3 f3AmbientColor = globalLayout.f4AmbientColor.rgb;

	vec2 f2VisibleAreaPosition = WorldToVisibleArea(f3InWorldPosition, globalLayout.f4VisibleArea);
	vec2 f2LightingPosition = WorldToVisibleArea(f3InWorldPosition, globalLayout.f4LightingArea);
	vec2 f2ShadowPosition = WorldToVisibleArea(f3InWorldPosition, globalLayout.f4ShadowArea);
	float fShadow = max(mainLayout.fPbrShadowFloor, SampleTerrainShadow(shadowTextureSampler, f2ShadowPosition));
	const float fShadowMoon = 1.0; // moon bypasses terrain shadow; Model.frag has no other shadow inputs

	// Accumulate lighting
	vec3 color = vec3(0.0);

	// Cook-Torrance microfacet BRDF for direct sun lighting
	// Combines three terms: F (Fresnel), D (Distribution), V (Visibility)
	// - F: Surface reflectivity increases at grazing angles (Schlick approximation)
	// - D: Microfacet normal distribution controlling highlight shape (GGX/Trowbridge-Reitz)
	// - V: Self-shadowing between microfacets based on roughness (Smith-GGX)
#if ENABLE_BRDF
	vec3 F = F_Schlick(VdotH, specularColor, reflectance90);
	float D = D_GGX(NdotH, alphaRoughness);
	float V = V_SmithGGXCorrelated(NdotL, NdotV, alphaRoughness);
	vec3 diffuseContrib = (1.0 - F) * DiffuseLambert(diffuseColor);
	vec3 specularContrib = F * D * V;
	vec3 diffuseResult = pow(mainLayout.fPbrBrdfDiffuse * diffuseContrib, vec3(mainLayout.fPbrBrdfDiffusePower));
	vec3 specularResult = pow(mainLayout.fPbrBrdfSpecular * specularContrib, vec3(mainLayout.fPbrBrdfSpecularPower));
	vec3 brdf = NdotL * (diffuseResult + specularResult);
	color += max(globalLayout.f4PbrSunColorObjects.rgb  * fShadow * fShadow,
	             globalLayout.f4PbrMoonColorObjects.rgb * fShadowMoon * fShadowMoon) * brdf;
#endif

	// Image based lighting
#if ENABLE_IBL
	vec3 f3IblDiffuse;
	vec3 f3IblSpecular;
	GetIBLContribution(NdotV, perceptualRoughness, diffuseColor, specularColor, n, reflection, f3IblDiffuse, f3IblSpecular);
	f3IblDiffuse *= mainLayout.fPbrAmbient * mix(vec3(1.0), f3AmbientColor, mainLayout.fPbrIblAmbientColorBlend) * mix(1.0, fShadow, mainLayout.fPbrIblShadowBlend);
	f3IblSpecular *= max(globalLayout.f4PbrSunColorObjectsIbl.rgb  * fShadow,
	                     globalLayout.f4PbrMoonColorObjectsIbl.rgb * fShadowMoon);
	vec3 f3IblDiffuseResult = pow(mainLayout.fPbrIblDiffuse * f3IblDiffuse, vec3(mainLayout.fPbrIblDiffusePower));
	vec3 f3IblSpecularResult = pow(mainLayout.fPbrIblSpecular * f3IblSpecular, vec3(mainLayout.fPbrIblSpecularPower));
	color += f3IblDiffuseResult + f3IblSpecularResult;
#endif

	// Apply ambient occlusion
	if (material.iOcclusionTextureSet > -1)
	{
		color *= ao;
	}

	#if 1
	// Sample lighting texture at world x/y (very close to base-height already)
	vec2 f2LightingTexcoord = WorldToVisibleArea(f3InWorldPosition, globalLayout.f4LightingArea);
	vec4 pf4Lighting[3];
	ReadLighting(pf4Lighting, pLightingSamplers, f2LightingTexcoord);

	// Apply directional lighting
	vec3 f3Directional = DirectionalLighting(pf4Lighting, n, mainLayout.fLightingDirectionalIntensity, mainLayout.fLightingDirectionalPower, mainLayout.fLightingDirectionalPowerMode);
	vec3 f3Lighting = globalLayout.fLightingObjects * globalLayout.fLightingTimeOfDayMultiplier * f3Directional;
	vec3 directionalLighting = f3Lighting * mix(baseColor.rgb, vec3(1.0f), globalLayout.fLightingObjectsAdd);
	#else
	// Engine directional lighting
	vec2 f2DirectTexcoord = WorldToVisibleArea(f3InWorldPosition, globalLayout.f4LightingArea);
	vec4 pf4DirectLighting[3];
	ReadLighting(pf4DirectLighting, pLightingSamplers, f2DirectTexcoord);
	vec3 f3Direct = DirectionalLighting(pf4DirectLighting, n, mainLayout.fLightingDirectionalIntensity, mainLayout.fLightingDirectionalPower, mainLayout.fLightingDirectionalPowerMode);

	vec2 f2AmbientPosition = BaseHeightPosition(globalLayout, mainLayout, f3InWorldPosition);
	vec2 f2AmbientTexcoord = WorldToVisibleArea(vec3(f2AmbientPosition, 0.0f), globalLayout.f4LightingArea);
	vec4 pf4Lighting[3];
	ReadLighting(pf4Lighting, pLightingSamplers, f2AmbientTexcoord);
	vec3 f3Ambient = AmbientLighting(pf4Lighting, mainLayout.fLightingAmbientIntensity, mainLayout.fLightingAmbientPower, mainLayout.fLightingAmbientPowerMode);
	pf4Lighting[0] *= mainLayout.fLightingAmbientIntensity;
	pf4Lighting[1] *= mainLayout.fLightingAmbientIntensity;
	pf4Lighting[2] *= mainLayout.fLightingAmbientIntensity;

	vec3 f3NewLighting = (f3Direct + f3Ambient) * globalLayout.fLightingTimeOfDayMultiplier * globalLayout.fLightingObjects;
	vec3 directionalLighting = globalLayout.fLightingObjectsAdd * f3NewLighting + (1.0f - globalLayout.fLightingObjectsAdd) * f3NewLighting * baseColor.rgb;
	#endif

	// Cook-Torrance specular from engine directional lights (EWNS cardinal directions)
#if ENABLE_SPECULAR_LIGHTING
	const vec3 kCardinalDirs[4] = vec3[4](vec3(1.0, 0.0, 0.0), vec3(-1.0, 0.0, 0.0), vec3(0.0, 1.0, 0.0), vec3(0.0, -1.0, 0.0));
	vec3 specLightAccum = vec3(0.0);
	for (int i = 0; i < 4; i++)
	{
		vec3 lDir = kCardinalDirs[i];
		vec3 hDir = normalize(lDir + v);
		float cNdotL = max(dot(n, lDir), 0.0);
		float cNdotH = clamp(dot(n, hDir), 0.0, 1.0);
		float cVdotH = clamp(dot(v, hDir), 0.0, 1.0);

		vec3 cF = F_Schlick(cVdotH, specularColor, reflectance90);
		float cD = D_GGX(cNdotH, alphaRoughness);
		float cV = V_SmithGGXCorrelated(max(cNdotL, 0.001), NdotV, alphaRoughness);
		vec3 specBrdf = cF * cD * cV;

		vec3 lightIntensity = vec3(pf4Lighting[0][i], pf4Lighting[1][i], pf4Lighting[2][i]);
		specLightAccum += cNdotL * specBrdf * lightIntensity;
	}
	color += pow(mainLayout.fPbrLightingSpecular * specLightAccum, vec3(mainLayout.fPbrLightingSpecularPower));
#endif

#if ENABLE_DIRECTIONAL_LIGHTING
	color += pow(mainLayout.fPbrLighting * mainLayout.fPbrDayBrightness * directionalLighting, vec3(mainLayout.fPbrLightingPower));
#endif

	// Add emissive
#if ENABLE_EMISSIVE
	vec3 emissive = material.f4EmissiveFactor.rgb;
	if (material.iEmissiveTextureSet > -1)
	{
		emissive *= SRGBtoLinear(texture(sampler2D(pTextures[nonuniformEXT(int(material.fEmissiveTextureIndex))], samplerRepeat), getUV(material.iEmissiveTextureSet)).rgb);
	}
	color += mainLayout.fPbrEmissive * emissive;
#endif

	// Apply smoke/fog (project position to base height plane, fade with height)
#if ENABLE_SMOKE
	vec2 f2PositionAtBaseHeight = BaseHeightPosition(globalLayout, mainLayout, f3InWorldPosition);
	float fHeightFraction = clamp((f3InWorldPosition.z - globalLayout.fBaseHeight) * globalLayout.fSmokeObjectHeightInv, 0.0f, 1.0f);
	float fSmokeFade = 1.0f - fHeightFraction * fHeightFraction;
	color = AddSmoke(globalLayout, color, f2PositionAtBaseHeight, smokeSampler, fSmokeFade * mainLayout.fPbrSmoke, pf4Lighting);
#endif

	// Output color
	f4OutColor = vec4(color, baseColor.a);

	// Per-instance color add (for effects like damage flash)
	f4OutColor += f4InColorAdd;
}
