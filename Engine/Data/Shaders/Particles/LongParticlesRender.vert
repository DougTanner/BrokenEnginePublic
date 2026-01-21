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
layout (location = 0) out flat int iOutInstanceIndex;
layout (location = 1) out vec3 f3OutWorldPosition;
layout (location = 2) out vec2 f2OutTexcoord;

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

	vec3 f3Direction = normalize(particles.pParticles[i].f4Velocity.xyz);
	float fWidth = particles.pParticles[i].f4MiscTwo.x;
	float fLength = particles.pParticles[i].f4MiscTwo.y;

	// Normal from particle center to eye position
	vec4 f4Center = particles.pParticles[i].f4Position;
	vec3 f3ToEyeNormal = normalize(mainLayout.f4EyePosition.xyz - f4Center.xyz);
	
	// Cross product front direction and eye to get left direction
	vec3 f3LeftNormal = normalize(cross(f3ToEyeNormal, f3Direction));
	
	// Calculate length multiplier of particle based on velocity
	float fVelocityLength = length(particles.pParticles[i].f4Velocity.xyz);
	float fLengthMultiplier = 1.0f + globalLayout.f4ParticlesOne.z * clamp((fVelocityLength - globalLayout.f4ParticlesOne.x) / (globalLayout.f4ParticlesOne.y - globalLayout.f4ParticlesOne.x), 0.0f, 1.0f);
	
	// Use the vertex texcoords to place the vertex at the correct corner
	vec4 f4Position = f4Center;
	f4Position.xyz += fWidth * -f3LeftNormal + f2InQuadVertex.x * 2.0f * fWidth * f3LeftNormal;
	f4Position.xyz += fLengthMultiplier * fLength * f3Direction + f2InQuadVertex.y * 2.0f * fLengthMultiplier * fLength * -f3Direction;

	// Transform world position into projection space
	gl_Position = Transform(f4Position, mainLayout.f4x4ViewProjection);
	f3OutWorldPosition = f4Position.xyz;

	f2OutTexcoord = f2InQuadVertex;
}
