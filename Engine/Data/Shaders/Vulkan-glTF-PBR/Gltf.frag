// Based on https://github.com/SaschaWillems/Vulkan-glTF-PBR

/* Copyright (c) 2018-2024, Sascha Willems
 *
 * SPDX-License-Identifier: MIT
 *
 */

// PBR shader based on the Khronos WebGL PBR implementation
// See https://github.com/KhronosGroup/glTF-WebGL-PBR
// Supports both metallic roughness and specular glossiness inputs

#version 460

#include "ShaderLayouts.h"
#include "ShaderFunctions.h"

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


layout (binding = 3) uniform sampler2D colorMap;
layout (binding = 4) uniform sampler2D physicalDescriptorMap;
layout (binding = 5) uniform sampler2D normalMap;
layout (binding = 6) uniform sampler2D aoMap;
layout (binding = 7) uniform sampler2D emissiveMap;

layout (binding = 8) uniform samplerCube samplerIrradiance;
layout (binding = 9) uniform samplerCube prefilteredMap;
layout (binding = 10) uniform sampler2D samplerBRDFLUT;

layout (binding = 11) buffer readonly gltfMaterialsUniform
{
	GltfMaterialLayout pMaterials[];
};

layout (binding = 12) uniform sampler2D pLightingSamplers[3];
layout (binding = 13) uniform sampler2D shadowTextureSampler;
layout (binding = 14) uniform sampler2D smokeSampler;

// Input
layout (location = 0) in vec3 f3InWorldPosition;
layout (location = 1) in vec3 f3InNormal;
layout (location = 2) in vec2 f2InUV;
layout (location = 3) in vec4 f4InColorAdd;

// Output
layout (location = 0) out vec4 f4OutColor;

// Encapsulate the various inputs used by the various functions in the shading equation
// We store values in this struct to simplify the integration of alternative implementations
// of the shading terms, outlined in the Readme.MD Appendix.
struct PBRInfo
{
	float NdotL;                  // cos angle between normal and light direction
	float NdotV;                  // cos angle between normal and view direction
	float NdotH;                  // cos angle between normal and half vector
	float LdotH;                  // cos angle between light direction and half vector
	float VdotH;                  // cos angle between view direction and half vector
	float perceptualRoughness;    // roughness value, as authored by the model creator (input to shader)
	float metalness;              // metallic value at the surface
	vec3 reflectance0;            // full reflectance color (normal incidence angle)
	vec3 reflectance90;           // reflectance color at grazing angle
	float alphaRoughness;         // roughness mapped to a more linear change in the roughness (proposed by [2])
	vec3 diffuseColor;            // color contribution from diffuse lighting
	vec3 specularColor;           // color contribution from specular lighting
};

const float M_PI = 3.141592653589793;
const float c_MinRoughness = 0.04;

const float PBR_WORKFLOW_METALLIC_ROUGHNESS = 0.0;
const float PBR_WORKFLOW_SPECULAR_GLOSINESS = 1.0f;

#if 0 // DT:
#define MANUAL_SRGB 1
#endif

vec3 Uncharted2Tonemap(vec3 color)
{
	float A = 0.15;
	float B = 0.50;
	float C = 0.10;
	float D = 0.20;
	float E = 0.02;
	float F = 0.30;
	float W = 11.2;
	return ((color*(A*color+C*B)+D*E)/(color*(A*color+B)+D*F))-E/F;
}

vec4 tonemap(vec4 color)
{
	vec3 outcol = Uncharted2Tonemap(color.rgb * mainLayout.fGltfExposuse);
	outcol = outcol * (1.0f / Uncharted2Tonemap(vec3(11.2f)));	
	return vec4(pow(outcol, vec3(1.0f / mainLayout.fGltfGamma)), color.a);
}

