#version 460

vec4 Transform(vec4 f4Vec, vec4[4] f4x4Matrix)
{
	return vec4(dot(f4Vec, f4x4Matrix[0]), dot(f4Vec, f4x4Matrix[1]), dot(f4Vec, f4x4Matrix[2]), dot(f4Vec, f4x4Matrix[3]));
}

// Vertex inputs matching GltfVertex format (must match buffer stride even though we only use position)
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

// Output
layout (location = 0) out vec3 f3OutCubemapDirection;

// Push constants
layout(push_constant) uniform PushConsts {
	vec4 f4x4ViewProjection[4];
} pushConsts;

void main()
{
	f3OutCubemapDirection = f3InPosition;

	vec4 f4ClipPosition = Transform(vec4(f3InPosition, 1.0f), pushConsts.f4x4ViewProjection);
	f4ClipPosition.y *= -1.0f;
	gl_Position = f4ClipPosition;
}
