#version 460

#extension GL_ARB_separate_shader_objects : require
#extension GL_EXT_shader_explicit_arithmetic_types : require

#include "ShaderLayouts.h"
#include "ShaderFunctions.h"

// Vertex inputs
layout (location = 0) in vec3 f3InPosition;
layout (location = 1) in vec3 f3InNormal;
layout (location = 2) in vec2 f2InUV;
layout (location = 3) in vec2 f2InUV1;
layout (location = 4) in vec2 f2InUV2;
layout (location = 5) in vec2 f2InUV3;
layout (location = 6) in vec2 f2InUV4;
layout (location = 7) in float fJoint;
layout (location = 8) in vec4 f4Joint0;
layout (location = 9) in vec4 f4Weight0;

// Vertex outputs
layout (location = 0) out vec3 f3OutWorldPosition;
layout (location = 1) out vec3 f3OutNormal;
layout (location = 2) out vec2 f2OutUV;
layout (location = 3) out vec2 f2OutUV1;
layout (location = 4) out vec2 f2OutUV2;
layout (location = 5) out vec2 f2OutUV3;
layout (location = 6) out vec2 f2OutUV4;
layout (location = 7) out vec4 f4OutColorAdd;

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

layout (binding = 1) uniform mainUniform
{
	MainLayout mainLayout;
};

layout (std430, binding = 2) buffer readonly gltfsUniform
{
	GltfLayout pGltfs[];
};

layout (std430, binding = 15) buffer readonly jointMatricesBuffer
{
	mat4 pJointMatrices[];  // Indexed: pJointMatrices[gl_InstanceIndex * 128 + jointIndex]
};

void main()
{
	GltfLayout gltf = pGltfs[gl_InstanceIndex];

	vec3 f3LocalPosition = f3InPosition;
	vec3 f3LocalNormal = f3InNormal;

	int baseIndex = gl_InstanceIndex * 128;
	mat4 skinMatrix =
		f4Weight0.x * pJointMatrices[baseIndex + int(f4Joint0.x)] +
		f4Weight0.y * pJointMatrices[baseIndex + int(f4Joint0.y)] +
		f4Weight0.z * pJointMatrices[baseIndex + int(f4Joint0.z)] +
		f4Weight0.w * pJointMatrices[baseIndex + int(f4Joint0.w)];
	f3LocalPosition = (skinMatrix * vec4(f3InPosition, 1.0f)).xyz;
	f3LocalNormal = mat3(skinMatrix) * f3InNormal;

	vec3 f3WorldPosition = Transform(vec4(f3LocalPosition, 1.0f), gltf.f3x4Transform);
	vec3 f3WorldNormal = normalize(Transform(vec4(f3LocalNormal, 0.0f), gltf.f3x4TransformNormal));

	f3OutWorldPosition = f3WorldPosition;
	f3OutNormal = f3WorldNormal;
	f2OutUV = f2InUV;
	f2OutUV1 = f2InUV1;
	f2OutUV2 = f2InUV2;
	f2OutUV3 = f2InUV3;
	f2OutUV4 = f2InUV4;
	f4OutColorAdd = gltf.f4ColorAdd;

	int iRenderingMode = int(pushConstantsLayout.f4Pipeline.y);
	if (iRenderingMode == 0)
	{
		// Mode 0: Camera rendering
		gl_Position = Transform(vec4(f3WorldPosition, 1.0f), mainLayout.f4x4ViewProjection);
	}
	else if (iRenderingMode == 1)
	{
		// Mode 1: Visible area projection
		vec2 f2VisibleAreaUV = WorldToVisibleArea(f3WorldPosition, globalLayout.f4VisibleArea);
		gl_Position = vec4(2.0f * f2VisibleAreaUV.x - 1.0f, 1.0f - 2.0f * f2VisibleAreaUV.y, 0.0f, 1.0f);
	}
	else
	{
		// Mode 2: Shadow projection
		float fSunriseOffset = globalLayout.f4ShadowFour.x;
		float fSunsetOffset = globalLayout.f4ShadowFour.y;

		// Cubic falloff for softer shadow transition
		float fSunriseOffsetCubed = fSunriseOffset * fSunriseOffset * fSunriseOffset;
		float fSunsetOffsetCubed = fSunsetOffset * fSunsetOffset * fSunsetOffset;
		float fShadowOffset = fSunriseOffsetCubed + fSunsetOffsetCubed;

		// Translation: shift entire shadow opposite to sun direction
		vec2 f2ShadowDirection = -globalLayout.f4SunNormal.xy;
		vec2 f2Translation = fShadowOffset * f2ShadowDirection;

		// Differential stretch: vertices further from object center stretch more
		float fSunriseDiff = max(0.0f, gltf.f4Position.x - f3WorldPosition.x);
		float fSunsetDiff = max(0.0f, f3WorldPosition.x - gltf.f4Position.x);
		float fStretchX = -(0.5f + fSunriseDiff) * fSunriseOffsetCubed + (0.5f + fSunsetDiff) * fSunsetOffsetCubed;

		vec3 f3ShadowPosition = vec3(
			f3WorldPosition.x + f2Translation.x + fStretchX,
			f3WorldPosition.y + f2Translation.y,
			f3WorldPosition.z
		);

		vec2 f2VisibleAreaUV = WorldToVisibleArea(f3ShadowPosition, globalLayout.f4VisibleArea);
		gl_Position = vec4(2.0f * f2VisibleAreaUV.x - 1.0f, 1.0f - 2.0f * f2VisibleAreaUV.y, 0.0f, 1.0f);
	}
}