vec4 SRGBtoLINEAR(vec4 srgbIn)
{
	#ifdef MANUAL_SRGB
	#ifdef SRGB_FAST_APPROXIMATION
	vec3 linOut = pow(srgbIn.xyz,vec3(2.2));
	#else //SRGB_FAST_APPROXIMATION
	vec3 bLess = step(vec3(0.04045),srgbIn.xyz);
	vec3 linOut = mix( srgbIn.xyz/vec3(12.92), pow((srgbIn.xyz+vec3(0.055))/vec3(1.055),vec3(2.4)), bLess );
	#endif //SRGB_FAST_APPROXIMATION
	return vec4(linOut,srgbIn.w);
	#else //MANUAL_SRGB
	return srgbIn;
	#endif //MANUAL_SRGB
}

// Find the normal for this fragment, pulling either from a predefined normal map
// or from the interpolated mesh normal and tangent attributes.
vec3 getNormal()
{
	// Perturb normal, see http://www.thetenthplanet.de/archives/1180
#if 0 // DT:
	vec3 tangentNormal = texture(normalMap, material.iNormalTextureSet == 0 ? f2InUV0 : f2InUV1).xyz * 2.0 - 1.0;
#else
	vec3 tangentNormal = texture(normalMap, f2InUV).xyz * 2.0 - 1.0;
#endif

	vec3 q1 = dFdx(f3InWorldPosition);
	vec3 q2 = dFdy(f3InWorldPosition);
	vec2 st1 = dFdx(f2InUV);
	vec2 st2 = dFdy(f2InUV);

	vec3 N = f3InNormal;
	vec3 T = normalize(q1 * st2.t - q2 * st1.t);
	vec3 B = -normalize(cross(N, T));
	mat3 TBN = mat3(T, B, N);

	return normalize(TBN * tangentNormal);
}

// DT:
vec3 CubeToWorld(vec3 f3Normal)
{
	return vec3(f3Normal.x, f3Normal.z, f3Normal.y);
}

// Calculation of the lighting contribution from an optional Image Based Light source.
// Precomputed Environment Maps are required uniform inputs and are computed as outlined in [1].
// See our README.md on Environment Maps [3] for additional discussion.
vec3 getIBLContribution(PBRInfo pbrInputs, vec3 n, vec3 reflection)
{
	float lod = (pbrInputs.perceptualRoughness * mainLayout.fGltfMipCount);
	// retrieve a scale and bias to F0. See [1], Figure 3
	vec3 brdf = (texture(samplerBRDFLUT, vec2(pbrInputs.NdotV, 1.0 - pbrInputs.perceptualRoughness))).rgb;
	vec3 diffuseLight = SRGBtoLINEAR(tonemap(texture(samplerIrradiance, CubeToWorld(n)))).rgb;

	vec3 specularLight = SRGBtoLINEAR(tonemap(textureLod(prefilteredMap, CubeToWorld(reflection), lod))).rgb;

	vec3 diffuse = diffuseLight * pbrInputs.diffuseColor;
	vec3 specular = specularLight * (pbrInputs.specularColor * brdf.x + brdf.y);

	// For presentation, this allows us to disable IBL terms
	// For presentation, this allows us to disable IBL terms
	diffuse *= mainLayout.fGltfAmbient;
	specular *= mainLayout.fGltfAmbient;

	return diffuse + specular;
}

// Basic Lambertian diffuse
// Implementation from Lambert's Photometria https://archive.org/details/lambertsphotome00lambgoog
// See also [1], Equation 1
vec3 diffuse(PBRInfo pbrInputs)
{
	return pbrInputs.diffuseColor / M_PI;
}

// The following equation models the Fresnel reflectance term of the spec equation (aka F())
// Implementation of fresnel from [4], Equation 15
vec3 specularReflection(PBRInfo pbrInputs)
{
	return pbrInputs.reflectance0 + (pbrInputs.reflectance90 - pbrInputs.reflectance0) * pow(clamp(1.0 - pbrInputs.VdotH, 0.0, 1.0), 5.0);
}

// This calculates the specular geometric attenuation (aka G()),
// where rougher material will reflect less light back to the viewer.
// This implementation is based on [1] Equation 4, and we adopt their modifications to
// alphaRoughness as input as originally proposed in [2].
float geometricOcclusion(PBRInfo pbrInputs)
{
	float NdotL = pbrInputs.NdotL;
	float NdotV = pbrInputs.NdotV;
	float r = pbrInputs.alphaRoughness;

	float attenuationL = 2.0 * NdotL / (NdotL + sqrt(r * r + (1.0 - r * r) * (NdotL * NdotL)));
	float attenuationV = 2.0 * NdotV / (NdotV + sqrt(r * r + (1.0 - r * r) * (NdotV * NdotV)));
	return attenuationL * attenuationV;
}

