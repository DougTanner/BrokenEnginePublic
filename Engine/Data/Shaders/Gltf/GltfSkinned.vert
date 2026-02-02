#version 460

#extension GL_ARB_separate_shader_objects : require
#extension GL_EXT_shader_explicit_arithmetic_types : require

#include "ShaderLayouts.h"
#include "ShaderFunctions.h"

#include "GltfCommon.h"

void main()
{
	GltfLayout gltf = pGltfs[gl_InstanceIndex];
	int materialIndex = int(pushConstantsLayout.f4Pipeline.w);
	int meshIndex = int(gltf.uiMeshDataBase) + materialIndex;
	MeshData data = meshData[meshIndex];

	vec3 f3LocalPosition;
	vec3 f3LocalNormal;

	if (data.jointCount > 0)
	{
		// Use offset-based indexing into separate joint matrix buffer
		uint baseOffset = data.jointMatrixOffset;
		mat4 skinMatrix = f4Weight0.x * jointMatrices[baseOffset + int(f4Joint0.x)] + f4Weight0.y * jointMatrices[baseOffset + int(f4Joint0.y)] + f4Weight0.z * jointMatrices[baseOffset + int(f4Joint0.z)] + f4Weight0.w * jointMatrices[baseOffset + int(f4Joint0.w)];
		vec4 skinnedPos = skinMatrix * vec4(f3InPosition, 1.0f);
		f3LocalPosition = (data.matrix * skinnedPos).xyz;
		// Use precomputed mesh normal matrix with skin approximation (rigid transforms)
		mat3 normalMatrix = mat3(skinMatrix) * GetNormalMatrix(data);
		f3LocalNormal = normalize(normalMatrix * f3InNormal);
	}
	else
	{
		f3LocalPosition = (data.matrix * vec4(f3InPosition, 1.0f)).xyz;
		// Use precomputed normal matrix directly
		f3LocalNormal = normalize(GetNormalMatrix(data) * f3InNormal);
	}

	GltfVertexOutput(f3LocalPosition, f3LocalNormal, gltf);
}
