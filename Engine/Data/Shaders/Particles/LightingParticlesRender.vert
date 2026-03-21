#version 460

#include "ShaderLayouts.h"
#include "ShaderFunctions.h"

// Uniforms
layout (set = 0, binding = 0) uniform globalUniform
{
	GlobalLayout globalLayout;
};

layout (set = 0, binding = 1) uniform mainUniform
{
	MainLayout mainLayout;
};

layout (scalar, set = 1, binding = 2) buffer readonly particlesUniform
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
	float fSize = particles.pParticles[i].fLightingSize * particles.pParticles[i].fSize;

	float fWorldX = f4Center.x - fSize + 2.0f * f2InQuadVertex.x * fSize;
	float fWorldY = f4Center.y + fSize - 2.0f * f2InQuadVertex.y * fSize;
	gl_Position = vec4(-1.0f + 2.0f * (fWorldX - globalLayout.f4LightingArea.x) / (globalLayout.f4LightingArea.z - globalLayout.f4LightingArea.x),
	                    1.0f - 2.0f * (fWorldY - globalLayout.f4LightingArea.y) / (globalLayout.f4LightingArea.w - globalLayout.f4LightingArea.y),
					    0.0f,
					    1.0f);

	f2OutTexcoord = f2InQuadVertex;
}
