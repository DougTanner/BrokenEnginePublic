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

layout (set = 1, binding = 5) uniform sampler2D elevationTextureSampler;
layout (set = 1, binding = 7) uniform sampler2D noiseTextureSampler;

// Input
layout (location = 0) in vec2 f2InTexcoord;

// Output
layout (location = 0) out vec2 f2OutInitialPosition;
layout (location = 1) out vec3 f3OutPosition;
layout (location = 2) out vec2 f2OutTexcoord;
layout (location = 3) out vec3 f3OutNormal;

void Gertsner(vec2 f2LocalPosition, float fTerrainElevation)
{
	// Shore amplitude fade: 1.0 at/below bottom, 0.0 at/above top
	float fShoreAmplitude = clamp((fTerrainElevation - globalLayout.fBeachFadeTop) * globalLayout.fBeachFadeInvRange, 0.0f, 1.0f);

	float fLowSteepness = globalLayout.fWaterLowSteepness;
	vec3 f3TotalLow = vec3(f2LocalPosition, 0.0f);
	vec3 f3LowNormal = vec3(0.0f, 0.0f, 1.0f);
	for (int i = 0; i < globalLayout.iWaterLowCount; ++i)
	{
		float fOmega = mainLayout.pf4LowWavesTwo[i].x; // Frequency
		float fAmplitude = mainLayout.pf4LowWavesTwo[i].y * fShoreAmplitude;
		vec2 f2Direction = mainLayout.pf4LowWavesOne[i].xy;

		float fWA = fOmega * fAmplitude;
		float fReducedPhiTime = mainLayout.pf4LowWavesTwo[i].w;
		float fRadians = dot(f2Direction, f2LocalPosition + globalLayout.fWaterDebugLowWaveOffset) * fOmega + fReducedPhiTime;
		float fSin = sin(fRadians);
		float fCos = cos(fRadians);

		f3TotalLow += vec3(fLowSteepness * fAmplitude * f2Direction * fCos, fAmplitude * fSin);
		f3LowNormal.x -= f2Direction.x * fWA * fCos;
		f3LowNormal.y -= f2Direction.y * fWA * fCos;
		f3LowNormal.z -= fLowSteepness * fWA * fSin;
	}

	float fMediumSteepness = globalLayout.fWaterMediumSteepness;
	vec3 f3TotalMedium = f3TotalLow;
	vec3 f3MediumNormal = vec3(0.0f, 0.0f, 1.0f);
	for (int i = 0; i < globalLayout.iWaterMediumCount; ++i)
	{
		float fOmega = mainLayout.pf4MediumWavesTwo[i].x; // Frequency
		float fAmplitude = mainLayout.pf4MediumWavesTwo[i].y;
		vec2 f2Direction = mainLayout.pf4MediumWavesOne[i].xy;

		float fWA = fOmega * fAmplitude;
		float fReducedPhiTime = mainLayout.pf4MediumWavesTwo[i].w;
		float fRadians = dot(f2Direction, f2LocalPosition + globalLayout.fWaterDebugMediumWaveOffset) * fOmega + fReducedPhiTime;
		float fSin = sin(fRadians);
		float fCos = cos(fRadians);

		f3TotalMedium += vec3(fMediumSteepness * fAmplitude * f2Direction * fCos, fAmplitude * fSin);
		f3MediumNormal.x -= f2Direction.x * fWA * fCos;
		f3MediumNormal.y -= f2Direction.y * fWA * fCos;
		f3MediumNormal.z -= fMediumSteepness * fWA * fSin;
	}

	f3OutPosition = f3TotalMedium;
	f3OutNormal = normalize(f3LowNormal + f3MediumNormal);
}

void main()
{
	vec2 f2WorldPosition = vec2
	(
		(1.0f - f2InTexcoord.x) * globalLayout.f4VisibleArea.x + f2InTexcoord.x * globalLayout.f4VisibleArea.z,
		(1.0f - f2InTexcoord.y) * globalLayout.f4VisibleArea.y + f2InTexcoord.y * globalLayout.f4VisibleArea.w
	);

	// Camera-relative position for precision in fragment shader UV computation
	f2OutInitialPosition = f2WorldPosition - vec2(globalLayout.fWaterOriginX, globalLayout.fWaterOriginY);

	float fTerrainElevation = texture(elevationTextureSampler, WorldToVisibleArea(vec3(f2WorldPosition, 0.0f), globalLayout.f4VisibleArea)).x - globalLayout.fWaterHeight;
	if (fTerrainElevation > 1.0f)
	{
		gl_Position = vec4(0.0f, 0.0f, -100.0f, 0.0f);
		return;
	}

	Gertsner(f2OutInitialPosition, fTerrainElevation);
	f3OutPosition.xy += vec2(globalLayout.fWaterOriginX, globalLayout.fWaterOriginY);
	f3OutPosition.z += globalLayout.fWaterHeight;

	f2OutTexcoord = WorldToVisibleArea(f3OutPosition, globalLayout.f4VisibleArea);

	if (fTerrainElevation >= -globalLayout.fWaterTerrainHeight)
	{
		f3OutPosition.z *= -fTerrainElevation / globalLayout.fWaterTerrainHeight;
	}

	gl_Position = Transform(vec4(f3OutPosition, 1.0f), mainLayout.f4x4ViewProjection);
}
