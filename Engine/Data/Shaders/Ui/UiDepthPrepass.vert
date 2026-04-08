#version 460
#extension GL_EXT_scalar_block_layout : require

layout(scalar) buffer;

layout(set = 1, binding = 0) readonly buffer UiRects { vec4 rects[]; };

void main()
{
	vec4 rect = rects[gl_InstanceIndex];

	vec2 positions[6] = vec2[](
		rect.xy, vec2(rect.z, rect.y), rect.zw,
		rect.xy, rect.zw, vec2(rect.x, rect.w)
	);

	gl_Position = vec4(positions[gl_VertexIndex], 0.0, 1.0);
}
