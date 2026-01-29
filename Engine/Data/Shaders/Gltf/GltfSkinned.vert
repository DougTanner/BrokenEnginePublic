#version 460

#extension GL_ARB_separate_shader_objects : require
#extension GL_EXT_shader_explicit_arithmetic_types : require

#include "ShaderLayouts.h"
#include "ShaderFunctions.h"

#include "GltfCommon.h"

void main()
{
	// Get GltfLayout for this instance
	GltfLayout gltf = pGltfs[gl_InstanceIndex];

	// Calculate mesh data index: base offset + material index from push constants
	// This allows each material's draw call to access its own mesh transform
	int materialIndex = int(pushConstantsLayout.f4Pipeline.w);
	int meshIndex = int(gltf.uiMeshDataBase) + materialIndex;

	// Get mesh shader data for this material
	MeshShaderData data = meshData[meshIndex];

	vec3 f3LocalPosition;
	vec3 f3LocalNormal;

	if (data.jointCount > 0)
	{
		// Skinned mesh: compute weighted blend of joint matrices
		mat4 skinMatrix =
			f4Weight0.x * data.jointMatrix[int(f4Joint0.x)] +
			f4Weight0.y * data.jointMatrix[int(f4Joint0.y)] +
			f4Weight0.z * data.jointMatrix[int(f4Joint0.z)] +
			f4Weight0.w * data.jointMatrix[int(f4Joint0.w)];

		// Apply skinning then mesh matrix
		// Position: mesh * skin * vertex (model transform applied in GltfVertexOutput)
		vec4 skinnedPos = skinMatrix * vec4(f3InPosition, 1.0f);
		f3LocalPosition = (data.matrix * skinnedPos).xyz;

		// Transform normal: mesh * skin (model transform applied in GltfVertexOutput via f3x4TransformNormal)
		mat3 normalMatrix = transpose(inverse(mat3(data.matrix * skinMatrix)));
		f3LocalNormal = normalize(normalMatrix * f3InNormal);
	}
	else
	{
		// Non-skinned mesh: use mesh matrix directly
		// Position: mesh * vertex (model transform applied in GltfVertexOutput)
		f3LocalPosition = (data.matrix * vec4(f3InPosition, 1.0f)).xyz;

		// Transform normal: mesh only (model transform applied in GltfVertexOutput via f3x4TransformNormal)
		mat3 normalMatrix = transpose(inverse(mat3(data.matrix)));
		f3LocalNormal = normalize(normalMatrix * f3InNormal);
	}

	GltfVertexOutput(f3LocalPosition, f3LocalNormal, gltf);
}
