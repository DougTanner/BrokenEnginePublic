#pragma once

#include "Pipeline.h"

namespace engine
{

struct PipelineInfo;

class GltfPipeline
{
public:

	GltfPipeline() = default;
	~GltfPipeline() = default;

	void Create(common::crc_t gltfCrc, const PipelineInfo& rPipelineInfo, bool bAddGltfDescriptors = true);
	void RecordDrawIndirect(int64_t iCommandBuffer, VkCommandBuffer vkCommandBuffer, const DirectX::XMFLOAT4& rf4PushConstants = {});
	void WriteIndirectBuffer(int64_t iCommandBuffer, int64_t iCount);

	int64_t miMaterialCount = 0;
	Pipeline mpPipelines[common::GltfHeader::kiMaxMaterials] {};
	int64_t mpiIndexCounts[common::GltfHeader::kiMaxMaterials] {};
	int64_t mpiFirstIndices[common::GltfHeader::kiMaxMaterials] {};
};

} // namespace engine
