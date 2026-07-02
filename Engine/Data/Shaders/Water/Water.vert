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

layout (set = 1, binding = 5) uniform sampler2D elevationTextureSampler;
layout (set = 1, binding = 7) uniform sampler2D noiseTextureSampler;
// Pre-computed Gerstner displacement (Δx, Δy, Δz in camera-relative space) and Jacobian-derived
// flat-blended normal. Written by WaterDisplacement.comp once per frame so this vertex shader
// texelFetches one value per vertex instead of summing the wave bands itself.
// Texel grid is in 1:1 alignment with the active LOD's vertex grid — see
// MainUniforms.cpp where iWaterActiveQuadX/Y populates next to the LOD draw setup, and
// RenderTargetTextures.h for the texture creation comment.
layout (set = 1, binding = kiWaterBindingDisplacement) uniform sampler2D displacementTextureSampler;
layout (set = 1, binding = kiWaterBindingDisplacementNormal) uniform sampler2D displacementNormalTextureSampler;

// Input
layout (location = 0) in vec2 f2InTexcoord;

// Output
layout (location = 0) out vec2 f2OutInitialPosition;
layout (location = 1) out vec3 f3OutPosition;
layout (location = 2) out vec2 f2OutTexcoord;
layout (location = 3) out vec3 f3OutNormal;

void main()
{
	vec2 f2WorldPosition = VisibleAreaToWorld(f2InTexcoord, globalLayout.f4VisibleArea);

	// Camera-relative position for precision in fragment shader UV computation
	f2OutInitialPosition = f2WorldPosition - vec2(globalLayout.fWaterOriginX, globalLayout.fWaterOriginY);

	// Over land: skip the displacement sample — the Z scale below would collapse to 0 anyway. Emit a
	// flat vertex at world-Z=0 with up-normal; the fragment shader handles all elevations directly.
	float fTerrainElevation = textureLod(elevationTextureSampler, WorldToVisibleArea(vec3(f2WorldPosition, 0.0f), globalLayout.f4VisibleArea), 0.0f).x - globalLayout.fWaterHeight;
	if (fTerrainElevation >= 0.0f)
	{
		f3OutPosition = vec3(f2WorldPosition, globalLayout.fWaterZOffsetTemp);
		f3OutNormal = vec3(0.0f, 0.0f, 1.0f);
		f2OutTexcoord = WorldToVisibleArea(f3OutPosition, globalLayout.f4VisibleArea);
		gl_Position = Transform(vec4(f3OutPosition, 1.0f), mainLayout.f4x4ViewProjection);
		return;
	}

	// Look up the pre-computed Gerstner displacement and normal at the matching texel. The active LOD's
	// quad count determines the texel-vertex scaling: vertex (i, j) lives at UV (i/QX, j/QY) where
	// QX = iWaterActiveQuadX = iMeshX - 1, mirroring CreateVisibleAreaMesh's per-LOD spacing.
	ivec2 i2Grid = ivec2(round(f2InTexcoord * vec2(globalLayout.iWaterActiveQuadX, globalLayout.iWaterActiveQuadY)));
	vec3 f3Displacement = texelFetch(displacementTextureSampler,       i2Grid, 0).xyz;
	vec3 f3WaveNormal   = texelFetch(displacementNormalTextureSampler, i2Grid, 0).xyz;

	f3OutPosition = vec3(f2OutInitialPosition + f3Displacement.xy, f3Displacement.z);
	f3OutNormal = f3WaveNormal; // pre-blended toward (0,0,1) in compute via fWaterWaveNormalBlend

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
