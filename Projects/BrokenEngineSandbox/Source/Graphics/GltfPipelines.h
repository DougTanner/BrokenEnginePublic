#pragma once

#include "Graphics/Objects/GltfPipeline.h"

namespace game
{

enum GltfPipelinesEnum
{
	kGltfPipelinePlayerMissiles,
	kGltfPipelinePlayerMissilesShadow,

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

	void CreateGltfPipelineShadows();
	void CreateGltfPipelines();

	void RecordGltfPipelines(int64_t iCommandBuffer, VkCommandBuffer vkCommandBuffer);

	engine::GltfPipeline mpGltfPipelines[kGltfPipelineCount];
};

inline GltfPipelines* gpGltfPipelines = nullptr;

} // namespace game
