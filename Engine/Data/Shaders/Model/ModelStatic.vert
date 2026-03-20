#version 460

#include "ShaderLayouts.h"
#include "ShaderFunctions.h"

#include "ModelCommon.h"

void main()
{
	ModelLayout model = pModels[gl_InstanceIndex];

	// Static models: use vertex position directly, no mesh matrix transform needed
	ModelVertexOutput(f3InPosition, f3InNormal, model);
}