// The following equation(s) model the distribution of microfacet normals across the area being drawn (aka D())
// Implementation from "Average Irregularity Representation of a Roughened Surface for Ray Reflection" by T. S. Trowbridge, and K. P. Reitz
// Follows the distribution function recommended in the SIGGRAPH 2013 course notes from EPIC Games [1], Equation 3.
float microfacetDistribution(PBRInfo pbrInputs)
{
	float roughnessSq = pbrInputs.alphaRoughness * pbrInputs.alphaRoughness;
	float f = (pbrInputs.NdotH * roughnessSq - pbrInputs.NdotH) * pbrInputs.NdotH + 1.0;
	return roughnessSq / (M_PI * f * f);
}

// Gets metallic factor from specular glossiness workflow inputs 
float convertMetallic(vec3 diffuse, vec3 specular, float maxSpecular) {
	float perceivedDiffuse = sqrt(0.299 * diffuse.r * diffuse.r + 0.587 * diffuse.g * diffuse.g + 0.114 * diffuse.b * diffuse.b);
	float perceivedSpecular = sqrt(0.299 * specular.r * specular.r + 0.587 * specular.g * specular.g + 0.114 * specular.b * specular.b);
	if (perceivedSpecular < c_MinRoughness) {
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
	vec2 f2VisibleAreaTexcoord = WorldToVisibleArea(f3InWorldPosition, globalLayout.f4VisibleArea);
    vec4 pf4Lighting[3];
	ReadLighting(pf4Lighting, pLightingSamplers, f2VisibleAreaTexcoord);

	float perceptualRoughness;
	float metallic;
	vec3 diffuseColor;
	vec4 baseColor;

	vec3 f0 = vec3(0.04);

	GltfMaterialLayout material = pMaterials[int32_t(pushConstantsLayout.f4Pipeline.w)];
#if 0 // DT:
	if (material.alphaMask == 1.0f) {
		if (material.iColorTextureSet > -1) {
		#if 0 // DT:
			baseColor = SRGBtoLINEAR(texture(colorMap, material.iColorTextureSet == 0 ? f2InUV0 : f2InUV1)) * material.f4BaseColorFactor;
		#else
			baseColor = SRGBtoLINEAR(texture(colorMap, f2InUV)) * material.f4BaseColorFactor;
		#endif
		} else {
			baseColor = material.f4BaseColorFactor;
		}
		if (baseColor.a < material.alphaMaskCutoff) {
			discard;
		}
	}
#endif

	if (material.fWorkflow == PBR_WORKFLOW_METALLIC_ROUGHNESS) {
		// Metallic and Roughness material properties are packed together
		// In glTF, these factors can be specified by fixed scalar values
		// or from a metallic-roughness map
		perceptualRoughness = material.fRoughnessFactor;
		metallic = material.fMetallicFactor;
		if (material.iPhysicalDescriptorTextureSet > -1) {
			// Roughness is stored in the 'g' channel, metallic is stored in the 'b' channel.
			// This layout intentionally reserves the 'r' channel for (optional) occlusion map data
		#if 0 // DT:
			vec4 mrSample = texture(physicalDescriptorMap, material.iPhysicalDescriptorTextureSet == 0 ? f2InUV0 : f2InUV1);
		#else
			vec4 mrSample = texture(physicalDescriptorMap, f2InUV);
		#endif
			perceptualRoughness = mrSample.g * perceptualRoughness;
			metallic = mrSample.b * metallic;
		} else {
			perceptualRoughness = clamp(perceptualRoughness, c_MinRoughness, 1.0);
			metallic = clamp(metallic, 0.0, 1.0);
		}
		// Roughness is authored as perceptual roughness; as is convention,
		// convert to material roughness by squaring the perceptual roughness [2].

		// The albedo may be defined from a base texture or a flat color
		if (material.iColorTextureSet > -1) {
		#if 0 // DT:
			baseColor = SRGBtoLINEAR(texture(colorMap, material.iColorTextureSet == 0 ? f2InUV0 : f2InUV1)) * material.f4BaseColorFactor;
		#else
			baseColor = SRGBtoLINEAR(texture(colorMap, f2InUV)) * material.f4BaseColorFactor;
		#endif
		} else {
			baseColor = material.f4BaseColorFactor;
		}
	}

	if (material.fWorkflow == PBR_WORKFLOW_SPECULAR_GLOSINESS) {
		// Values from specular glossiness workflow are converted to metallic roughness
		if (material.iPhysicalDescriptorTextureSet > -1) {
		#if 0 // DT:
			perceptualRoughness = 1.0 - texture(physicalDescriptorMap, material.iPhysicalDescriptorTextureSet == 0 ? f2InUV0 : f2InUV1).a;
		#else
			perceptualRoughness = 1.0 - texture(physicalDescriptorMap, f2InUV).a;
		#endif
		} else {
			perceptualRoughness = 0.0;
		}

		const float epsilon = 1e-6;

		vec4 diffuse = SRGBtoLINEAR(texture(colorMap, f2InUV));
		vec3 specular = SRGBtoLINEAR(texture(physicalDescriptorMap, f2InUV)).rgb;

		float maxSpecular = max(max(specular.r, specular.g), specular.b);

		// Convert metallic value from specular glossiness inputs
		metallic = convertMetallic(diffuse.rgb, specular, maxSpecular);

		vec3 baseColorDiffusePart = diffuse.rgb * ((1.0 - maxSpecular) / (1 - c_MinRoughness) / max(1 - metallic, epsilon)) * material.f4DiffuseFactor.rgb;
		vec3 baseColorSpecularPart = specular - (vec3(c_MinRoughness) * (1 - metallic) * (1 / max(metallic, epsilon))) * material.f4SpecularFactor.rgb;
		baseColor = vec4(mix(baseColorDiffusePart, baseColorSpecularPart, metallic * metallic), diffuse.a);

	}

#if 0
	baseColor *= f3InColor0;
#endif

	diffuseColor = baseColor.rgb * (vec3(1.0) - f0);
	diffuseColor *= 1.0 - metallic;
		
	float alphaRoughness = perceptualRoughness * perceptualRoughness;

	vec3 specularColor = mix(f0, baseColor.rgb, metallic);

	// Compute reflectance.
	float reflectance = max(max(specularColor.r, specularColor.g), specularColor.b);

	// For typical incident reflectance range (between 4% to 100%) set the grazing reflectance to 100% for typical fresnel effect.
	// For very low reflectance range on highly diffuse objects (below 4%), incrementally reduce grazing reflecance to 0%.
	float reflectance90 = clamp(reflectance * 25.0, 0.0, 1.0);
	vec3 specularEnvironmentR0 = specularColor.rgb;
	vec3 specularEnvironmentR90 = vec3(1.0, 1.0, 1.0) * reflectance90;

	vec3 n = (material.iNormalTextureSet > -1) ? getNormal() : f3InNormal;
	vec3 v = normalize(mainLayout.f4EyePosition.xyz - f3InWorldPosition);    // Vector from surface point to camera
	vec3 l = normalize(globalLayout.f4SunNormal.xyz);     // Vector from surface point to light
	vec3 h = normalize(l+v);                        // Half vector between both l and v
	vec3 reflection = -normalize(reflect(v, n));
	reflection.y *= -1.0f;

	float NdotL = clamp(dot(n, l), 0.001, 1.0);
	float NdotV = clamp(abs(dot(n, v)), 0.001, 1.0);
	float NdotH = clamp(dot(n, h), 0.0, 1.0);
	float LdotH = clamp(dot(l, h), 0.0, 1.0);
	float VdotH = clamp(dot(v, h), 0.0, 1.0);

	PBRInfo pbrInputs = PBRInfo(
		NdotL,
		NdotV,
		NdotH,
		LdotH,
		VdotH,
		perceptualRoughness,
		metallic,
		specularEnvironmentR0,
		specularEnvironmentR90,
		alphaRoughness,
		diffuseColor,
		specularColor
	);

	// Calculate the shading terms for the microfacet specular shading model
	vec3 F = specularReflection(pbrInputs);
	float G = geometricOcclusion(pbrInputs);
	float D = microfacetDistribution(pbrInputs);

	// DT:
	vec3 u_LightColor = globalLayout.f4AmbientColor.xyz + globalLayout.f4SunColor.xyz;

	// Calculation of analytical lighting contribution
	vec3 diffuseContrib = (1.0 - F) * diffuse(pbrInputs);
	vec3 specContrib = F * G * D / (4.0 * NdotL * NdotV);
	// Obtain final intensity as reflectance (BRDF) scaled by the energy of the light (cosine law)
	vec3 color = pow(mainLayout.fGltfBrdf * NdotL * u_LightColor * (diffuseContrib + specContrib), vec3(mainLayout.fGltfBrdfPower, mainLayout.fGltfBrdfPower, mainLayout.fGltfBrdfPower));

	// Calculate lighting contribution from image based lighting source (IBL)
	color += pow(mainLayout.fGltfIbl * getIBLContribution(pbrInputs, n, reflection), vec3(mainLayout.fGltfIblPower, mainLayout.fGltfIblPower, mainLayout.fGltfIblPower));

	// DT: Sun & diffuse lighting
	color *= mainLayout.fGltfGamma;
	color += pow(mainLayout.fGltfSun * SunLighting(baseColor.rgb, globalLayout, vec4(f3InWorldPosition, 1.0f), f3InNormal, max(0.3f, texture(shadowTextureSampler, f2VisibleAreaTexcoord).x), 1.0f), vec3(mainLayout.fGltfSunPower, mainLayout.fGltfSunPower, mainLayout.fGltfSunPower));
	color += pow(mainLayout.fGltfLighting * Lighting(globalLayout, baseColor.rgb, f3InWorldPosition.z, f3InNormal, pf4Lighting, globalLayout.f4LightingTwo.w, globalLayout.f4LightingOne.z), vec3(mainLayout.fGltfLightingPower, mainLayout.fGltfLightingPower, mainLayout.fGltfLightingPower));

	const float u_OcclusionStrength = 1.0f;
	// Apply optional PBR terms for additional (optional) shading
	if (material.iOcclusionTextureSet > -1) {
	#if 0 // DT:
		float ao = texture(aoMap, (material.iOcclusionTextureSet == 0 ? f2InUV0 : f2InUV1)).r;
	#else
		float ao = texture(aoMap, f2InUV).r;
	#endif
		color = mix(color, color * ao, u_OcclusionStrength);
	}

	// DT: Specular lighting
	vec3 l2 = normalize(reflection);
	vec3 h2 = normalize(l2+v);
	float NdotL2 = clamp(dot(n, l2), 0.001, 1.0);
	float VdotH2 = clamp(dot(v, h2), 0.0, 1.0);
#if 0
	color += mainLayout.fGltfSpecular * NdotL2 * VdotH2 * metallic * specularEnvironmentR90 * Lighting(globalLayout, specularColor, f3InWorldPosition.z, reflection, pf4Lighting, globalLayout.f4LightingTwo.w, globalLayout.f4LightingOne.z);
#else
	float NdotH2 = clamp(dot(n, h2), 0.0, 0.99);
	float LdotH2 = clamp(dot(l2, h2), 0.0, 1.0);
	PBRInfo pbrInputs2 = PBRInfo(
		NdotL2,
		NdotV,
		NdotH2,
		LdotH2,
		VdotH2,
		perceptualRoughness,
		metallic,
		specularEnvironmentR0,
		specularEnvironmentR90,
		alphaRoughness,
		diffuseColor,
		specularColor
	);
	vec3 F2 = specularReflection(pbrInputs2);
	float G2 = geometricOcclusion(pbrInputs2);
	float D2 = microfacetDistribution(pbrInputs2);
	vec3 diffuseContrib2 = (1.0 - F2) * diffuse(pbrInputs2);
	vec3 specContrib2 = F2 * G2 * D2 / (4.0 * NdotL2 * NdotV);
	color += mainLayout.fGltfSpecular * NdotL2 * (diffuseContrib2 + specContrib2) * Lighting(globalLayout, specularColor, f3InWorldPosition.z, reflection, pf4Lighting, globalLayout.f4LightingTwo.w, globalLayout.f4LightingOne.z);
#endif

	const float u_EmissiveFactor = 1.0f;
	if (material.iEmissiveTextureSet > -1) {
	#if 0 // DT:
		vec3 emissive = SRGBtoLINEAR(texture(emissiveMap, material.iEmissiveTextureSet == 0 ? f2InUV0 : f2InUV1)).rgb * u_EmissiveFactor;
	#else
		vec3 emissive = SRGBtoLINEAR(texture(emissiveMap, f2InUV)).rgb * u_EmissiveFactor;
	#endif
		color += emissive;
	}

	// DT: Smoke
    vec2 f2PositionAtBaseHeight = BaseHeightPosition(globalLayout, mainLayout, f3InWorldPosition);
    color = AddSmoke(globalLayout, color, f2PositionAtBaseHeight, smokeSampler, 1.0f, pf4Lighting);
	
	f4OutColor = vec4(color, baseColor.a);

	// Shader inputs debug visualization
	if (mainLayout.fGltfDebugViewInputs > 0.0) {
		int index = int(mainLayout.fGltfDebugViewInputs);
		switch (index) {
		#if 0 // DT:
			case 1:
				f4OutColor.rgba = material.iColorTextureSet > -1 ? texture(colorMap, material.iColorTextureSet == 0 ? f2InUV0 : f2InUV1) : vec4(1.0f);
				break;
			case 2:
				f4OutColor.rgb = (material.iNormalTextureSet > -1) ? texture(normalMap, material.iNormalTextureSet == 0 ? f2InUV0 : f2InUV1).rgb : normalize(f3InNormal);
				break;
			case 3:
				f4OutColor.rgb = (material.iOcclusionTextureSet > -1) ? texture(aoMap, material.iOcclusionTextureSet == 0 ? f2InUV0 : f2InUV1).rrr : vec3(0.0f);
				break;
			case 4:
				f4OutColor.rgb = (material.iEmissiveTextureSet > -1) ? texture(emissiveMap, material.iEmissiveTextureSet == 0 ? f2InUV0 : f2InUV1).rgb : vec3(0.0f);
				break;
		#else
			case 1:
				f4OutColor.rgba = material.iColorTextureSet > -1 ? texture(colorMap, f2InUV) : vec4(1.0f);
				break;
			case 2:
				f4OutColor.rgb = (material.iNormalTextureSet > -1) ? texture(normalMap, f2InUV).rgb : normalize(f3InNormal);
				break;
			case 3:
				f4OutColor.rgb = (material.iOcclusionTextureSet > -1) ? texture(aoMap, f2InUV).rrr : vec3(0.0f);
				break;
			case 4:
				f4OutColor.rgb = (material.iEmissiveTextureSet > -1) ? texture(emissiveMap, f2InUV).rgb : vec3(0.0f);
				break;
		#endif
			case 5:
				f4OutColor.rgb = texture(physicalDescriptorMap, f2InUV).bbb;
				break;
			case 6:
				f4OutColor.rgb = texture(physicalDescriptorMap, f2InUV).ggg;
				break;
		}
		f4OutColor = SRGBtoLINEAR(f4OutColor);
	}

	// PBR equation debug visualization
	// "none", "Diff (l,n)", "F (l,h)", "G (l,v,h)", "D (h)", "Specular"
	if (mainLayout.fGltfDebugViewEquation > 0.0) {
		int index = int(mainLayout.fGltfDebugViewEquation);
		switch (index) {
			case 1:
				f4OutColor.rgb = diffuseContrib;
				break;
			case 2:
				f4OutColor.rgb = F;
				break;
			case 3:
				f4OutColor.rgb = vec3(G);
				break;
			case 4: 
				f4OutColor.rgb = vec3(D);
				break;
			case 5:
				f4OutColor.rgb = specContrib;
				break;				
		}
	}

	f4OutColor += f4InColorAdd;
}
