// Based on https://github.com/SaschaWillems/Vulkan-glTF-PBR

/* Copyright (c) 2018-2023, Sascha Willems
 *
 * SPDX-License-Identifier: MIT
 *
 */
 
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

layout (binding = 1) uniform mainUniform
{
	MainLayout mainLayout;
};

layout (binding = 2) buffer readonly gltfsUniform
{
	GltfLayout pGltfs[];
};

// Input
layout (location = 0) in vec3 f3InPosition;
layout (location = 1) in vec3 f3InNormal;
layout (location = 2) in vec2 f2InUV;
layout (location = 3) in float fJoint;

// Output
layout (location = 0) out vec3 f3OutWorldPosition;
layout (location = 1) out vec3 f3OutNormal;
layout (location = 2) out vec2 f2OutUV;
layout (location = 3) out vec4 f4OutColorAdd;

#if defined(GLTF_ANIMATION)
#define MAX_NUM_JOINTS 128

layout (set = 2, binding = 0) uniform UBONode
{
	mat4 matrix;
	mat4 jointMatrix[MAX_NUM_JOINTS];
	float jointCount;
} node;
#endif

void main() 
{
	f3OutWorldPosition = Transform(vec4(f3InPosition, 1.0f), pGltfs[gl_InstanceIndex].f3x4Transform);
	f3OutNormal = normalize(Transform(vec4(f3InNormal, 0.0f), pGltfs[gl_InstanceIndex].f3x4TransformNormal));
	f2OutUV = f2InUV;
	f4OutColorAdd = pGltfs[gl_InstanceIndex].f4ColorAdd;

	if (int(pushConstantsLayout.f4Pipeline.y) == 0)
	{
		gl_Position = Transform(vec4(f3OutWorldPosition, 1.0f), mainLayout.f4x4ViewProjection);
	}
	else if (int(pushConstantsLayout.f4Pipeline.y) == 1)
	{
		vec2 f2VisibleAreaPosition = WorldToVisibleArea(f3OutWorldPosition, globalLayout.f4VisibleArea);
		gl_Position = vec4(vec2(-1.0f + 2.0f * f2VisibleAreaPosition.x, 1.0f - 2.0f * f2VisibleAreaPosition.y), 0.0f, 1.0f);
	}
	else
	{
		float fSunriseDiff = max(0.0f, pGltfs[gl_InstanceIndex].f4Position.x - f3OutWorldPosition.x);
		f3OutWorldPosition.x -= (0.5f + fSunriseDiff) * globalLayout.f4ShadowFour.x * globalLayout.f4ShadowFour.x * globalLayout.f4ShadowFour.x;

		float fSunsetDiff = max(0.0f, f3OutWorldPosition.x - pGltfs[gl_InstanceIndex].f4Position.x);
		f3OutWorldPosition.x += (0.5f + fSunsetDiff) * globalLayout.f4ShadowFour.y * globalLayout.f4ShadowFour.y * globalLayout.f4ShadowFour.y;
		
		vec2 f2VisibleAreaPosition = WorldToVisibleArea(f3OutWorldPosition, globalLayout.f4VisibleArea);
		gl_Position = vec4(vec2(-1.0f + 2.0f * f2VisibleAreaPosition.x, 1.0f - 2.0f * f2VisibleAreaPosition.y), 0.0f, 1.0f);
	}

#if defined(GLTF_ANIMATION)
	vec4 locPos;
	if (node.jointCount > 0.0) {
		// Mesh is skinned
		mat4 skinMat = 
			inWeight0.x * node.jointMatrix[int(inJoint0.x)] +
			inWeight0.y * node.jointMatrix[int(inJoint0.y)] +
			inWeight0.z * node.jointMatrix[int(inJoint0.z)] +
			inWeight0.w * node.jointMatrix[int(inJoint0.w)];

		locPos = ubo.model * node.matrix * skinMat * vec4(inPos, 1.0);
		outNormal = normalize(transpose(inverse(mat3(ubo.model * node.matrix * skinMat))) * inNormal);
	} else {
		locPos = ubo.model * node.matrix * vec4(inPos, 1.0);
		outNormal = normalize(transpose(inverse(mat3(ubo.model * node.matrix))) * inNormal);
	}
	locPos.y = -locPos.y;
	outWorldPos = locPos.xyz / locPos.w;
	gl_Position =  ubo.projection * ubo.view * vec4(outWorldPos, 1.0);
#endif
}
