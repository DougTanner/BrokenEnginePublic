#version 460

#include "ShaderLayouts.h"
#include "ShaderFunctions.h"

// Uniforms
layout (binding = 2) buffer readonly billboardsUniform
{
	BillboardLayout pBillboards[];
};

layout (binding = 3) uniform sampler texturesSampler;
layout (binding = 4) uniform texture2D pTextures[kiTextureCount];

// Input
layout (location = 0) in flat int iInInstanceIndex;
layout (location = 1) in vec2 f2InTexcoord;

// Output
layout (location = 0) out vec4 f4OutColor;

void main()
{
	f4OutColor = texture(sampler2D(pTextures[int32_t(pBillboards[iInInstanceIndex].f4Misc.y)], texturesSampler), f2InTexcoord);
	f4OutColor.a *= pBillboards[iInInstanceIndex].f4Misc.w;
}
