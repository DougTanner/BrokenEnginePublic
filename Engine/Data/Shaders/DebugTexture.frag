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
		// Float16 EWNS: Same tone mapping as LightCombine.comp
		float fExposure = globalLayout.fCombineExposure;
		float fLinearClamp = globalLayout.fCombineLinearClamp;
		float fPower = globalLayout.fCombinePower;
		vec4 f4Scaled = f4Sample * fExposure;
		f4Scaled = mix(f4Scaled / (vec4(1.0f) + f4Scaled), clamp(f4Scaled, vec4(0.0f), vec4(1.0f)), fLinearClamp);
		f4Scaled = pow(f4Scaled, vec4(fPower));
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
}
