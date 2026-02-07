// Based on Cook-Torrance microfacet BRDF model
// References:
// - glTF 2.0 PBR specification
// - Khronos glTF-WebGL-PBR
// - Google Filament documentation
// - LearnOpenGL PBR Theory

#version 460

#extension GL_ARB_separate_shader_objects : require
#extension GL_EXT_shader_explicit_arithmetic_types : require
#extension GL_EXT_nonuniform_qualifier : require

#include "ShaderLayouts.h"
#include "ShaderFunctions.h"

#define ENABLE_ALPHA_MASK 0

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
layout (binding = 0) uniform globalUniform
{
	GlobalLayout globalLayout;
};

layout (binding = 1) uniform mainUniform
{
	MainLayout mainLayout;
};

layout (std430, binding = 2) buffer readonly gltfsUniform
{
	GltfLayout pGltfs[];
};

// Material textures
layout (binding = 3) uniform sampler2D colorMap;
layout (binding = 4) uniform sampler2D physicalDescriptorMap;
layout (binding = 5) uniform sampler2D normalMap;
layout (binding = 6) uniform sampler2D aoMap;
layout (binding = 7) uniform sampler2D emissiveMap;

// IBL textures
layout (binding = 8) uniform samplerCube samplerIrradiance;
layout (binding = 9) uniform samplerCube prefilteredMap;
layout (binding = 10) uniform sampler2D samplerBRDFLUT;

// Material buffer
layout (std430, binding = 11) buffer readonly gltfMaterialsUniform
{
	GltfMaterialLayout pMaterials[];
};

// Engine-specific textures
layout (binding = 12) uniform sampler2D pLightingSamplers[3];
layout (binding = 13) uniform sampler2D shadowTextureSampler;
layout (binding = 14) uniform sampler2D smokeSampler;

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
const float M_PI = 3.141592653589793;
const float c_MinRoughness = 0.04;

// PBR input structure
struct PBRInfo
{
	float NdotL;
	float NdotV;
	float NdotH;
	float LdotH;
	float VdotH;
	float perceptualRoughness;
	float metalness;
	vec3 reflectance0;
	vec3 reflectance90;
	float alphaRoughness;
	vec3 diffuseColor;
	vec3 specularColor;
};

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

// Uncharted 2 tone mapping operator
vec3 Uncharted2Tonemap(vec3 x)
{
	const float A = 0.15;
	const float B = 0.50;
	const float C = 0.10;
	const float D = 0.20;
	const float E = 0.02;
	const float F = 0.30;
	return ((x * (A * x + C * B) + D * E) / (x * (A * x + B) + D * F)) - E / F;
}

// Apply tone mapping with exposure and gamma correction
vec3 Tonemap(vec3 color, float exposure, float gamma)
{
	const float W = 11.2;
	color *= exposure;
	color = Uncharted2Tonemap(color);
	vec3 whiteScale = 1.0 / Uncharted2Tonemap(vec3(W));
	color *= whiteScale;
	return pow(color, vec3(1.0 / gamma));
}

// GGX/Trowbridge-Reitz normal distribution function
float D_GGX(float NdotH, float alphaRoughness)
{
	float a2 = alphaRoughness * alphaRoughness;
	float f = (NdotH * NdotH) * (a2 - 1.0) + 1.0;
	return a2 / (M_PI * f * f);
}

// Schlick Fresnel approximation
vec3 F_Schlick(float VdotH, vec3 F0, vec3 F90)
{
	return F0 + (F90 - F0) * pow(clamp(1.0 - VdotH, 0.0, 1.0), 5.0);
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
	return diffuseColor / M_PI;
}

// Compute perturbed normal from normal map using screen-space derivatives
vec3 GetNormal()
{
	GltfMaterialLayout material = pMaterials[int32_t(pushConstantsLayout.f4Pipeline.w)];

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
	T = normalize(T - N * dot(N, T));
	vec3 B = normalize(cross(N, T));
	mat3 TBN = mat3(T, B, N);

	vec3 tangentNormal = texture(normalMap, getUV(material.iNormalTextureSet)).xyz * 2.0 - 1.0;
	return normalize(TBN * tangentNormal);
}

