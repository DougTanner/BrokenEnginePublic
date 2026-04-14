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
		// Float16 EWNS: Same tone mapping as LightCombine.comp (Uchimura)
		float fPassCount = globalLayout.fSpreadPassCount;
		float fPassNorm = mix(1.0f, 1.0f / fPassCount, globalLayout.fCombinePassNormalize);
		float fPassScale = pow(fPassCount, -globalLayout.fCombineExposurePassScale);
		vec4 f4Scaled = f4Sample * fPassNorm * fPassScale;

		float P = globalLayout.fCombineMaxBrightness;
		float a = globalLayout.fCombineContrast;
		float m = globalLayout.fCombineLinearStart;
		float l = globalLayout.fCombineLinearLength;
		float c = globalLayout.fCombineToe;
		float b = globalLayout.fCombineBlackTightness;
		float l0 = ((P - m) * l) / a;
		float S0 = m + l0;
		float S1 = m + a * l0;
		float C2 = (a * P) / (P - S1);
		float CP = -C2 / P;
		vec4 f4W0 = vec4(1.0f) - smoothstep(vec4(0.0f), vec4(m), f4Scaled);
		vec4 f4W2 = step(vec4(S0), f4Scaled);
		vec4 f4W1 = vec4(1.0f) - f4W0 - f4W2;
		f4Scaled = (m * pow(f4Scaled / m, vec4(c)) + b) * f4W0 + (m + a * (f4Scaled - m)) * f4W1 + (P - (P - S1) * exp(CP * (f4Scaled - S0))) * f4W2;
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
		// Camera-centered rectangle sized to visible area, mapped to lighting area UV
		float fVisibleWidth = globalLayout.f4VisibleArea.z - globalLayout.f4VisibleArea.x;
		float fVisibleHeight = globalLayout.f4VisibleArea.y - globalLayout.f4VisibleArea.w;
		vec2 f2WorldPos = vec2(
			globalLayout.f2CameraPosition.x - fVisibleWidth * 0.5f + f2InTexcoord.x * fVisibleWidth,
			globalLayout.f2CameraPosition.y + fVisibleHeight * 0.5f - f2InTexcoord.y * fVisibleHeight);
		vec2 f2LightingTexcoord = vec2(
			(f2WorldPos.x - globalLayout.f4LightingArea.x) / (globalLayout.f4LightingArea.z - globalLayout.f4LightingArea.x),
			1.0f - (f2WorldPos.y - globalLayout.f4LightingArea.w) / (globalLayout.f4LightingArea.y - globalLayout.f4LightingArea.w));

		vec4 f4Remapped = texture(debugTextures[iIndex], f2LightingTexcoord);

		// Same Uchimura tone mapping + EWNS directional as Float16LightingDirectional
		float fPassCount = globalLayout.fSpreadPassCount;
		float fPassNorm = mix(1.0f, 1.0f / fPassCount, globalLayout.fCombinePassNormalize);
		float fPassScale = pow(fPassCount, -globalLayout.fCombineExposurePassScale);
		vec4 f4Scaled = f4Remapped * fPassNorm * fPassScale;

		float P = globalLayout.fCombineMaxBrightness;
		float a = globalLayout.fCombineContrast;
		float m = globalLayout.fCombineLinearStart;
		float l = globalLayout.fCombineLinearLength;
		float c = globalLayout.fCombineToe;
		float b = globalLayout.fCombineBlackTightness;
		float l0 = ((P - m) * l) / a;
		float S0 = m + l0;
		float S1 = m + a * l0;
		float C2 = (a * P) / (P - S1);
		float CP = -C2 / P;
		vec4 f4W0 = vec4(1.0f) - smoothstep(vec4(0.0f), vec4(m), f4Scaled);
		vec4 f4W2 = step(vec4(S0), f4Scaled);
		vec4 f4W1 = vec4(1.0f) - f4W0 - f4W2;
		f4Scaled = (m * pow(f4Scaled / m, vec4(c)) + b) * f4W0 + (m + a * (f4Scaled - m)) * f4W1 + (P - (P - S1) * exp(CP * (f4Scaled - S0))) * f4W2;
		float fTotal = f4Scaled.r + f4Scaled.g + f4Scaled.b + f4Scaled.a;
		float fIntensity = min(fTotal, 1.0f);
		float fEastWest = fTotal > 0.0f ? (f4Scaled.r - f4Scaled.g) / fTotal * 0.5f + 0.5f : 0.0f;
		float fNorthSouth = fTotal > 0.0f ? (f4Scaled.a - f4Scaled.b) / fTotal * 0.5f + 0.5f : 0.0f;
		f4OutColor = vec4(fEastWest * fIntensity, fNorthSouth * fIntensity, 0.0f, 1.0f);
	}
}
