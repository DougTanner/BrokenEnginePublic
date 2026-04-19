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

	// 2D velocity direction for quad orientation (top-down deposit space)
	vec4 f4Center = particles.pParticles[i].f4Position;
	vec2 f2Velocity2d = particles.pParticles[i].f4Velocity.xy;
	float fVelocityLength2d = length(f2Velocity2d);
	vec2 f2Direction = fVelocityLength2d > 0.0001f ? f2Velocity2d / fVelocityLength2d : vec2(1.0f, 0.0f);
	vec2 f2Left = vec2(-f2Direction.y, f2Direction.x);

	float fLightingScale = particles.pParticles[i].fLightingSize;
	float fWidth = particles.pParticles[i].fSize * fLightingScale;
	float fLength = particles.pParticles[i].fLength * fLightingScale;

	// Mirror visible-render velocity-stretch so the deposit matches the on-screen streak
	float fVelocityLength3d = length(particles.pParticles[i].f4Velocity.xyz);
	float fLengthMultiplier = 1.0f + globalLayout.fParticlesStretchVelocityMultiplier * clamp((fVelocityLength3d - globalLayout.fParticlesStretchVelocityStart) / (globalLayout.fParticlesStretchVelocityEnd - globalLayout.fParticlesStretchVelocityStart), 0.0f, 1.0f);
	fLength *= fLengthMultiplier;

	// f2InQuadVertex.x -> perpendicular (width), f2InQuadVertex.y -> along velocity (length), matching LongParticlesRender.vert
	vec2 f2Offset = fWidth * -f2Left + f2InQuadVertex.x * 2.0f * fWidth * f2Left;
	f2Offset += fLength * f2Direction + f2InQuadVertex.y * 2.0f * fLength * -f2Direction;

	float fWorldX = f4Center.x + f2Offset.x;
	float fWorldY = f4Center.y + f2Offset.y;

	gl_Position = vec4(-1.0f + 2.0f * (fWorldX - globalLayout.f4LightingArea.x) / (globalLayout.f4LightingArea.z - globalLayout.f4LightingArea.x),
	                    1.0f - 2.0f * (fWorldY - globalLayout.f4LightingArea.y) / (globalLayout.f4LightingArea.w - globalLayout.f4LightingArea.y),
					    0.0f,
					    1.0f);

	f2OutTexcoord = f2InQuadVertex;
}
