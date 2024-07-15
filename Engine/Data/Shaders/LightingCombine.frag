#version 460

#include "ShaderLayouts.h"
#include "ShaderFunctions.h"

// Uniforms
layout (binding = 0) uniform globalUniform
{
	GlobalLayout globalLayout;
};

layout (binding = 1) uniform mainUniform
{
	MainLayout mainLayout;
};

layout (binding = 2) uniform sampler2D lightingSamplers[kiMaxLightingBlurCount];

// Input
layout (location = 0) in flat int iInInstanceIndex;
layout (location = 1) in vec2 f2InTexcoord;

// Output
layout (location = 0) out vec4 f4OutColor;

void main()
{
	f4OutColor = vec4(0.0f, 0.0f, 0.0f, 0.0f);
	int iCombineCount = int(globalLayout.f4LightingTwo.y);
	float fMult = globalLayout.f4LightingThree.w;
	float fCurrent = mainLayout.fLightingTimeOfDayMultiplier;
	for (int i = 0; i < iCombineCount; ++i)
	{
		f4OutColor += fCurrent * texture(lightingSamplers[i], f2InTexcoord);
		fCurrent *= fMult;
	}
}
