#version 460

#extension GL_ARB_separate_shader_objects : require
#extension GL_EXT_shader_explicit_arithmetic_types : require

#include "ShaderLayouts.h"
#include "ShaderFunctions.h"

#include "GltfCommon.h"

void main()
{
	GltfLayout gltf = pGltfs[gl_InstanceIndex];
	int baseIndex = gl_InstanceIndex * 128;

	mat4 skinMatrix =
		f4Weight0.x * pJointMatrices[baseIndex + int(f4Joint0.x)] +
		f4Weight0.y * pJointMatrices[baseIndex + int(f4Joint0.y)] +
		f4Weight0.z * pJointMatrices[baseIndex + int(f4Joint0.z)] +
		f4Weight0.w * pJointMatrices[baseIndex + int(f4Joint0.w)];

	vec3 f3LocalPosition = (skinMatrix * vec4(f3InPosition, 1.0f)).xyz;
	vec3 f3LocalNormal = mat3(skinMatrix) * f3InNormal;

	GltfVertexOutput(f3LocalPosition, f3LocalNormal, gltf);
}
