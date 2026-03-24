#version 460

#extension GL_EXT_nonuniform_qualifier : require

#include "ShaderLayouts.h"
#include "ShaderFunctions.h"

// Push constants
layout(push_constant) uniform pushConstants
{
	PushConstantsLayout pushConstantsLayout;
};

// Uniforms
layout (set = 0, binding = 0) uniform globalUniform
{
    GlobalLayout globalLayout;
};

layout (scalar, set = 1, binding = 2) buffer readonly particlesUniform
{
	ParticlesLayout particles;
};

layout (set = 1, binding = 3) buffer lightOccupancyBuffer { uint occupancy[]; };

layout (set = 0, binding = 12) uniform sampler particleSampler;
layout (set = 0, binding = 4) uniform texture2D pTextures[];

// Input
layout (location = 0) in flat int iInInstanceIndex;
layout (location = 1) in vec2 f2InTexcoord;

// Output
layout (location = 0) out vec4 f4OutColorRed;
layout (location = 1) out vec4 f4OutColorGreen;
layout (location = 2) out vec4 f4OutColorBlue;

void main()
{
	int i = iInInstanceIndex;
	float fIntensity = particles.pParticles[i].fIntensity;

	vec4 f4Color = unpackUnorm4x8(particles.pParticles[i].iColor).abgr;
	float fCookie = texture(sampler2D(pTextures[nonuniformEXT(particles.pParticles[i].iCookie)], particleSampler), f2InTexcoord).x;
	f4Color = vec4(fCookie * fIntensity * f4Color.w * f4Color.xyz, 0.0f);

	// Compute all color channels simultaneously
	float fParticleIntensity = float(particles.pParticles[i].iLightingIntensity);
	f4OutColorRed = f4Color.r * vec4(0.25f, 0.25f, 0.25f, 0.25f) * fParticleIntensity;
	f4OutColorGreen = f4Color.g * vec4(0.25f, 0.25f, 0.25f, 0.25f) * fParticleIntensity;
	f4OutColorBlue = f4Color.b * vec4(0.25f, 0.25f, 0.25f, 0.25f) * fParticleIntensity;

	// Mark occupancy for deposited tile and surrounding tiles within dilation radius
	int iCenterTileX = int(gl_FragCoord.x) / int(kiComputeTileSize);
	int iCenterTileY = int(gl_FragCoord.y) / int(kiComputeTileSize);
	int iDilation = int(globalLayout.uiLightOccupancyDilation);
	for (int iDy = -iDilation; iDy <= iDilation; ++iDy)
	{
		for (int iDx = -iDilation; iDx <= iDilation; ++iDx)
		{
			int iTileX = iCenterTileX + iDx;
			int iTileY = iCenterTileY + iDy;
			if (iTileX >= 0 && iTileY >= 0 && uint(iTileX) < globalLayout.uiLightTilesX && uint(iTileY) < globalLayout.uiLightTilesY)
			{
				uint uiTileIndex = uint(iTileY) * globalLayout.uiLightTilesX + uint(iTileX);
				atomicOr(occupancy[uiTileIndex >> 5], 1u << (uiTileIndex & 31));
			}
		}
	}
}
