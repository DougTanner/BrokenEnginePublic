// Based on https://github.com/SaschaWillems/Vulkan-glTF-PBR

/* Copyright (c) 2018-2023, Sascha Willems
 *
 * SPDX-License-Identifier: MIT
 *
 */
 
 #version 450

#include "ShaderLayouts.h"
#include "ShaderFunctions.h"

layout (location = 0) in vec3 inPos;
layout (location = 1) in vec3 f3InNormal;
layout (location = 2) in vec2 f2InUV;
layout (location = 3) in float fJoint;

layout(push_constant) uniform PushConsts
{
	vec4 pf4PushConstants[4];
};

layout (location = 0) out vec3 outUVW;

out gl_PerVertex {
	vec4 gl_Position;
};

void main() 
{
	outUVW = inPos;
	gl_Position = Transform(vec4(inPos.xyz, 1.0f), pf4PushConstants);
	gl_Position.y *= -1.0f;
}
