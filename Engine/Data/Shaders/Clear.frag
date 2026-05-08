#version 460

#include "ShaderLayouts.h"
#include "ShaderFunctions.h"

// Push constants
layout(push_constant) uniform pushConstants
{
	PushConstantsLayout pushConstantsLayout;
};

// Input — declared (not used) so VS-out interface matches paired vertex shaders (Log.vert, QuadsFullscreen.vert) and Vulkan validation does not raise WARNING-Shader-OutputNotConsumed
layout (location = 0) in flat int iInInstanceIndex;
layout (location = 1) in vec2 f2InTexcoord;

// Output
layout (location = 0) out vec4 f4OutColor;

void main()
{
    f4OutColor = pushConstantsLayout.f4Pipeline;
}
