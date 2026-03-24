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
	vec4 f4Sample = texture(debugTextures[iIndex], f2InTexcoord);

	// Lighting EWNS visualization: R = west(0) to east(1), G = south(0) to north(1)
	float fTotal = f4Sample.r + f4Sample.g + f4Sample.b + f4Sample.a;
	float fIntensity = min(fTotal / 100.0f, 1.0f);
	float fEastWest = fTotal > 0.0f ? (f4Sample.r - f4Sample.g) / fTotal * 0.5f + 0.5f : 0.0f;
	float fNorthSouth = fTotal > 0.0f ? (f4Sample.a - f4Sample.b) / fTotal * 0.5f + 0.5f : 0.0f;
	f4OutColor = vec4(fEastWest * fIntensity, fNorthSouth * fIntensity, 0.0f, 1.0f);
}
