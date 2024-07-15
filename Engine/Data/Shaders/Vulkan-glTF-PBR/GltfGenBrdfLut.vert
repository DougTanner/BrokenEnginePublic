// Based on https://github.com/SaschaWillems/Vulkan-glTF-PBR

/* Copyright (c) 2018-2023, Sascha Willems
 *
 * SPDX-License-Identifier: MIT
 *
 */
 
 #version 460

// Input
layout (location = 0) in vec2 f2InQuadVertex;

// Output
layout (location = 0) out flat int iOutInstanceIndex;
layout (location = 1) out vec2 f2OutTexcoord;

void main()
{
	iOutInstanceIndex = gl_InstanceIndex;

	f2OutTexcoord = f2InQuadVertex;

	gl_Position = vec4(-1.0f + 2.0f * f2InQuadVertex.x, 1.0f - 2.0f * f2InQuadVertex.y, 0.0f, 1.0f);
}
