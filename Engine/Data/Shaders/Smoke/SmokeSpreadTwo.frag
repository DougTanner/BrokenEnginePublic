#version 460

#include "ShaderLayouts.h"
#include "ShaderFunctions.h"
#include "SmokeSpreadCommon.h"

// Uniforms
layout (set = 0, binding = 0) uniform globalUniform
{
	GlobalLayout globalLayout;
};

layout (set = 1, binding = 1) uniform sampler2D textureSampler;
layout (set = 1, binding = 2) uniform sampler2D noiseTextureSampler;
layout (set = 1, binding = 3) uniform sampler2D elevationTextureSampler;
layout (set = 1, binding = 4) uniform sampler2D windTextureSamplerOne;
layout (set = 1, binding = 5) uniform sampler2D windTextureSamplerTwo;

// Input
layout (location = 0) in flat int iInInstanceIndex;
layout (location = 1) in vec2 f2InTexcoord;

// Output
layout (location = 0) out vec4 f4OutColor;

void main()
{
	vec2 f2WorldPosition = SmokeWorldPosition(globalLayout.f4SmokeArea, f2InTexcoord);
	vec2 f2VisibleAreaTexcoord = WorldToVisibleArea(vec3(f2WorldPosition, 0.0f), globalLayout.f4VisibleArea);
	float fElevation = texture(elevationTextureSampler, f2VisibleAreaTexcoord).x;

	f4OutColor = SmokeSpread(globalLayout, textureSampler, noiseTextureSampler, windTextureSamplerOne, windTextureSamplerTwo, f2InTexcoord, globalLayout.fSmokeNoiseScaleTwo, fElevation);

	// Extra decay over terrain
	if (fElevation > 0.0f)
	{
		f4OutColor.x *= pow(globalLayout.fSmokeDecay, max(1.0f, fElevation - 5.0f));
	}

	// Extra decay at edge of simulation area
	f4OutColor.x *= min(1.0f, globalLayout.fSmokeEdgeDecayDistanceInv * min(f2InTexcoord.x, min(1.0f - f2InTexcoord.x, min(f2InTexcoord.y, 1.0f - f2InTexcoord.y))));
}
