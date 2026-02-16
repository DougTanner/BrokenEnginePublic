#version 460

#extension GL_EXT_nonuniform_qualifier : require

#include "ShaderLayouts.h"
#include "ShaderFunctions.h"

// Push constants
layout(push_constant) uniform pushConstants
{
	PushConstantsLayout pushConstantsLayout;
};

// Uniforms
layout (scalar, binding = 2) buffer readonly modelsUniform
{
	ModelLayout pModels[];
};

// Bindless texture array
layout (binding = 3) uniform sampler samplerRepeat;
layout (binding = 4) uniform texture2D pTextures[];

// IBL textures
layout (binding = 5) uniform samplerCube samplerIrradiance;
layout (binding = 6) uniform samplerCube prefilteredMap;
layout (binding = 7) uniform sampler2D samplerBRDFLUT;

layout (scalar, binding = 8) buffer readonly pbrMaterialsUniform
{
	PbrMaterialLayout pMaterials[];
};

// Input
layout (location = 0) in flat int iInInstanceIndex;
layout (location = 1) in vec3 f3InWorldPosition;
layout (location = 2) in vec3 f3InNormal;
layout (location = 3) in vec2 f2InUV;

// Output
layout (location = 0) out vec4 f4OutColor;

void main()
{
	int i = iInInstanceIndex;

	vec3 emissive = vec3(0.0f, 0.0f, 0.0f);
	PbrMaterialLayout material = pMaterials[int32_t(pushConstantsLayout.f4Pipeline.w)];
	if (material.iEmissiveTextureSet > -1)
	{
		emissive = texture(sampler2D(pTextures[nonuniformEXT(int(material.fEmissiveTextureIndex))], samplerRepeat), f2InUV).rgb;
	}

	float fColor = 0.0f;
	if (int(pushConstantsLayout.f4Pipeline.z) == 0)
	{
		fColor = emissive.r;
	}
	else if (int(pushConstantsLayout.f4Pipeline.z) == 1)
	{
		fColor = emissive.g;
	}
	else if (int(pushConstantsLayout.f4Pipeline.z) == 2)
	{
		fColor = emissive.b;
	}

	// Lighting direction
	vec2 f2Direction = normalize(f3InNormal.xy);
	vec4 f4Direction = vec4(f2Direction.x > 0.0f ? f2Direction.x : 0.0f, f2Direction.x < 0.0f ? -f2Direction.x : 0.0f, f2Direction.y > 0.0f ? f2Direction.y : 0.0f, f2Direction.y < 0.0f ? -f2Direction.y : 0.0f);

    f4OutColor = 500.0f * fColor * f4Direction;
}
