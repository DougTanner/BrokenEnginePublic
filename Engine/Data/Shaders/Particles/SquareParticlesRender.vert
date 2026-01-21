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

	// Normal from particle center to eye position
	vec4 f4Center = particles.pParticles[i].f4Position;
	vec3 f3ToEyeNormal = normalize(mainLayout.f4EyePosition.xyz - f4Center.xyz);
	
	// Cross product world up and eye to get left direction, then calculate up
	vec3 f3WorldUp = vec3(0.0f, 0.0f, 1.0f);
	vec3 f3LeftNormal = normalize(cross(f3ToEyeNormal, f3WorldUp));
	vec3 f3UpNormal = normalize(cross(f3LeftNormal, f3ToEyeNormal));

	// Use the vertex texcoords to place the vertex at the correct corner
	float fSize = particles.pParticles[i].f4MiscTwo.x;
	vec4 f4Position = f4Center;
	f4Position.xyz += fSize * -f3LeftNormal + f2InQuadVertex.x * 2.0f * fSize * f3LeftNormal;
	f4Position.xyz += fSize * f3UpNormal + f2InQuadVertex.y * 2.0f * fSize * -f3UpNormal;

	// Transform world position into projection space
	gl_Position = Transform(f4Position, mainLayout.f4x4ViewProjection);
	f3OutWorldPosition = f4Position.xyz;

	// Rotate texcoords
	float fRotation = particles.pParticles[i].f4MiscThree.y;
	f2OutTexcoord = f2InQuadVertex;
	f2OutTexcoord -= vec2(0.5f, 0.5f);
	f2OutTexcoord = Rotate(f2OutTexcoord, fRotation);
	f2OutTexcoord += vec2(0.5f, 0.5f);
}
