#version 460

#extension GL_EXT_nonuniform_qualifier : require

#include "ShaderLayouts.h"
#include "ShaderFunctions.h"

// Uniforms
layout (set = 1, binding = 2) uniform sampler2D textureSampler[kiMaxIslands];

// Input
layout (location = 0) in flat int iInInstanceIndex;
layout (location = 1) in vec4 f4InMisc;
layout (location = 2) in vec2 f2InTexcoord;

// Output
layout (location = 0) out vec4 f4OutColor;

void main()
{
	vec4 f4Color = texture(textureSampler[nonuniformEXT(iInInstanceIndex)], f2InTexcoord);

	// Per-island normal flip: 1.0 - n maps (0.5 + 0.5*x) to (0.5 - 0.5*x), negating the decoded direction
	if (f4InMisc.y > 0.5f) // flipX
		f4Color.x = 1.0f - f4Color.x;
	if (f4InMisc.z > 0.5f) // flipY
		f4Color.y = 1.0f - f4Color.y;

	f4OutColor = f4Color;
}