// Convert cubemap coordinates from Z-up engine space to Y-up cubemap space
vec3 ToCubemapCoord(vec3 worldNormal)
{
	return vec3(worldNormal.x, worldNormal.z, worldNormal.y);
}

// Tonemap for IBL cubemap samples
vec4 TonemapIBL(vec4 color)
{
	vec3 outcol = Uncharted2Tonemap(color.rgb * mainLayout.fGltfExposure);
	outcol = outcol * (1.0 / Uncharted2Tonemap(vec3(11.2)));
	return vec4(pow(outcol, vec3(1.0 / mainLayout.fGltfGamma)), color.a);
}

// IBL contribution using split-sum approximation
void GetIBLContribution(PBRInfo pbrInputs, vec3 n, vec3 reflection, out vec3 f3Diffuse, out vec3 f3Specular)
{
	float lod = pbrInputs.perceptualRoughness * mainLayout.fGltfMipCount;
	vec3 brdf = texture(samplerBRDFLUT, vec2(pbrInputs.NdotV, 1.0 - pbrInputs.perceptualRoughness)).rgb;
	vec3 diffuseLight = SRGBtoLinear(TonemapIBL(texture(samplerIrradiance, ToCubemapCoord(n)))).rgb;
	vec3 specularLight = SRGBtoLinear(TonemapIBL(textureLod(prefilteredMap, ToCubemapCoord(reflection), lod))).rgb;
	f3Diffuse = diffuseLight * pbrInputs.diffuseColor;
	f3Specular = specularLight * (pbrInputs.specularColor * brdf.x + brdf.y);
}

