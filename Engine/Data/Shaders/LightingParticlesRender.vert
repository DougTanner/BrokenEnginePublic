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

layout (binding = 2) buffer readonly particlesUniform
{
	ParticlesLayout particles;
};

// Input
layout (location = 0) in vec2 f2InQuadVertex;

// Output
layout (location = 0) out flat int32_t iOutInstanceIndex;
layout (location = 1) out vec2 f2OutTexcoord;

void main()
{
	int32_t i = int32_t(gl_InstanceIndex);
	iOutInstanceIndex = i;
#if defined(ENABLE_32_BIT_BOOL)
	if ((particles.puiAllocated[i / 32] & (1 << (i % 32))) == 0)
#else
	if (particles.pbAllocated[i] == 0)
#endif
	{
		gl_Position = vec4(0.0f, 0.0f, 0.0f, 1.0f);
		return;
	}

	vec4 f4Center = particles.pParticles[i].f4Position;
	float fSize = particles.pParticles[i].f4MiscOne.w * particles.pParticles[i].f4MiscTwo.x;

	float fWorldX = f4Center.x - fSize + 2.0f * f2InQuadVertex.x * fSize;
	float fWorldY = f4Center.y + fSize - 2.0f * f2InQuadVertex.y * fSize;
	gl_Position = vec4(-1.0f + 2.0f * (fWorldX - globalLayout.f4VisibleArea.x) / (globalLayout.f4VisibleArea.z - globalLayout.f4VisibleArea.x),
	                    1.0f - 2.0f * (fWorldY - globalLayout.f4VisibleArea.y) / (globalLayout.f4VisibleArea.w - globalLayout.f4VisibleArea.y),
					    0.0f,
					    1.0f);

	f2OutTexcoord = f2InQuadVertex;
}
