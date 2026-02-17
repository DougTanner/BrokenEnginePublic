#version 460

#include "ShaderLayouts.h"
#include "ShaderFunctions.h"
#include "SmokeSpreadCommon.h"

// Uniforms
layout (set = 0, binding = 0) uniform globalUniform
{
	GlobalLayout globalLayout;
};

layout (set = 1, binding = 2) uniform sampler2D textureSampler;
layout (set = 1, binding = 3) uniform sampler2D noiseTextureSampler;
layout (set = 1, binding = 4) uniform sampler2D windTextureSamplerOne;
layout (set = 1, binding = 5) uniform sampler2D windTextureSamplerTwo;

// Input
layout (location = 0) in flat int iInInstanceIndex;
layout (location = 1) in vec4 f4InMisc;
layout (location = 2) in vec2 f2InTexcoord;
layout (location = 3) in flat uint uiInColor;

// Output
layout (location = 0) out vec4 f4OutColor;

void main()
{
	f4OutColor = SmokeSpread(globalLayout, textureSampler, noiseTextureSampler, windTextureSamplerOne, windTextureSamplerTwo, f2InTexcoord, globalLayout.fSmokeNoiseScaleOne, 0.0f);
}