void main()
{
	GltfMaterialLayout material = pMaterials[int32_t(pushConstantsLayout.f4Pipeline.w)];

#if ENABLE_ALPHA_MASK
	// Alpha masking
	if (material.fAlphaMask > 0.0 && baseColor.a < material.fAlphaMaskCutoff)
	{
		discard;
	}
#endif

	// Sample base color
	vec4 baseColor = material.f4BaseColorFactor;
	if (material.iColorTextureSet > -1)
	{
		baseColor *= SRGBtoLinear(texture(colorMap, getUV(material.iColorTextureSet)));
	}

	// Metallic-Roughness workflow
	float metallic = material.fMetallicFactor;
	float perceptualRoughness = material.fRoughnessFactor;
	if (material.iPhysicalDescriptorTextureSet > -1)
	{
		vec4 mrSample = texture(physicalDescriptorMap, getUV(material.iPhysicalDescriptorTextureSet));
		perceptualRoughness *= mrSample.g;
		metallic *= mrSample.b;
	}
	metallic = clamp(metallic, 0.0, 1.0);
	perceptualRoughness = clamp(perceptualRoughness, c_MinRoughness, 1.0);

	float alphaRoughness = perceptualRoughness * perceptualRoughness;

	// Energy-conserving diffuse (accounts for light reflected as specular)
	vec3 f0 = vec3(0.04);
	vec3 diffuseColor = baseColor.rgb * (vec3(1.0) - f0);
	diffuseColor *= 1.0 - metallic;

	// Specular color: F0 for dielectrics, baseColor for metals
	vec3 specularColor = mix(f0, baseColor.rgb, metallic);

	// Compute vectors
	vec3 n = GetNormal();
	vec3 v = normalize(mainLayout.f4EyePosition.xyz - f3InWorldPosition);
	vec3 l = normalize(globalLayout.f4SunNormal.xyz);
	vec3 h = normalize(l + v);
	vec3 reflection = -normalize(reflect(v, n));
	reflection.y *= -1.0;

	// Compute dot products
	float NdotL = clamp(dot(n, l), 0.001, 1.0);
	float NdotV = clamp(abs(dot(n, v)), 0.001, 1.0);
	float NdotH = clamp(dot(n, h), 0.0, 1.0);
	float LdotH = clamp(dot(l, h), 0.0, 1.0);
	float VdotH = clamp(dot(v, h), 0.0, 1.0);

	// Build PBR info structure
	PBRInfo pbrInputs;
	pbrInputs.NdotL = NdotL;
	pbrInputs.NdotV = NdotV;
	pbrInputs.NdotH = NdotH;
	pbrInputs.LdotH = LdotH;
	pbrInputs.VdotH = VdotH;
	pbrInputs.perceptualRoughness = perceptualRoughness;
	pbrInputs.metalness = metallic;
	pbrInputs.reflectance0 = specularColor;
	pbrInputs.reflectance90 = vec3(clamp(max(max(specularColor.r, specularColor.g), specularColor.b) * 25.0, 0.0, 1.0));
	pbrInputs.alphaRoughness = alphaRoughness;
	pbrInputs.diffuseColor = diffuseColor;
	pbrInputs.specularColor = specularColor;

	// Sample ambient occlusion
	float ao = 1.0;
	if (material.iOcclusionTextureSet > -1)
	{
		ao = texture(aoMap, getUV(material.iOcclusionTextureSet)).r;
	}

	// Engine-specific lighting variables
	vec3 f3SunColor = mainLayout.fGltfSun * globalLayout.f4SunColor.rgb;
	float fSunIntensity = mainLayout.fGltfSun * (f3SunColor.r + f3SunColor.g + f3SunColor.b) / mainLayout.fGltfDayBrightness;
	float fSunDot = max(0.0, dot(f3InNormal, globalLayout.f4SunNormal.xyz));

	vec3 f3AmbientColor = globalLayout.f4AmbientColor.rgb;

	vec2 f2VisibleAreaPosition = WorldToVisibleArea(f3InWorldPosition, globalLayout.f4VisibleArea);
	float fShadow = max(mainLayout.fGltfShadowFloor, texture(shadowTextureSampler, f2VisibleAreaPosition).r);

	// Accumulate lighting
	vec3 color = vec3(0.0);

	// Cook-Torrance microfacet BRDF for direct sun lighting
	// Combines three terms: F (Fresnel), D (Distribution), V (Visibility)
	// - F: Surface reflectivity increases at grazing angles (Schlick approximation)
	// - D: Microfacet normal distribution controlling highlight shape (GGX/Trowbridge-Reitz)
	// - V: Self-shadowing between microfacets based on roughness (Smith-GGX)
#if ENABLE_BRDF
	vec3 F = F_Schlick(VdotH, pbrInputs.reflectance0, pbrInputs.reflectance90);
	float D = D_GGX(NdotH, alphaRoughness);
	float V = V_SmithGGXCorrelated(NdotL, NdotV, alphaRoughness);
	vec3 diffuseContrib = (1.0 - F) * DiffuseLambert(diffuseColor);
	vec3 specularContrib = F * D * V;
	vec3 diffuseResult = pow(mainLayout.fGltfBrdfDiffuse * diffuseContrib, vec3(mainLayout.fGltfBrdfDiffusePower));
	vec3 specularResult = pow(mainLayout.fGltfBrdfSpecular * specularContrib, vec3(mainLayout.fGltfBrdfSpecularPower));
	vec3 brdf = NdotL * (diffuseResult + specularResult);
	color += f3SunColor * fShadow * fShadow * brdf;
#endif

	// Image based lighting
#if ENABLE_IBL
	vec3 f3IblDiffuse;
	vec3 f3IblSpecular;
	GetIBLContribution(pbrInputs, n, reflection, f3IblDiffuse, f3IblSpecular);
	f3IblDiffuse *= mainLayout.fGltfAmbient * mix(vec3(1.0), f3AmbientColor, mainLayout.fGltfIblAmbientColorBlend) * mix(1.0, fShadow, mainLayout.fGltfIblShadowBlend);
	f3IblSpecular *= fSunIntensity * f3SunColor * fShadow;
	vec3 f3IblDiffuseResult = pow(mainLayout.fGltfIblDiffuse * f3IblDiffuse, vec3(mainLayout.fGltfIblDiffusePower));
	vec3 f3IblSpecularResult = pow(mainLayout.fGltfIblSpecular * f3IblSpecular, vec3(mainLayout.fGltfIblSpecularPower));
	color += f3IblDiffuseResult + f3IblSpecularResult;
#endif

	// Apply ambient occlusion
	if (material.iOcclusionTextureSet > -1)
	{
		color *= ao;
	}

	// Read engine directional lighting
	vec4 pf4Lighting[3];
	ReadLighting(pf4Lighting, pLightingSamplers, f2VisibleAreaPosition);

	// Cook-Torrance specular from engine directional lights (EWNS cardinal directions)
#if ENABLE_SPECULAR_LIGHTING
	const vec3 kCardinalDirs[4] = vec3[4](vec3(-1.0, 0.0, 0.0), vec3(1.0, 0.0, 0.0), vec3(0.0, -1.0, 0.0), vec3(0.0, 1.0, 0.0));
	vec3 specLightAccum = vec3(0.0);
	for (int i = 0; i < 4; i++)
	{
		vec3 lDir = kCardinalDirs[i];
		vec3 hDir = normalize(lDir + v);
		float cNdotL = max(dot(n, lDir), 0.0);
		float cNdotH = clamp(dot(n, hDir), 0.0, 1.0);
		float cVdotH = clamp(dot(v, hDir), 0.0, 1.0);

		vec3 cF = F_Schlick(cVdotH, pbrInputs.reflectance0, pbrInputs.reflectance90);
		float cD = D_GGX(cNdotH, alphaRoughness);
		float cV = V_SmithGGXCorrelated(max(cNdotL, 0.001), NdotV, alphaRoughness);
		vec3 specBrdf = cF * cD * cV;

		vec3 lightIntensity = vec3(pf4Lighting[0][i], pf4Lighting[1][i], pf4Lighting[2][i]);
		specLightAccum += cNdotL * specBrdf * lightIntensity;
	}
	color += pow(mainLayout.fGltfLightingSpecular * specLightAccum, vec3(mainLayout.fGltfLightingSpecularPower));
#endif

	// Engine directional lighting
#if ENABLE_DIRECTIONAL_LIGHTING
	vec3 directionalLighting = Lighting(globalLayout, baseColor.rgb, f3InWorldPosition.z, n, pf4Lighting, globalLayout.f4LightingTwo.w, globalLayout.f4LightingOne.z);
	color += pow(mainLayout.fGltfLighting * mainLayout.fGltfDayBrightness * directionalLighting, vec3(mainLayout.fGltfLightingPower));
#endif

	// Add emissive
#if ENABLE_EMISSIVE
	vec3 emissive = material.f4EmissiveFactor.rgb;
	if (material.iEmissiveTextureSet > -1)
	{
		emissive *= SRGBtoLinear(texture(emissiveMap, getUV(material.iEmissiveTextureSet)).rgb);
	}
	color += mainLayout.fGltfEmissive * emissive;
#endif

	// Apply smoke/fog (project position to base height plane)
#if ENABLE_SMOKE
	vec2 f2PositionAtBaseHeight = BaseHeightPosition(globalLayout, mainLayout, f3InWorldPosition);
	color = AddSmoke(globalLayout, color, f2PositionAtBaseHeight, smokeSampler, mainLayout.fGltfSmoke, pf4Lighting);
#endif

	// Output color
	f4OutColor = vec4(color, baseColor.a);

	// Per-instance color add (for effects like damage flash)
	f4OutColor += f4InColorAdd;
}
