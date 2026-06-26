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
layout (set = 0, binding = kiGlobalBindingGlobalUniform) uniform globalUniform
{
    GlobalLayout globalLayout;
};

layout (set = 0, binding = kiGlobalBindingMainUniform) uniform mainUniform
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
	if (gl_VertexIndex == 0)
	{
		debugPrintfEXT("Log.vert Frame: %d Render: %d", mainLayout.iFrameNumber, mainLayout.iRenderNumber);
	}

	iOutInstanceIndex = gl_InstanceIndex;
	f2OutTexcoord = f2InQuadVertex;

	gl_Position = vec4(-1.0f + 2.0f * f2InQuadVertex.x, 1.0f - 2.0f * f2InQuadVertex.y, 0.0f, 1.0f);
}
