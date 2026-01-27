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

// Constants
const float M_PI = 3.141592653589793;
const float c_MinRoughness = 0.04;
const float PBR_WORKFLOW_METALLIC_ROUGHNESS = 0.0;
const float PBR_WORKFLOW_SPECULAR_GLOSSINESS = 1.0;

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

layout (binding = 2) buffer readonly gltfsUniform
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
layout (binding = 11) buffer readonly gltfMaterialsUniform
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
	if (abs(det) < 1e-6)
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
vec3 GetIBLContribution(PBRInfo pbrInputs, vec3 n, vec3 reflection)
{
	float lod = pbrInputs.perceptualRoughness * mainLayout.fGltfMipCount;

	// Sample BRDF LUT (note: Y coordinate is 1.0 - roughness)
	vec3 brdf = texture(samplerBRDFLUT, vec2(pbrInputs.NdotV, 1.0 - pbrInputs.perceptualRoughness)).rgb;

	// Sample and process diffuse irradiance
	vec3 diffuseLight = SRGBtoLinear(TonemapIBL(texture(samplerIrradiance, ToCubemapCoord(n)))).rgb;

	// Sample and process specular from prefiltered environment map
	vec3 specularLight = SRGBtoLinear(TonemapIBL(textureLod(prefilteredMap, ToCubemapCoord(reflection), lod))).rgb;

	vec3 diffuse = diffuseLight * pbrInputs.diffuseColor;
	vec3 specular = specularLight * (pbrInputs.specularColor * brdf.x + brdf.y);

	// Apply ambient multiplier
	diffuse *= mainLayout.fGltfAmbient;
	specular *= mainLayout.fGltfAmbient;

	return diffuse + specular;
}

// Convert specular-glossiness to metallic-roughness workflow
float ConvertMetallic(vec3 diffuse, vec3 specular, float maxSpecular)
{
	float perceivedDiffuse = sqrt(0.299 * diffuse.r * diffuse.r + 0.587 * diffuse.g * diffuse.g + 0.114 * diffuse.b * diffuse.b);
	float perceivedSpecular = sqrt(0.299 * specular.r * specular.r + 0.587 * specular.g * specular.g + 0.114 * specular.b * specular.b);
	if (perceivedSpecular < c_MinRoughness)
	{
		return 0.0;
	}
	float a = c_MinRoughness;
	float b = perceivedDiffuse * (1.0 - maxSpecular) / (1.0 - c_MinRoughness) + perceivedSpecular - 2.0 * c_MinRoughness;
	float c = c_MinRoughness - perceivedSpecular;
	float D = max(b * b - 4.0 * a * c, 0.0);
	return clamp((-b + sqrt(D)) / (2.0 * a), 0.0, 1.0);
}

