#pragma once

#include "Graphics/Objects/GltfPipeline.h"

namespace game
{

#if defined(ENABLE_GLTF_TEST)
enum GltfPipelinesEnum
{
	// DT: TEMP
	kGltfPipelineTest,

	kGltfPipelineCount
};
#endif

class GltfPipelines
{
public:

	GltfPipelines();
	virtual ~GltfPipelines();

	void CreateGltfPipelineShadows();
	void CreateGltfPipelines();

	void RecordGltfPipelines(int64_t iCommandBuffer, VkCommandBuffer vkCommandBuffer);

#if defined(ENABLE_GLTF_TEST)
	engine::GltfPipeline mpGltfPipelines[kGltfPipelineCount];
#endif
};

inline GltfPipelines* gpGltfPipelines = nullptr;

} // namespace game
