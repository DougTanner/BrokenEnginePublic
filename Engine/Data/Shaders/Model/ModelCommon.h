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
layout (set = 0, binding = kiGlobalBindingGlobalUniform) uniform globalUniform
{
	GlobalLayout globalLayout;
};

layout (set = 0, binding = kiGlobalBindingMainUniform) uniform mainUniform
{
	MainLayout mainLayout;
};

layout (scalar, set = 1, binding = 2) buffer readonly modelsUniform
{
	ModelLayout pModels[];
};

// Per-mesh shader data (small struct without embedded joints)
// Joint matrices stored separately so MeshData stays small and fixed-size with no embedded joint cap
struct MeshData
{
	mat4 matrix;              // Mesh world matrix
	vec4 normalMatrix[3];     // Normal matrix: transpose(inverse(mat3(matrix))), stored as 3 vec4s
	uint jointCount;          // 0 for non-skinned meshes
	uint jointMatrixOffset;   // Index into jointMatrices[] buffer
};

// Extract mat3 normal matrix from the 3 vec4 storage format
mat3 GetNormalMatrix(MeshData data)
{
	return mat3(data.normalMatrix[0].xyz, data.normalMatrix[1].xyz, data.normalMatrix[2].xyz);
}

// Binding 15: Per-mesh data (small, fixed size per mesh)
layout (scalar, set = 1, binding = kiModelBindingMeshData) buffer readonly meshDataBuffer
{
	MeshData meshData[];
};

// Joint matrix: 3 vec4s (48 bytes) instead of full mat4 (64 bytes)
// rows[i].xyz = rotation row i, rows[i].w = translation component (Tx, Ty, Tz)
struct JointMatrix
{
	vec4 rows[3];
};

// Binding 16: Joint matrices (dynamically sized, separate from mesh data)
layout (scalar, set = 1, binding = kiModelBindingJointMatrix) buffer readonly jointMatrixBuffer
{
	JointMatrix jointMatrices[];
};

void ModelVertexOutput(vec3 f3LocalPosition, vec3 f3LocalNormal, ModelLayout model)
{
	vec3 f3WorldPosition = Transform(vec4(f3LocalPosition, 1.0f), model.f3x4Transform);
	vec3 f3WorldNormal = normalize(Transform(vec4(f3LocalNormal, 0.0f), model.f3x4TransformNormal));

	// IMPORTANT: Do not Y-axis flip here

	f3OutWorldPosition = f3WorldPosition;
	f3OutNormal = f3WorldNormal;
	f2OutUV = f2InUV;
	f2OutUV1 = f2InUV1;
	f2OutUV2 = f2InUV2;
	f2OutUV3 = f2InUV3;
	f2OutUV4 = f2InUV4;
	f4OutColorAdd = model.f4ColorAdd;

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
		// Mode 2: Shadow projection. Grow each object's lateral XY footprint around its
		// own center so small ground units stay readable from the top-down RTS camera.
		vec3 f3GrownWorldPosition = f3WorldPosition;
		vec2 f2Lateral = f3WorldPosition.xy - model.f4Position.xy;
		f2Lateral *= (1.0f + globalLayout.fObjectShadowsGrow);
		f3GrownWorldPosition.xy = model.f4Position.xy + f2Lateral;
		gl_Position = ShadowStretchProjection(globalLayout, f3GrownWorldPosition, model.f4Position.xyz);
	}
}
