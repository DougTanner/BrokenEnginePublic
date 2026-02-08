#pragma once

#include "Pipeline.h"

namespace engine
{

struct PipelineInfo;

class ModelPipeline
{
public:

	ModelPipeline() = default;
	~ModelPipeline() = default;

	void Create(common::crc_t sceneCrc, const PipelineInfo& rPipelineInfo, bool bAddModelDescriptors = true);
	void RecordDrawIndirect(int64_t iCommandBuffer, VkCommandBuffer vkCommandBuffer, const XMFLOAT4& rf4PushConstants = {});
	void WriteIndirectBuffer(int64_t iCommandBuffer, int64_t iCount);
	void UpdateStorageBufferDescriptors(int64_t iFramebuffer, int64_t iBinding, Buffer* pBuffer);
	void UpdateModelTextureDescriptors();

	int64_t miMaterialCount = 0;
	Pipeline mpPipelines[common::SceneHeader::kiMaxMaterials] {};
	int64_t mpiIndexCounts[common::SceneHeader::kiMaxMaterials] {};
	int64_t mpiFirstIndices[common::SceneHeader::kiMaxMaterials] {};
};

} // namespace engine
