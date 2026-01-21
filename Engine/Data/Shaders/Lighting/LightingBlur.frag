#version 460

#include "ShaderLayouts.h"
#include "ShaderFunctions.h"

// Push constants
layout(push_constant) uniform pushConstants
{
	PushConstantsLayout pushConstantsLayout;
};

// Uniforms
layout (binding = 0) uniform globalUniform
{
	GlobalLayout globalLayout;
};

layout (binding = 1) uniform sampler2D textureSampler;

// Input
layout (location = 0) in flat int iInInstanceIndex;
layout (location = 1) in vec2 f2InTexcoord;

// Output
layout (location = 0) out vec4 f4OutColor;

void main()
{
	float fAspectRatioY = 1.0f / (globalLayout.f4VisibleArea.z - globalLayout.f4VisibleArea.x);
	float fAspectRatioX = fAspectRatioY;

	const float fDirectionality = globalLayout.f4LightingThree.x;
	const float fBlurDistance = globalLayout.f4LightingTwo.x;
	const float fJitterX = globalLayout.f4LightingThree.z / pushConstantsLayout.f4Pipeline.x;
	const float fJitterY = globalLayout.f4LightingThree.z / pushConstantsLayout.f4Pipeline.y;
	const float fGrowOne = 1.0f * fBlurDistance;
	const float fGrowOneOne = 0.7071f * fBlurDistance;
	const float fGrowTwo = 2.0f * fGrowOne;
	const float fGrowTwoTwo = 2.0f * fGrowOneOne;
	const float fGrowTwoOne = 0.5f * (fGrowTwo + fGrowTwoTwo);

	// X is east, Y is west, Z is north, W is south
	f4OutColor = vec4(0.0f, 0.0f, 0.0f, 0.0f);

#if 1
	const float fDirectionCount = 20.0f;
	const float fDirectionStep = (2.0f * fPi) / fDirectionCount;
	const uint32_t uiDirectionCount = uint32_t(fDirectionCount);
	const uint32_t uiDistanceCount = 4;

	const float kfDivisor = 1.0f / 4294967295.0f;
	uint32_t uiW = uint32_t(f2InTexcoord.x * 4294967295.0f);
	uint32_t uiZ = uint32_t(f2InTexcoord.y * 4294967295.0f);

	float fCurrentAngle = 0.25f * fDirectionStep;
	for (uint32_t i = 0; i < uiDirectionCount; ++i, fCurrentAngle += fDirectionStep)
	{
		vec2 f2Direction = normalize(Rotate(vec2(1.0f, 0.0f), fCurrentAngle));

		for (uint32_t j = 0; j < uiDistanceCount; ++j)
		{
			float fDistance = float(1 + uiDistanceCount);

			uiW = 18000 * (uiW & 65535) + (uiW >> 16);
			uiZ = 36969 * (uiZ & 65535) + (uiZ >> 16);
			float fRandomOne = fDistance * fJitterX * (-1.0f + 2.0f * float((uiZ << 16) + uiW) * kfDivisor);
			uiW = 18000 * (uiW & 65535) + (uiW >> 16);
			uiZ = 36969 * (uiZ & 65535) + (uiZ >> 16);
			float fRandomTwo = fDistance * fJitterY * (-1.0f + 2.0f * float((uiZ << 16) + uiW) * kfDivisor);

			vec4 f4Color = texture(textureSampler, f2InTexcoord + vec2(fRandomOne, fRandomTwo) + vec2(fAspectRatioX, fAspectRatioY) * (fBlurDistance * float(j + 1) * f2Direction));
			vec4 f4ColorDirectional = vec4(max(dot(f2Direction, vec2(-1.0f,  0.0f)), 0.0f) * f4Color.x,
			                               max(dot(f2Direction, vec2( 1.0f,  0.0f)), 0.0f) * f4Color.y,
										   max(dot(f2Direction, vec2( 0.0f,  1.0f)), 0.0f) * f4Color.z,
										   max(dot(f2Direction, vec2( 0.0f, -1.0f)), 0.0f) * f4Color.w);
			f4OutColor += mix(f4Color, f4ColorDirectional, fDirectionality) / pow(fDistance, 0.5f);
		}
	}
	f4OutColor /= fDirectionCount * float(uiDistanceCount) * mix(1.0f, 0.32f, fDirectionality);

	f4OutColor /= pushConstantsLayout.f4Pipeline.z;
#else
	f4OutColor += vec4(1.0f, fDirectionality, fDirectionality, 1.0f) * texture(textureSampler, f2InTexcoord + fGrowTwoTwo * vec2(fAspectRatioX * -2.0f, fAspectRatioY * -2.0f) + vec2(0.0f, fAspectRatioY * 2.0f * fJitter));
	f4OutColor += vec4(1.0f, fDirectionality, fDirectionality, 1.0f) * texture(textureSampler, f2InTexcoord + fGrowTwoOne * vec2(fAspectRatioX * -1.0f, fAspectRatioY * -2.0f));
	f4OutColor += vec4(1.0f, 1.0f, fDirectionality, 1.0f)            * texture(textureSampler, f2InTexcoord + fGrowTwo    * vec2(fAspectRatioX *  0.0f, fAspectRatioY * -2.0f));
	f4OutColor += vec4(fDirectionality, 1.0f, fDirectionality, 1.0f) * texture(textureSampler, f2InTexcoord + fGrowTwoOne * vec2(fAspectRatioX *  1.0f, fAspectRatioY * -2.0f));
	f4OutColor += vec4(fDirectionality, 1.0f, fDirectionality, 1.0f) * texture(textureSampler, f2InTexcoord + fGrowTwoTwo * vec2(fAspectRatioX *  2.0f, fAspectRatioY * -2.0f) + vec2(fAspectRatioX * 2.0f * fJitter, 0.0f));

	f4OutColor += vec4(1.0f, fDirectionality, fDirectionality, 1.0f) * texture(textureSampler, f2InTexcoord + fGrowTwoOne * vec2(fAspectRatioX * -2.0f, fAspectRatioY * -1.0f));
	f4OutColor += vec4(1.0f, fDirectionality, fDirectionality, 1.0f) * texture(textureSampler, f2InTexcoord + fGrowOneOne * vec2(fAspectRatioX * -1.0f, fAspectRatioY * -1.0f) + vec2(fAspectRatioX * -fJitter, 0.0f));
	f4OutColor += vec4(1.0f, 1.0f, fDirectionality, 1.0f)            * texture(textureSampler, f2InTexcoord + fGrowOne    * vec2(fAspectRatioX *  0.0f, fAspectRatioY * -1.0f));
	f4OutColor += vec4(fDirectionality, 1.0f, fDirectionality, 1.0f) * texture(textureSampler, f2InTexcoord + fGrowOneOne * vec2(fAspectRatioX *  1.0f, fAspectRatioY * -1.0f) + vec2(0.0f, fAspectRatioY * fJitter));
	f4OutColor += vec4(fDirectionality, 1.0f, fDirectionality, 1.0f) * texture(textureSampler, f2InTexcoord + fGrowTwoOne * vec2(fAspectRatioX *  2.0f, fAspectRatioY * -1.0f));

	f4OutColor += vec4(1.0f, fDirectionality, 1.0f, 1.0f)            * texture(textureSampler, f2InTexcoord + fGrowTwo    * vec2(fAspectRatioX * -2.0f, 0.0f));
	f4OutColor += vec4(1.0f, fDirectionality, 1.0f, 1.0f)            * texture(textureSampler, f2InTexcoord + fGrowOne    * vec2(fAspectRatioX * -1.0f, 0.0f));
	f4OutColor += vec4(fDirectionality, 1.0f, 1.0f, 1.0f)            * texture(textureSampler, f2InTexcoord + fGrowOne    * vec2(fAspectRatioX *  1.0f, 0.0f));
	f4OutColor += vec4(fDirectionality, 1.0f, 1.0f, 1.0f)            * texture(textureSampler, f2InTexcoord + fGrowTwo    * vec2(fAspectRatioX *  2.0f, 0.0f));

	f4OutColor += vec4(1.0f, fDirectionality, 1.0f, fDirectionality) * texture(textureSampler, f2InTexcoord + fGrowTwoOne * vec2(fAspectRatioX * -2.0f, fAspectRatioY * 1.0f));
	f4OutColor += vec4(1.0f, fDirectionality, 1.0f, fDirectionality) * texture(textureSampler, f2InTexcoord + fGrowOneOne * vec2(fAspectRatioX * -1.0f, fAspectRatioY * 1.0f) + vec2(0.0f, fAspectRatioY * -fJitter));
	f4OutColor += vec4(1.0f, 1.0f, 1.0f, fDirectionality)            * texture(textureSampler, f2InTexcoord + fGrowOne    * vec2(fAspectRatioX *  0.0f, fAspectRatioY * 1.0f));
	f4OutColor += vec4(fDirectionality, 1.0f, 1.0f, fDirectionality) * texture(textureSampler, f2InTexcoord + fGrowOneOne * vec2(fAspectRatioX *  1.0f, fAspectRatioY * 1.0f) + vec2(fAspectRatioX * fJitter, 0.0f));
	f4OutColor += vec4(fDirectionality, 1.0f, 1.0f, fDirectionality) * texture(textureSampler, f2InTexcoord + fGrowTwoOne * vec2(fAspectRatioX *  2.0f, fAspectRatioY * 1.0f));

	f4OutColor += vec4(1.0f, fDirectionality, 1.0f, fDirectionality) * texture(textureSampler, f2InTexcoord + fGrowTwoTwo * vec2(fAspectRatioX * -2.0f, fAspectRatioY * 2.0f) + vec2(fAspectRatioX * -2.0f * fJitter, 0.0f));
	f4OutColor += vec4(1.0f, fDirectionality, 1.0f, fDirectionality) * texture(textureSampler, f2InTexcoord + fGrowTwoOne * vec2(fAspectRatioX * -1.0f, fAspectRatioY * 2.0f));
	f4OutColor += vec4(1.0f, 1.0f, 1.0f, fDirectionality)            * texture(textureSampler, f2InTexcoord + fGrowTwo    * vec2(fAspectRatioX *  0.0f, fAspectRatioY * 2.0f));
	f4OutColor += vec4(fDirectionality, 1.0f, 1.0f, fDirectionality) * texture(textureSampler, f2InTexcoord + fGrowTwoOne * vec2(fAspectRatioX *  1.0f, fAspectRatioY * 2.0f));
	f4OutColor += vec4(fDirectionality, 1.0f, 1.0f, fDirectionality) * texture(textureSampler, f2InTexcoord + fGrowTwoTwo * vec2(fAspectRatioX *  2.0f, fAspectRatioY * 2.0f) + vec2(0.0f, fAspectRatioY * -2.0f * fJitter));

	f4OutColor /= (56.0f + 40.0f * fDirectionality) / (56.0f + 40.0f);

	f4OutColor /= pushConstantsLayout.f4Pipeline.z;

	f4OutColor = f4OutColor.xywz;
#endif
}
