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
layout (location = 0) out flat int iOutInstanceIndex;
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

	vec4 f4Center = particles.pParticles[i].f4Position;
	vec3 f3ToEyeNormal = normalize(mainLayout.f4EyePosition.xyz - f4Center.xyz);

	// Direction from velocity; guard zero-velocity (newly-spawned / stalled) — fall back to eye-facing
	vec3 f3Velocity = particles.pParticles[i].f4Velocity.xyz;
	float fVelocitySquared = dot(f3Velocity, f3Velocity);
	float fVelocityLength = sqrt(fVelocitySquared);
	vec3 f3Direction = (fVelocitySquared > kfEpsilon * kfEpsilon) ? f3Velocity / fVelocityLength : f3ToEyeNormal;

	float fWidth = particles.pParticles[i].fSize;
	float fLength = particles.pParticles[i].fLength;

	// Cross product front direction and eye to get left direction; guard view-parallel-velocity singularity (top-down RTS)
	vec3 f3CrossCandidate = cross(f3ToEyeNormal, f3Direction);
	float fCrossSquared = dot(f3CrossCandidate, f3CrossCandidate);
	vec3 f3LeftNormal;
	if (fCrossSquared > kfEpsilon * kfEpsilon)
	{
		f3LeftNormal = f3CrossCandidate * inversesqrt(fCrossSquared);
	}
	else
	{
		vec3 f3WorldUp = abs(f3ToEyeNormal.z) < 0.999f ? vec3(0.0f, 0.0f, 1.0f) : vec3(1.0f, 0.0f, 0.0f);
		f3LeftNormal = normalize(cross(f3ToEyeNormal, f3WorldUp));
	}

	// Calculate length multiplier of particle based on velocity (stretch-range reciprocal folded CPU-side)
	float fLengthMultiplier = 1.0f + globalLayout.fParticlesStretchVelocityMultiplier * clamp((fVelocityLength - globalLayout.fParticlesStretchVelocityStart) * globalLayout.fParticlesStretchRangeInv, 0.0f, 1.0f);
	
	// Use the vertex texcoords to place the vertex at the correct corner
	vec4 f4Position = f4Center;
	f4Position.xyz += fWidth * -f3LeftNormal + f2InQuadVertex.x * 2.0f * fWidth * f3LeftNormal;
	f4Position.xyz += fLengthMultiplier * fLength * f3Direction + f2InQuadVertex.y * 2.0f * fLengthMultiplier * fLength * -f3Direction;

	// Transform world position into projection space
	f4Position.w = 1.0f;
	gl_Position = Transform(f4Position, mainLayout.f4x4ViewProjection);
	f3OutWorldPosition = f4Position.xyz;

	f2OutTexcoord = f2InQuadVertex;
}
