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

// Low- and medium-frequency Gerstner bands are split so main() can skip either
// independently when its main amplitude slider (gWaterLowAmplitude / gWaterMediumAmplitude)
// is zero. Both helpers accumulate into shared position/Jacobian state via inout params;
// caller derives the final normal from `cross(T, B)` once both bands have contributed.
//
// Full Jacobian accumulators (proper T×B cross-product, not the simplified Tessendorf form).
// Simplified `N.z = 1 - Σ Q·ω·A·sin` flips negative once the Finch invariant Σ Q·ω·A·sin > 1
// and `normalize()` then produces a downward normal. The full Jacobian keeps Q² cross-terms
// so N stays well-defined into the high-steepness regime.
//   fA: Σ Q·ω·A · D.x² · sin
//   fB: Σ Q·ω·A · D.x·D.y · sin
//   fD: Σ Q·ω·A · D.y² · sin
//   fG: Σ   ω·A · D.x · cos
//   fE: Σ   ω·A · D.y · cos
void GerstnerLow(vec2 f2LocalPosition, float fShoreAmplitude,
	inout vec3 f3Total,
	inout float fA, inout float fB, inout float fD,
	inout float fG, inout float fE)
{
	if (fShoreAmplitude <= 0.0f)
	{
		return;
	}

	float fLowSteepness = globalLayout.fWaterLowSteepness;
	for (int i = 0; i < globalLayout.iWaterLowCount; ++i)
	{
		float fOmega = mainLayout.pf4LowWavesTwo[i].x;
		float fAmplitude = mainLayout.pf4LowWavesTwo[i].y * fShoreAmplitude;
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
}

void GerstnerMedium(vec2 f2LocalPosition,
	inout vec3 f3Total,
	inout float fA, inout float fB, inout float fD,
	inout float fG, inout float fE)
{
	float fMediumSteepness = globalLayout.fWaterMediumSteepness;
	for (int i = 0; i < globalLayout.iWaterMediumCount; ++i)
	{
		float fOmega = mainLayout.pf4MediumWavesTwo[i].x;
		float fAmplitude = mainLayout.pf4MediumWavesTwo[i].y;
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

	// Over land (or no amplitude): skip Gerstner — the Z scale below would collapse to 0 anyway.
	float fTerrainElevation = textureLod(elevationTextureSampler, WorldToVisibleArea(vec3(f2WorldPosition, 0.0f), globalLayout.f4VisibleArea), 0.0f).x - globalLayout.fWaterHeight;
	float fGerstnerAmplitude = globalLayout.fWaterLowAmplitude + globalLayout.fWaterMediumAmplitude;
	if (fTerrainElevation >= 0.0f || fGerstnerAmplitude <= 0.0f)
	{
		f3OutPosition = vec3(f2WorldPosition, globalLayout.fWaterZOffsetTemp);
		f3OutNormal = vec3(0.0f, 0.0f, 1.0f);
		f2OutTexcoord = WorldToVisibleArea(f3OutPosition, globalLayout.f4VisibleArea);
		gl_Position = Transform(vec4(f3OutPosition, 1.0f), mainLayout.f4x4ViewProjection);
		return;
	}

	// Shore amplitude fade: 1.0 at/below bottom, 0.0 at/above top. Applied to low band only;
	// medium band is unaffected and the Z-taper below scales its displacement toward 0 at the beach.
	float fShoreAmplitude = clamp((fTerrainElevation - globalLayout.fBeachFadeTop) * globalLayout.fBeachFadeInvRange, 0.0f, 1.0f);

	vec3 f3Total = vec3(f2OutInitialPosition, 0.0f);
	float fA = 0.0f;
	float fB = 0.0f;
	float fD = 0.0f;
	float fG = 0.0f;
	float fE = 0.0f;

	if (globalLayout.fWaterLowAmplitude > 0.0f)
	{
		GerstnerLow(f2OutInitialPosition, fShoreAmplitude, f3Total, fA, fB, fD, fG, fE);
	}
	if (globalLayout.fWaterMediumAmplitude > 0.0f)
	{
		GerstnerMedium(f2OutInitialPosition, f3Total, fA, fB, fD, fG, fE);
	}

	// Tangent ∂P/∂u and bitangent ∂P/∂v of the summed Gerstner surface; normalize(cross) is the full Jacobian normal.
	// When both bands skipped, accumulators stay at zero → tangent (1,0,0), bitangent (0,1,0), normal (0,0,1).
	vec3 f3Tangent   = vec3(1.0f - fA, -fB, fG);
	vec3 f3Bitangent = vec3(-fB, 1.0f - fD, fE);
	vec3 f3WaveNormal = normalize(cross(f3Tangent, f3Bitangent));

	f3OutPosition = f3Total;
	f3OutNormal = normalize(mix(vec3(0.0f, 0.0f, 1.0f), f3WaveNormal, globalLayout.fWaterWaveNormalBlend));

	f3OutPosition.xy += vec2(globalLayout.fWaterOriginX, globalLayout.fWaterOriginY);
	f3OutPosition.z += globalLayout.fWaterHeight;

	f2OutTexcoord = WorldToVisibleArea(f3OutPosition, globalLayout.f4VisibleArea);

	if (fTerrainElevation >= -globalLayout.fWaterTerrainHeight)
	{
		f3OutPosition.z *= -fTerrainElevation / globalLayout.fWaterTerrainHeight;
	}

	f3OutPosition.z += globalLayout.fWaterZOffsetTemp; // DT: TEMP

	gl_Position = Transform(vec4(f3OutPosition, 1.0f), mainLayout.f4x4ViewProjection);
}
