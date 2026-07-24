#version 460

#include "ShaderLayouts.h"

// Uniforms
layout (set = 0, binding = kiGlobalBindingGlobalUniform) uniform globalUniform
{
	GlobalLayout globalLayout;
};

// Texture array
layout (set = 1, binding = 1) uniform sampler2D debugTextures[kiMaxDebugTextures];
layout (set = 1, binding = 2) uniform sampler2D pLightingDepositSamplers[3];
layout (set = 1, binding = 3) uniform sampler2D pDebugTexturesB[kiMaxDebugTextures];
layout (set = 1, binding = 4) uniform sampler2D pDebugTexturesC[kiMaxDebugTextures];

// Input
layout (location = 0) in flat int iInInstanceIndex;
layout (location = 1) in vec2 f2InTexcoord;

// Output
layout (location = 0) out vec4 f4OutColor;

void main()
{
	f4OutColor = vec4(0.0f, 0.0f, 0.0f, 1.0f);
	int iIndex = int(globalLayout.fDebugTextureIndex);
	int iFormat = int(globalLayout.fDebugTextureFormat);
	vec4 f4Sample = texture(debugTextures[iIndex], f2InTexcoord);

	if (iFormat == kiDebugTextureFormatFloat16LightingDirectional)
	{
		// Float16 EWNS: Same tone mapping as LightCombine.comp (Uchimura). Pass scaling and the Uchimura
		// segment constants (S0/S1/CP) are precomputed on the CPU (globalLayout.fCombine*).
		vec4 f4Scaled = f4Sample * globalLayout.fCombinePassNormScale;

		float P = globalLayout.fCombineMaxBrightness;
		float a = globalLayout.fCombineContrast;
		float m = globalLayout.fCombineLinearStart;
		float c = globalLayout.fCombineToe;
		float b = globalLayout.fCombineBlackTightness;
		float S0 = globalLayout.fCombineS0;
		float S1 = globalLayout.fCombineS1;
		float CP = globalLayout.fCombineCP;
		vec4 f4W0 = vec4(1.0f) - smoothstep(vec4(0.0f), vec4(m), f4Scaled);
		vec4 f4W2 = step(vec4(S0), f4Scaled);
		vec4 f4W1 = vec4(1.0f) - f4W0 - f4W2;
		f4Scaled = (m * pow(max(f4Scaled, 0.0f) / m, vec4(c)) + b) * f4W0 + (m + a * (f4Scaled - m)) * f4W1 + (P - (P - S1) * exp(CP * (f4Scaled - S0))) * f4W2;
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

		vec4 f4R = texture(pLightingDepositSamplers[0], f2LightingTexcoord);
		vec4 f4G = texture(pLightingDepositSamplers[1], f2LightingTexcoord);
		vec4 f4B = texture(pLightingDepositSamplers[2], f2LightingTexcoord);
		float fInvRange = 1.0f / globalLayout.fDebugTextureLinearRange;
		float fR = clamp((f4R.r + f4R.g + f4R.b + f4R.a) * fInvRange, 0.0f, 1.0f);
		float fG = clamp((f4G.r + f4G.g + f4G.b + f4G.a) * fInvRange, 0.0f, 1.0f);
		float fB = clamp((f4B.r + f4B.g + f4B.b + f4B.a) * fInvRange, 0.0f, 1.0f);
		f4OutColor = vec4(fR, fG, fB, 1.0f);
	}
	else if (iFormat == kiDebugTextureFormatFloat16DepositDirectionCombined)
	{
		// Combined direction across all 3 deposit channels, weighted by per-channel intensity
		float fVisibleWidth = globalLayout.f4VisibleArea.z - globalLayout.f4VisibleArea.x;
		float fVisibleHeight = globalLayout.f4VisibleArea.y - globalLayout.f4VisibleArea.w;
		vec2 f2WorldPos = vec2(
			globalLayout.f2CameraPosition.x - fVisibleWidth * 0.5f + f2InTexcoord.x * fVisibleWidth,
			globalLayout.f2CameraPosition.y + fVisibleHeight * 0.5f - f2InTexcoord.y * fVisibleHeight);
		vec2 f2LightingTexcoord = vec2(
			(f2WorldPos.x - globalLayout.f4LightingArea.x) / (globalLayout.f4LightingArea.z - globalLayout.f4LightingArea.x),
			1.0f - (f2WorldPos.y - globalLayout.f4LightingArea.w) / (globalLayout.f4LightingArea.y - globalLayout.f4LightingArea.w));

		vec4 f4R = texture(pLightingDepositSamplers[0], f2LightingTexcoord);
		vec4 f4G = texture(pLightingDepositSamplers[1], f2LightingTexcoord);
		vec4 f4B = texture(pLightingDepositSamplers[2], f2LightingTexcoord);
		// EWNS: r=East, g=West, a=South, b=North. Summing signed components across channels
		// naturally weights direction by each channel's magnitude (intensity)
		float fEastWest = (f4R.r - f4R.g) + (f4G.r - f4G.g) + (f4B.r - f4B.g);
		float fNorthSouth = (f4R.a - f4R.b) + (f4G.a - f4G.b) + (f4B.a - f4B.b);
		float fTotal = (f4R.r + f4R.g + f4R.b + f4R.a) + (f4G.r + f4G.g + f4G.b + f4G.a) + (f4B.r + f4B.g + f4B.b + f4B.a);
		float fIntensity = clamp(fTotal / globalLayout.fDebugTextureLinearRange, 0.0f, 1.0f);
		float fEW = fTotal > 0.0f ? fEastWest / fTotal * 0.5f + 0.5f : 0.5f;
		float fNS = fTotal > 0.0f ? fNorthSouth / fTotal * 0.5f + 0.5f : 0.5f;
		f4OutColor = vec4(fEW * fIntensity, fNS * fIntensity, 0.0f, 1.0f);
	}
	else if (iFormat == kiDebugTextureFormatFloat16SpreadDirectionCombined)
	{
		// Combined direction across 3 spread textures for this pass, no tone mapping
		vec4 f4R = f4Sample;
		vec4 f4G = texture(pDebugTexturesB[iIndex], f2InTexcoord);
		vec4 f4B = texture(pDebugTexturesC[iIndex], f2InTexcoord);
		float fEastWest = (f4R.r - f4R.g) + (f4G.r - f4G.g) + (f4B.r - f4B.g);
		float fNorthSouth = (f4R.a - f4R.b) + (f4G.a - f4G.b) + (f4B.a - f4B.b);
		float fTotal = (f4R.r + f4R.g + f4R.b + f4R.a) + (f4G.r + f4G.g + f4G.b + f4G.a) + (f4B.r + f4B.g + f4B.b + f4B.a);
		float fIntensity = clamp(fTotal / globalLayout.fDebugTextureLinearRange, 0.0f, 1.0f);
		float fEW = fTotal > 0.0f ? fEastWest / fTotal * 0.5f + 0.5f : 0.5f;
		float fNS = fTotal > 0.0f ? fNorthSouth / fTotal * 0.5f + 0.5f : 0.5f;
		f4OutColor = vec4(fEW * fIntensity, fNS * fIntensity, 0.0f, 1.0f);
	}
	else if (iFormat == kiDebugTextureFormatRgb)
	{
		f4OutColor = vec4(f4Sample.rgb, 1.0f);
	}
	else if (iFormat == kiDebugTextureFormatGrayscaleR)
	{
		f4OutColor = vec4(f4Sample.rrr, 1.0f);
	}
	else if (iFormat == kiDebugTextureFormatTerrainElevation)
	{
		float fLow = globalLayout.fSeaFloorElevation;
		float fHigh = globalLayout.fDebugTerrainElevationHigh;
		float fRange = max(fHigh - fLow, kfEpsilon);
		float fValue = clamp((f4Sample.r - fLow) / fRange, 0.0f, 1.0f);
		f4OutColor = vec4(fValue, fValue, fValue, 1.0f);
	}
}
