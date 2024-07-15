// Based on https://github.com/SaschaWillems/Vulkan-glTF-PBR

#version 460

#include "ShaderLayouts.h"
#include "ShaderFunctions.h"

// Push constants
layout(push_constant) uniform pushConstants
{
	PushConstantsLayout pushConstantsLayout;
};

// Uniforms
layout (binding = 2) buffer readonly gltfsUniform
{
	GltfLayout pGltfs[];
};

layout (binding = 3) uniform sampler2D colorMap;
layout (binding = 4) uniform sampler2D physicalDescriptorMap;
layout (binding = 5) uniform sampler2D normalMap;
layout (binding = 6) uniform sampler2D aoMap;
layout (binding = 7) uniform sampler2D emissiveMap;

layout (binding = 8) uniform samplerCube samplerIrradiance;
layout (binding = 9) uniform samplerCube prefilteredMap;
layout (binding = 10) uniform sampler2D samplerBRDFLUT;

layout (binding = 11) buffer readonly gltfMaterialsUniform
{
	GltfMaterialLayout pMaterials[];
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
	GltfMaterialLayout material = pMaterials[int32_t(pushConstantsLayout.f4Pipeline.w)];
	if (material.iEmissiveTextureSet > -1)
	{
		emissive = texture(emissiveMap, f2InUV).rgb;
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
