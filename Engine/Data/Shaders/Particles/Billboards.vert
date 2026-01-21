#version 460

#include "ShaderLayouts.h"
#include "ShaderFunctions.h"

// Uniforms
layout (binding = 0) uniform globalUniform
{
    GlobalLayout globalLayout;
};

layout (binding = 1) uniform mainUniform
{
	MainLayout mainLayout;
};

layout (binding = 2) buffer readonly billboardsUniform
{
	BillboardLayout pBillboards[];
};

// Input
layout (location = 0) in vec2 f2InQuadVertex;

// Output
layout (location = 0) out flat int32_t iOutInstanceIndex;
layout (location = 1) out vec2 f2OutTexcoord;

void main()
{
	int32_t i = int32_t(gl_InstanceIndex);
	iOutInstanceIndex = i;

	gl_Position = pBillboards[i].f4Position;

	float fSize = pBillboards[i].f4Misc.x;
	gl_Position.x += (-fSize + f2InQuadVertex.x * 2.0f * fSize) / globalLayout.f4Misc.z;
	gl_Position.y += fSize - f2InQuadVertex.y * 2.0f * fSize;

	// Rotate texcoords
	float fRotation = pBillboards[i].f4Misc.z;
	f2OutTexcoord = f2InQuadVertex;
	f2OutTexcoord -= vec2(0.5f, 0.5f);
	f2OutTexcoord = Rotate(f2OutTexcoord, fRotation);
	f2OutTexcoord += vec2(0.5f, 0.5f);
}
