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

layout (binding = 5) uniform sampler2D elevationTextureSampler;
layout (binding = 7) uniform sampler2D noiseTextureSampler;

// Input
layout (location = 0) in vec2 f2InTexcoord;

// Output
layout (location = 0) out vec2 f2OutInitialPosition;
layout (location = 1) out vec3 f3OutPosition;
layout (location = 2) out vec2 f2OutTexcoord;
layout (location = 3) out vec3 f3OutNormal;

void Gertsner(vec2 f2Position, float fNoise, float fTerrainElevation)
{
	float fTime = globalLayout.f4Misc.x;
	float fMix = clamp(globalLayout.f4WaterSix.y + globalLayout.f4WaterSix.z * fTerrainElevation, 0.0f, 1.0f);

	float fLowSteepness = globalLayout.f4WaterSix.w;
	vec3 f3TotalLow = vec3(f2Position, 0.0f);
	vec3 f3LowNormal = vec3(0.0f, 0.0f, 1.0f);
	for (int i = 0; i < globalLayout.i4Water.x; ++i)
	{
		float fOmega = mainLayout.pf4LowWavesTwo[i].x; // Wavelength
		float fAmplitude = mainLayout.pf4LowWavesTwo[i].y;
		float fPhi = mainLayout.pf4LowWavesTwo[i].z; // Speed
		vec2 f2Direction = normalize(fMix * mainLayout.pf4LowWavesOne[i].zw + (1.0f - fMix) * mainLayout.pf4LowWavesOne[i].xy);

		float fWA = fOmega * fAmplitude;
		float fRadians = dot(f2Direction, f2Position) * fOmega + fPhi * (fTime + float(i));
		float fSin = sin(fRadians);
		float fCos = cos(fRadians);

		f3TotalLow += vec3(fLowSteepness * fAmplitude * f2Direction * fCos, fAmplitude * fSin);
		f3LowNormal.x -= f2Direction.x * fWA * fCos;
		f3LowNormal.y -= f2Direction.y * fWA * fCos;
		f3LowNormal.z -= fLowSteepness * fWA * fSin;
	}

	float fMediumSteepness = globalLayout.f4WaterSeven.x;
	vec3 f3TotalMedium = f3TotalLow;
	vec3 f3MediumNormal = vec3(0.0f, 0.0f, 1.0f);
	for (int i = 0; i < globalLayout.i4Water.y; ++i)
	{
		float fOmega = mainLayout.pf4MediumWavesTwo[i].x; // Wavelength
		float fAmplitude = mainLayout.pf4MediumWavesTwo[i].y;
		float fPhi = mainLayout.pf4MediumWavesTwo[i].z; // Speed
		vec2 f2Direction = mainLayout.pf4MediumWavesOne[i].xy;

		float fWA = fOmega * fAmplitude;
		float fRadians = dot(f2Direction, f2Position) * fOmega + fPhi * fTime;
		float fSin = sin(fRadians);
		float fCos = cos(fRadians);

		f3TotalMedium += vec3(fMediumSteepness * fAmplitude * f2Direction * fCos, fAmplitude * fSin);
		f3LowNormal.x -= f2Direction.x * fWA * fCos;
		f3LowNormal.y -= f2Direction.y * fWA * fCos;
		f3LowNormal.z -= fMediumSteepness * fWA * fSin;
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

	float fNoise = globalLayout.f4WaterOne.w * texture(noiseTextureSampler, globalLayout.f4WaterOne.z * f2OutInitialPosition).x;
	Gertsner(f2OutInitialPosition, fNoise, fTerrainElevation);

	f2OutTexcoord = WorldToVisibleArea(f3OutPosition, globalLayout.f4VisibleArea);

	if (fTerrainElevation >= -globalLayout.f4WaterOne.x)
	{
		f3OutPosition.z *= -fTerrainElevation / globalLayout.f4WaterOne.x;
	}

	gl_Position = Transform(vec4(f3OutPosition, 1.0f), mainLayout.f4x4ViewProjection);
}
