#pragma once

#include "Graphics/Objects/GltfPipeline.h"

namespace game
{

enum GltfPipelinesEnum
{
	kGltfPipelinePlayerMissiles,
	kGltfPipelinePlayerMissilesShadow,
	kGltfPipelinePlayer,
	kGltfPipelinePlayerShadow,
	kGltfPipelineSpaceships,
	kGltfPipelineSpaceshipsShadow,

#if defined(ENABLE_GLTF_TEST)
	kGltfPipelineTest,
#endif

	kGltfPipelineCount
};

class GltfPipelines
{
public:

	GltfPipelines();
	virtual ~GltfPipelines();

	void CreateGltfShadowPipelines();
	void CreateGltfPipelines();

	void RecordGltfShadowPipelines(int64_t iCommandBuffer, VkCommandBuffer vkCommandBuffer);
	void RecordGltfPipelines(int64_t iCommandBuffer, VkCommandBuffer vkCommandBuffer);

	engine::GltfPipeline mpGltfPipelines[kGltfPipelineCount];
};

inline GltfPipelines* gpGltfPipelines = nullptr;

} // namespace game
