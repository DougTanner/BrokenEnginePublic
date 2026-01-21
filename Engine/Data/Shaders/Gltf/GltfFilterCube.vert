#version 460

vec4 Transform(vec4 f4Vec, vec4[4] f4x4Matrix)
{
	return vec4(dot(f4Vec, f4x4Matrix[0]), dot(f4Vec, f4x4Matrix[1]), dot(f4Vec, f4x4Matrix[2]), dot(f4Vec, f4x4Matrix[3]));
}

// Input
layout (location = 0) in vec3 f3InPosition;

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