void main()
{
	// Get material data
	GltfMaterialLayout material = pMaterials[int32_t(pushConstantsLayout.f4Pipeline.w)];

	// Sample base color
	vec4 baseColor = material.f4BaseColorFactor;
	if (material.iColorTextureSet > -1)
	{
		baseColor *= SRGBtoLinear(texture(colorMap, getUV(material.iColorTextureSet)));
	}

	// Alpha masking
	if (material.fAlphaMask > 0.0 && baseColor.a < material.fAlphaMaskCutoff)
	{
		discard;
	}

	// Initialize material properties
	float metallic = material.fMetallicFactor;
	float perceptualRoughness = material.fRoughnessFactor;
	vec3 diffuseColor = vec3(0.0);
	vec3 specularColor = vec3(0.0);
	vec3 f0 = vec3(0.04);

	// Metallic-Roughness workflow
	if (material.fWorkflow == PBR_WORKFLOW_METALLIC_ROUGHNESS)
	{
		if (material.iPhysicalDescriptorTextureSet > -1)
		{
			vec4 mrSample = texture(physicalDescriptorMap, getUV(material.iPhysicalDescriptorTextureSet));
			perceptualRoughness *= mrSample.g;
			metallic *= mrSample.b;
		}
		perceptualRoughness = clamp(perceptualRoughness, c_MinRoughness, 1.0);
		metallic = clamp(metallic, 0.0, 1.0);

		// Energy-conserving diffuse (accounts for light reflected as specular)
		diffuseColor = baseColor.rgb * (vec3(1.0) - f0);
		diffuseColor *= 1.0 - metallic;

		// Specular color: F0 for dielectrics, baseColor for metals
		specularColor = mix(f0, baseColor.rgb, metallic);
	}
	// Specular-Glossiness workflow
	else if (material.fWorkflow == PBR_WORKFLOW_SPECULAR_GLOSSINESS)
	{
		vec4 diffuse = SRGBtoLinear(material.f4DiffuseFactor);
		vec3 specular = SRGBtoLinear(material.f4SpecularFactor.rgb);

		if (material.iColorTextureSet > -1)
		{
			diffuse *= SRGBtoLinear(texture(colorMap, getUV(material.iColorTextureSet)));
		}
		if (material.iPhysicalDescriptorTextureSet > -1)
		{
			vec4 sgSample = texture(physicalDescriptorMap, getUV(material.iPhysicalDescriptorTextureSet));
			specular *= SRGBtoLinear(sgSample.rgb);
			perceptualRoughness = 1.0 - sgSample.a * material.f4SpecularFactor.a;
		}
		else
		{
			perceptualRoughness = 1.0 - material.f4SpecularFactor.a;
		}

		float maxSpecular = max(max(specular.r, specular.g), specular.b);
		metallic = ConvertMetallic(diffuse.rgb, specular, maxSpecular);
		f0 = specular;
		diffuseColor = diffuse.rgb * (1.0 - maxSpecular);
		specularColor = specular;
		perceptualRoughness = clamp(perceptualRoughness, c_MinRoughness, 1.0);
	}

	float alphaRoughness = perceptualRoughness * perceptualRoughness;

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

	// Evaluate BRDF terms for direct lighting
	vec3 F = F_Schlick(VdotH, pbrInputs.reflectance0, pbrInputs.reflectance90);
	float D = D_GGX(NdotH, alphaRoughness);
	float V = V_SmithGGXCorrelated(NdotL, NdotV, alphaRoughness);

	// Diffuse and specular contributions
	vec3 diffuseContrib = (1.0 - F) * DiffuseLambert(diffuseColor);
	vec3 specularContrib = F * D * V;

	// Sample shadow
	vec2 f2VisibleAreaPosition = WorldToVisibleArea(f3InWorldPosition, globalLayout.f4VisibleArea);
	float fShadow = max(0.3, texture(shadowTextureSampler, f2VisibleAreaPosition).r);

	// Sample ambient occlusion
	float ao = 1.0;
	if (material.iOcclusionTextureSet > -1)
	{
		ao = texture(aoMap, getUV(material.iOcclusionTextureSet)).r;
	}

	// Combined light color for BRDF (ambient + sun)
	vec3 u_LightColor = globalLayout.f4AmbientColor.rgb + globalLayout.f4SunColor.rgb;

	// Accumulate lighting
	vec3 color = vec3(0.0);

	// Read directional lighting textures
	vec4 pf4Lighting[3];
	ReadLighting(pf4Lighting, pLightingSamplers, f2VisibleAreaPosition);

	// BRDF contribution (analytical lighting with combined light color)
	color = pow(mainLayout.fGltfBrdf * NdotL * u_LightColor * (diffuseContrib + specularContrib), vec3(mainLayout.fGltfBrdfPower));

	// Image-based lighting
	vec3 iblContribution = GetIBLContribution(pbrInputs, n, reflection);
	color += pow(mainLayout.fGltfIbl * iblContribution, vec3(mainLayout.fGltfIblPower));

	// Gamma scale (applied before sun/directional lighting)
	color *= mainLayout.fGltfGamma;

	// Sun lighting using engine SunLighting function (includes ambient contribution)
	color += pow(mainLayout.fGltfSun * SunLighting(baseColor.rgb, globalLayout, vec4(f3InWorldPosition, 1.0), f3InNormal, fShadow, 1.0), vec3(mainLayout.fGltfSunPower));

	// Additive sun diffuse (pure NdotL lighting - visible on dark objects)
	float fSunDot = max(0.0, dot(f3InNormal, globalLayout.f4SunNormal.xyz));
	vec3 sunDiffuseAdd = 0.25 * fShadow * fSunDot * globalLayout.f4SunColor.rgb * globalLayout.f4SunColor.rgb;
	color += mainLayout.fGltfSun * sunDiffuseAdd;

	// Engine directional lighting
	vec3 directionalLighting = Lighting(globalLayout, baseColor.rgb, f3InWorldPosition.z, f3InNormal, pf4Lighting, globalLayout.f4LightingTwo.w, globalLayout.f4LightingOne.z);
	color += pow(mainLayout.fGltfLighting * directionalLighting, vec3(mainLayout.fGltfLightingPower));

	// Apply ambient occlusion
	if (material.iOcclusionTextureSet > -1)
	{
		color *= ao;
	}

	// Secondary specular using reflection direction as virtual light
	vec3 l2 = normalize(reflection);
	vec3 h2 = normalize(l2 + v);
	float NdotL2 = clamp(dot(n, l2), 0.001, 1.0);
	float NdotH2 = clamp(dot(n, h2), 0.0, 0.99);
	float VdotH2 = clamp(dot(v, h2), 0.0, 1.0);
	float LdotH2 = clamp(dot(l2, h2), 0.0, 1.0);

	vec3 F2 = F_Schlick(VdotH2, pbrInputs.reflectance0, pbrInputs.reflectance90);
	float D2 = D_GGX(NdotH2, alphaRoughness);
	float V2 = V_SmithGGXCorrelated(NdotL2, NdotV, alphaRoughness);
	vec3 diffuseContrib2 = (1.0 - F2) * DiffuseLambert(diffuseColor);
	vec3 specContrib2 = F2 * D2 * V2;
	color += mainLayout.fGltfSpecular * NdotL2 * (diffuseContrib2 + specContrib2) * Lighting(globalLayout, specularColor, f3InWorldPosition.z, reflection, pf4Lighting, globalLayout.f4LightingTwo.w, globalLayout.f4LightingOne.z);

	// Add emissive
	vec3 emissive = material.f4EmissiveFactor.rgb;
	if (material.iEmissiveTextureSet > -1)
	{
		emissive *= SRGBtoLinear(texture(emissiveMap, getUV(material.iEmissiveTextureSet)).rgb);
	}
	color += emissive;

	// Apply smoke/fog (project position to base height plane)
	vec2 f2PositionAtBaseHeight = BaseHeightPosition(globalLayout, mainLayout, f3InWorldPosition);
	color = AddSmoke(globalLayout, color, f2PositionAtBaseHeight, smokeSampler, mainLayout.fGltfSmoke, pf4Lighting);

	// Debug visualization modes (output directly without tone mapping)
	if (mainLayout.fGltfDebugViewInputs > 0.0)
	{
		int mode = int(mainLayout.fGltfDebugViewInputs);
		if (mode == 1)
		{
			f4OutColor = vec4(baseColor.rgb, baseColor.a);
		}
		else if (mode == 2)
		{
			f4OutColor = vec4((material.iNormalTextureSet > -1) ? texture(normalMap, f2InUV).rgb : n * 0.5 + 0.5, 1.0);
		}
		else if (mode == 3)
		{
			f4OutColor = vec4(vec3(ao), 1.0);
		}
		else if (mode == 4)
		{
			f4OutColor = vec4(emissive, 1.0);
		}
		else if (mode == 5)
		{
			f4OutColor = vec4(vec3(metallic), 1.0);
		}
		else if (mode == 6)
		{
			f4OutColor = vec4(vec3(perceptualRoughness), 1.0);
		}
		return;
	}

	if (mainLayout.fGltfDebugViewEquation > 0.0)
	{
		int mode = int(mainLayout.fGltfDebugViewEquation);
		if (mode == 1)
		{
			f4OutColor = vec4(diffuseContrib, 1.0);
		}
		else if (mode == 2)
		{
			f4OutColor = vec4(F, 1.0);
		}
		else if (mode == 3)
		{
			f4OutColor = vec4(vec3(V), 1.0);
		}
		else if (mode == 4)
		{
			f4OutColor = vec4(vec3(D), 1.0);
		}
		else if (mode == 5)
		{
			f4OutColor = vec4(specularContrib, 1.0);
		}
		return;
	}

	// Output color
	f4OutColor = vec4(color, baseColor.a);

	// Per-instance color add (for effects like damage flash)
	f4OutColor += f4InColorAdd;
}
