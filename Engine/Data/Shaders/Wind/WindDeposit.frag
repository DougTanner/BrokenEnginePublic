#version 460

#include "ShaderLayouts.h"

// Uniforms
layout (binding = 0) uniform globalUniform
{
	GlobalLayout globalLayout;
};

layout (binding = 2) uniform sampler2D textureSampler;

// Input
layout (location = 0) in flat int iInInstanceIndex;
layout (location = 1) in vec4 f4InParams;
layout (location = 2) in vec2 f2InTexcoord;

// Output
layout (location = 0) out vec4 f4OutColor;

void main()
{
	float fFalloff = texture(textureSampler, f2InTexcoord).r;
	float fMagnitude = f4InParams.x;

	vec2 f2WindDir;
	if (f4InParams.w > 0.5)
	{
		// Radial: compute outward direction from quad center
		vec2 f2FromCenter = f2InTexcoord - vec2(0.5);
		float fLen = length(f2FromCenter);
		f2WindDir = fLen > 0.001 ? f2FromCenter / fLen : vec2(0.0);
		f2WindDir.y = -f2WindDir.y;  // Texcoord Y is inverted relative to world Y
	}
	else
	{
		// Directional: use CPU-computed direction
		f2WindDir = f4InParams.yz;
	}

	f4OutColor = vec4(fFalloff * fMagnitude * f2WindDir, 0.0, 1.0);
}
