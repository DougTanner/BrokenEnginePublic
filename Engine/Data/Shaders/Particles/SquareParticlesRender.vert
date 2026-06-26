#version 460

#include "ShaderLayouts.h"
#include "ShaderFunctions.h"

// Uniforms
layout (set = 0, binding = kiGlobalBindingGlobalUniform) uniform globalUniform
{
    GlobalLayout globalLayout;
};

layout (set = 0, binding = kiGlobalBindingMainUniform) uniform mainUniform
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
layout (location = 1) out vec3 f3OutWorldPosition;
layout (location = 2) out vec2 f2OutTexcoord;

void main()
{
	int32_t i = int32_t(gl_InstanceIndex);
	iOutInstanceIndex = i;
#if defined(ENABLE_32_BIT_BOOL)
	if ((particles.puiAllocated[i / 32] & (1u << (uint(i) % 32u))) == 0u)
#else
	if (particles.pbAllocated[i] == 0)
#endif
	{
		gl_Position = vec4(0.0f, 0.0f, 0.0f, 1.0f);
		return;
	}

	// Hoist SSBO reads (one access per field)
	vec4 f4Center = particles.pParticles[i].f4Position;
	float fSize = particles.pParticles[i].fSize;
	float fRotation = particles.pParticles[i].fRotation;

	// Normal from particle center to eye position; guard particle-at-eye singularity
	vec3 f3EyeOffset = mainLayout.f4EyePosition.xyz - f4Center.xyz;
	float fEyeSquared = dot(f3EyeOffset, f3EyeOffset);
	vec3 f3ToEyeNormal = (fEyeSquared > kfEpsilon * kfEpsilon) ? f3EyeOffset * inversesqrt(fEyeSquared) : vec3(0.0f, 0.0f, 1.0f);

	// Cross product world up and eye to get left direction; perturb world up when view is parallel (top-down RTS camera)
	vec3 f3WorldUp = abs(f3ToEyeNormal.z) < 0.999f ? vec3(0.0f, 0.0f, 1.0f) : vec3(1.0f, 0.0f, 0.0f);
	vec3 f3LeftNormal = normalize(cross(f3ToEyeNormal, f3WorldUp));
	// Cross of two perpendicular unit vectors is already unit; no outer normalize needed
	vec3 f3UpNormal = cross(f3LeftNormal, f3ToEyeNormal);

	// Use the vertex texcoords to place the vertex at the correct corner
	vec4 f4Position = f4Center;
	f4Position.xyz += fSize * -f3LeftNormal + f2InQuadVertex.x * 2.0f * fSize * f3LeftNormal;
	f4Position.xyz += fSize * f3UpNormal + f2InQuadVertex.y * 2.0f * fSize * -f3UpNormal;

	// Transform world position into projection space
	f4Position.w = 1.0f;
	gl_Position = Transform(f4Position, mainLayout.f4x4ViewProjection);
	f3OutWorldPosition = f4Position.xyz;

	// Rotate texcoords
	f2OutTexcoord = f2InQuadVertex;
	f2OutTexcoord -= vec2(0.5f, 0.5f);
	f2OutTexcoord = Rotate(f2OutTexcoord, fRotation);
	f2OutTexcoord += vec2(0.5f, 0.5f);
}
