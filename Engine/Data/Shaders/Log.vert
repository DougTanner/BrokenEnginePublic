#version 460
#extension GL_EXT_debug_printf : require

#include "ShaderLayouts.h"
#include "ShaderFunctions.h"

// Push constants
layout(push_constant) uniform pushConstants
{
	PushConstantsLayout pushConstantsLayout;
};

// Uniforms
layout (set = 0, binding = 0) uniform globalUniform
{
    GlobalLayout globalLayout;
};

layout (set = 0, binding = 1) uniform mainUniform
{
	MainLayout mainLayout;
};

// Input
layout (location = 0) in vec2 f2InQuadVertex;

// Output
layout (location = 0) out flat int iOutInstanceIndex;
layout (location = 1) out vec2 f2OutTexcoord;

void main()
{
	if (f2InQuadVertex == vec2(0.0f, 0.0f))
	{
		debugPrintfEXT("Log.vert Frame: %d Render: %d", mainLayout.iFrameNumber, mainLayout.iRenderNumber);
	}

	gl_Position = vec4(f2InQuadVertex, 0.0f, 0.0f);
}
