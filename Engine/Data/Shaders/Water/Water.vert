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

void Gertsner(vec2 f2Position, float fTerrainElevation)
{
	float fTime = globalLayout.fElapsedTime;
	float fMix = clamp(globalLayout.fBeachDirectionalFadeBottom + globalLayout.fBeachDirectionalFadeHeightInv * fTerrainElevation, 0.0f, 1.0f);
	vec2 f2LocalPosition = f2Position - vec2(globalLayout.fWaterWaveOriginX, globalLayout.fWaterWaveOriginY);

	float fLowSteepness = globalLayout.fWaterLowSteepness;
	vec3 f3TotalLow = vec3(f2Position, 0.0f);
	vec3 f3LowNormal = vec3(0.0f, 0.0f, 1.0f);
	for (int i = 0; i < globalLayout.iWaterLowCount; ++i)
	{
		float fOmega = mainLayout.pf4LowWavesTwo[i].x; // Frequency
		float fAmplitude = mainLayout.pf4LowWavesTwo[i].y;
		float fPhi = mainLayout.pf4LowWavesTwo[i].z; // Speed
		vec2 f2Direction = normalize(fMix * mainLayout.pf4LowWavesOne[i].zw + (1.0f - fMix) * mainLayout.pf4LowWavesOne[i].xy);

		float fWA = fOmega * fAmplitude;
		float fRadians = mainLayout.pf4LowWavesTwo[i].w + dot(mainLayout.pf4LowWavesOne[i].xy, f2LocalPosition) * fOmega + fPhi * (fTime + float(i));
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
		float fPhi = mainLayout.pf4MediumWavesTwo[i].z; // Speed
		vec2 f2Direction = mainLayout.pf4MediumWavesOne[i].xy;

		float fWA = fOmega * fAmplitude;
		float fRadians = mainLayout.pf4MediumWavesTwo[i].w + dot(f2Direction, f2LocalPosition) * fOmega + fPhi * fTime;
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
	f2OutInitialPosition = vec2
	(
		(1.0f - f2InTexcoord.x) * globalLayout.f4VisibleArea.x + f2InTexcoord.x * globalLayout.f4VisibleArea.z,
		(1.0f - f2InTexcoord.y) * globalLayout.f4VisibleArea.y + f2InTexcoord.y * globalLayout.f4VisibleArea.w
	);

	float fTerrainElevation = texture(elevationTextureSampler, WorldToVisibleArea(vec3(f2OutInitialPosition, 0.0f), globalLayout.f4VisibleArea)).x;
	if (fTerrainElevation > 1.0f)
	{
		gl_Position = vec4(0.0f, 0.0f, -100.0f, 0.0f);
		return;
	}

	Gertsner(f2OutInitialPosition, fTerrainElevation);

	f2OutTexcoord = WorldToVisibleArea(f3OutPosition, globalLayout.f4VisibleArea);

	if (fTerrainElevation >= -globalLayout.fWaterTerrainHeight)
	{
		f3OutPosition.z *= -fTerrainElevation / globalLayout.fWaterTerrainHeight;
	}

	gl_Position = Transform(vec4(f3OutPosition, 1.0f), mainLayout.f4x4ViewProjection);
}
