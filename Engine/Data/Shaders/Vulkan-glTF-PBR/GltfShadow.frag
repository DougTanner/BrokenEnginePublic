// Based on https://github.com/SaschaWillems/Vulkan-glTF-PBR

#version 460

#include "ShaderLayouts.h"
#include "ShaderFunctions.h"

// Push constants
layout(push_constant) uniform pushConstants
{
	PushConstantsLayout pushConstantsLayout;
};

// Input
layout (location = 0) in vec3 f3InWorldPosition;
layout (location = 1) in vec3 f3InNormal;
layout (location = 2) in vec2 f2InUV;
layout (location = 3) in vec4 f4InColorAdd;

// Output
layout (location = 0) out float fOutColor;

void main()
{
	fOutColor = 0.0f;
}
