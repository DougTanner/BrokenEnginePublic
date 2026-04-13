#version 460

#include "ShaderLayouts.h"

// Uniforms
layout (set = 0, binding = 0) uniform globalUniform
{
	GlobalLayout globalLayout;
};

// Texture array
layout (set = 1, binding = 1) uniform sampler2D debugTextures[kiMaxDebugTextures];

// Input
layout (location = 0) in flat int iInInstanceIndex;
layout (location = 1) in vec2 f2InTexcoord;

// Output
layout (location = 0) out vec4 f4OutColor;

void main()
{
	int iIndex = int(globalLayout.fDebugTextureIndex);
	int iFormat = int(globalLayout.fDebugTextureFormat);
	vec4 f4Sample = texture(debugTextures[iIndex], f2InTexcoord);

	if (iFormat == kiDebugTextureFormatFloat16LightingDirectional)
	{
		// Float16 EWNS: Same tone mapping as LightCombine.comp (curve 1 only for debug)
		float fPassCount = globalLayout.fSpreadPassCount;
		float fPassNorm = mix(1.0f, 1.0f / fPassCount, globalLayout.fCombinePassNormalize);
		float fPassScale = pow(fPassCount, -globalLayout.fCombineExposurePassScale);
		vec4 f4Scaled = f4Sample * fPassNorm * fPassScale;
#if 0 // Luminance-based Reinhard
		float fLum = max(dot(f4Scaled, vec4(1.0f)), 0.001f);
		f4Scaled *= (fLum / (1.0f + fLum)) / fLum;
#else
		f4Scaled = f4Scaled / (vec4(1.0f) + f4Scaled);
#endif
		f4Scaled = globalLayout.fCombineIntensityOne * pow(f4Scaled, vec4(globalLayout.fCombinePowerOne));
		float fTotal = f4Scaled.r + f4Scaled.g + f4Scaled.b + f4Scaled.a;
		float fIntensity = min(fTotal, 1.0f);
		float fEastWest = fTotal > 0.0f ? (f4Scaled.r - f4Scaled.g) / fTotal * 0.5f + 0.5f : 0.0f;
		float fNorthSouth = fTotal > 0.0f ? (f4Scaled.a - f4Scaled.b) / fTotal * 0.5f + 0.5f : 0.0f;
		f4OutColor = vec4(fEastWest * fIntensity, fNorthSouth * fIntensity, 0.0f, 1.0f);
	}
	else if (iFormat == kiDebugTextureFormatUnormLightingDirectional)
	{
		// UNORM EWNS: values already in [0,1], show full range
		float fTotal = f4Sample.r + f4Sample.g + f4Sample.b + f4Sample.a;
		float fIntensity = min(fTotal, 1.0f);
		float fEastWest = fTotal > 0.0f ? (f4Sample.r - f4Sample.g) / fTotal * 0.5f + 0.5f : 0.0f;
		float fNorthSouth = fTotal > 0.0f ? (f4Sample.a - f4Sample.b) / fTotal * 0.5f + 0.5f : 0.0f;
		f4OutColor = vec4(fEastWest * fIntensity, fNorthSouth * fIntensity, 0.0f, 1.0f);
	}
	else if (iFormat == kiDebugTextureFormatFloat16Linear)
	{
		float fTotal = f4Sample.r + f4Sample.g + f4Sample.b + f4Sample.a;
		float fValue = clamp(fTotal / globalLayout.fDebugTextureLinearRange, 0.0f, 1.0f);
		f4OutColor = vec4(fValue, fValue, fValue, 1.0f);
	}
	else if (iFormat == kiDebugTextureFormatFloat16LinearVisibleArea)
	{
		// Remap screen texcoord to lighting area texcoord, camera-tracked via lighting area center
		float fVisibleWidth = globalLayout.f4VisibleArea.z - globalLayout.f4VisibleArea.x;
		float fVisibleHeight = globalLayout.f4VisibleArea.y - globalLayout.f4VisibleArea.w;
		float fLightingCenterX = (globalLayout.f4LightingArea.x + globalLayout.f4LightingArea.z) * 0.5f;
		float fLightingCenterY = (globalLayout.f4LightingArea.y + globalLayout.f4LightingArea.w) * 0.5f;
		vec2 f2WorldPos = vec2(
			fLightingCenterX - fVisibleWidth * 0.5f + f2InTexcoord.x * fVisibleWidth,
			fLightingCenterY + fVisibleHeight * 0.5f - f2InTexcoord.y * fVisibleHeight);
		float fLightingMultX = 1.0f / (globalLayout.f4LightingArea.z - globalLayout.f4LightingArea.x);
		float fLightingMultY = 1.0f / (globalLayout.f4LightingArea.y - globalLayout.f4LightingArea.w);
		vec2 f2LightingTexcoord = vec2(
			fLightingMultX * (f2WorldPos.x - globalLayout.f4LightingArea.x),
			1.0f - fLightingMultY * (f2WorldPos.y - globalLayout.f4LightingArea.w));

		vec4 f4Remapped = texture(debugTextures[iIndex], f2LightingTexcoord);
		float fTotal = f4Remapped.r + f4Remapped.g + f4Remapped.b + f4Remapped.a;
		float fValue = clamp(fTotal / globalLayout.fDebugTextureLinearRange, 0.0f, 1.0f);
		f4OutColor = vec4(fValue, fValue, fValue, 1.0f);
	}
}
