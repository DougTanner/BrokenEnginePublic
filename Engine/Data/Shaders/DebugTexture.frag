#version 460

#include "ShaderLayouts.h"

// Push constants
layout(push_constant) uniform pushConstants
{
	PushConstantsLayout pushConstantsLayout;
};

// Samplers
layout (set = 1, binding = 1) uniform sampler2D depositSampler;
layout (set = 1, binding = 2) uniform sampler2D firstSpreadSampler;

// Input
layout (location = 0) in flat int iInInstanceIndex;
layout (location = 1) in vec2 f2InTexcoord;

// Output
layout (location = 0) out vec4 f4OutColor;

void main()
{
	vec4 f4Sample = (pushConstantsLayout.f4Pipeline.y > 0.5f)
		? texture(firstSpreadSampler, f2InTexcoord)
		: texture(depositSampler, f2InTexcoord);

	if (pushConstantsLayout.f4Pipeline.x > 0.5f)
	{
		// Lighting EWNS visualization: R = west(0) to east(1), G = south(0) to north(1)
		float fTotal = f4Sample.r + f4Sample.g + f4Sample.b + f4Sample.a;
		float fIntensity = min(fTotal / 100.0f, 1.0f);
		float fEastWest = fTotal > 0.0f ? (f4Sample.r - f4Sample.g) / fTotal * 0.5f + 0.5f : 0.0f;
		float fNorthSouth = fTotal > 0.0f ? (f4Sample.a - f4Sample.b) / fTotal * 0.5f + 0.5f : 0.0f;
		f4OutColor = vec4(fEastWest * fIntensity, fNorthSouth * fIntensity, 0.0f, 1.0f);
	}
	else
	{
		f4OutColor = vec4(f4Sample.rgb, 1.0f);
	}
}
