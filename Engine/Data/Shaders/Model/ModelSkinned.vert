#version 460

#include "ShaderLayouts.h"
#include "ShaderFunctions.h"

#include "ModelCommon.h"

void main()
{
	ModelLayout model = pModels[gl_InstanceIndex];
	int materialIndex = int(pushConstantsLayout.f4Pipeline.w);
	int meshIndex = int(model.uiMeshDataBase) + materialIndex;
	MeshData data = meshData[meshIndex];

	vec3 f3LocalPosition;
	vec3 f3LocalNormal;

	if (data.jointCount > 0)
	{
		// Use offset-based indexing into separate joint matrix buffer
		uint baseOffset = data.jointMatrixOffset;
		JointMatrix jm0 = jointMatrices[baseOffset + int(f4Joint0.x)];
		JointMatrix jm1 = jointMatrices[baseOffset + int(f4Joint0.y)];
		JointMatrix jm2 = jointMatrices[baseOffset + int(f4Joint0.z)];
		JointMatrix jm3 = jointMatrices[baseOffset + int(f4Joint0.w)];

		// Weighted blend of 3-row matrices, then reconstruct mat4
		// Each row stores rotation in .xyz and translation component in .w
		vec4 row0 = f4Weight0.x * jm0.rows[0] + f4Weight0.y * jm1.rows[0] + f4Weight0.z * jm2.rows[0] + f4Weight0.w * jm3.rows[0];
		vec4 row1 = f4Weight0.x * jm0.rows[1] + f4Weight0.y * jm1.rows[1] + f4Weight0.z * jm2.rows[1] + f4Weight0.w * jm3.rows[1];
		vec4 row2 = f4Weight0.x * jm0.rows[2] + f4Weight0.y * jm1.rows[2] + f4Weight0.z * jm2.rows[2] + f4Weight0.w * jm3.rows[2];

		// Unpack: rotation in .xyz (w was 0, now holds translation), translation from .w components
		mat4 skinMatrix = mat4(vec4(row0.xyz, 0.0), vec4(row1.xyz, 0.0), vec4(row2.xyz, 0.0), vec4(row0.w, row1.w, row2.w, 1.0));
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

	ModelVertexOutput(f3LocalPosition, f3LocalNormal, model);
}
