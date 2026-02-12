#version 460

#extension GL_ARB_separate_shader_objects : require
#extension GL_EXT_shader_explicit_arithmetic_types : require

#include "ShaderLayouts.h"
#include "ShaderFunctions.h"

#include "ModelCommon.h"

void main()
{
	ModelLayout model = pModels[gl_InstanceIndex];

	// Static models: use vertex position directly, no mesh matrix transform needed
	ModelVertexOutput(f3InPosition, f3InNormal, model);
}
