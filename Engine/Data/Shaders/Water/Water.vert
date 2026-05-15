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
	float fGlobalAmplitudeFade = globalLayout.fWaterGlobalAmplitudeFade;

	vec3 f3Total = vec3(f2LocalPosition, 0.0f);

	// Full Jacobian accumulators (proper T×B cross-product, not the simplified Tessendorf form).
	// Simplified `N.z = 1 - Σ Q·ω·A·sin` flips negative once the Finch invariant Σ Q·ω·A·sin > 1
	// and `normalize()` then produces a downward normal. The full Jacobian keeps Q² cross-terms
	// so N stays well-defined into the high-steepness regime.
	float fA = 0.0f; // Σ Q·ω·A · D.x² · sin
	float fB = 0.0f; // Σ Q·ω·A · D.x·D.y · sin
	float fD = 0.0f; // Σ Q·ω·A · D.y² · sin
	float fG = 0.0f; // Σ   ω·A · D.x · cos
	float fE = 0.0f; // Σ   ω·A · D.y · cos

	float fLowSteepness = globalLayout.fWaterLowSteepness;
	for (int i = 0; i < globalLayout.iWaterLowCount; ++i)
	{
		float fOmega = mainLayout.pf4LowWavesTwo[i].x;
		float fAmplitude = mainLayout.pf4LowWavesTwo[i].y * fShoreAmplitude * fGlobalAmplitudeFade;
		vec2 f2Direction = mainLayout.pf4LowWavesOne[i].xy;

		float fReducedPhiTime = mainLayout.pf4LowWavesTwo[i].w;
		float fRadians = dot(f2Direction, f2LocalPosition) * fOmega + fReducedPhiTime;
		float fSin = sin(fRadians);
		float fCos = cos(fRadians);

		float fWA = fOmega * fAmplitude;
		float fQWASin = fLowSteepness * fWA * fSin;
		float fWACos = fWA * fCos;

		f3Total += vec3(fLowSteepness * fAmplitude * f2Direction * fCos, fAmplitude * fSin);

		fA += fQWASin * f2Direction.x * f2Direction.x;
		fB += fQWASin * f2Direction.x * f2Direction.y;
		fD += fQWASin * f2Direction.y * f2Direction.y;
		fG += fWACos * f2Direction.x;
		fE += fWACos * f2Direction.y;
	}

	float fMediumSteepness = globalLayout.fWaterMediumSteepness;
	for (int i = 0; i < globalLayout.iWaterMediumCount; ++i)
	{
		float fOmega = mainLayout.pf4MediumWavesTwo[i].x;
		float fAmplitude = mainLayout.pf4MediumWavesTwo[i].y * fGlobalAmplitudeFade;
		vec2 f2Direction = mainLayout.pf4MediumWavesOne[i].xy;

		float fReducedPhiTime = mainLayout.pf4MediumWavesTwo[i].w;
		float fRadians = dot(f2Direction, f2LocalPosition) * fOmega + fReducedPhiTime;
		float fSin = sin(fRadians);
		float fCos = cos(fRadians);

		float fWA = fOmega * fAmplitude;
		float fQWASin = fMediumSteepness * fWA * fSin;
		float fWACos = fWA * fCos;

		f3Total += vec3(fMediumSteepness * fAmplitude * f2Direction * fCos, fAmplitude * fSin);

		fA += fQWASin * f2Direction.x * f2Direction.x;
		fB += fQWASin * f2Direction.x * f2Direction.y;
		fD += fQWASin * f2Direction.y * f2Direction.y;
		fG += fWACos * f2Direction.x;
		fE += fWACos * f2Direction.y;
	}

	f3OutPosition = f3Total;

	// Tangent ∂P/∂u and bitangent ∂P/∂v of the summed Gerstner surface; normalize(cross) is the full Jacobian normal.
	vec3 f3Tangent   = vec3(1.0f - fA, -fB, fG);
	vec3 f3Bitangent = vec3(-fB, 1.0f - fD, fE);
	vec3 f3WaveNormal = normalize(cross(f3Tangent, f3Bitangent));

	f3OutNormal = normalize(mix(vec3(0.0f, 0.0f, 1.0f), f3WaveNormal, globalLayout.fWaterWaveNormalBlend));
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

	float fTerrainElevation = texture(elevationTextureSampler, WorldToVisibleArea(vec3(f2WorldPosition, 0.0f), globalLayout.f4VisibleArea)).x;
	if (fTerrainElevation > 1.0f)
	{
		gl_Position = vec4(0.0f, 0.0f, -100.0f, 0.0f);
		return;
	}

	Gertsner(f2OutInitialPosition, fTerrainElevation);
	f3OutPosition.xy += vec2(globalLayout.fWaterOriginX, globalLayout.fWaterOriginY);

	f2OutTexcoord = WorldToVisibleArea(f3OutPosition, globalLayout.f4VisibleArea);

	if (fTerrainElevation >= -globalLayout.fWaterTerrainHeight)
	{
		f3OutPosition.z *= -fTerrainElevation / globalLayout.fWaterTerrainHeight;
	}

	f3OutPosition.z += globalLayout.fWaterZOffsetTemp; // DT: TEMP

	gl_Position = Transform(vec4(f3OutPosition, 1.0f), mainLayout.f4x4ViewProjection);
}
