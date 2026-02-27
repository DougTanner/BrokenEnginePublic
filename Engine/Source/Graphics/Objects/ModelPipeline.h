#pragma once

namespace engine
{

struct PipelineInfo;

enum class ModelDrawPass : uint8_t { kAll, kOpaque, kTransparent };

class ModelPipeline
{
public:

	ModelPipeline() = default;
	~ModelPipeline() = default;

	void Create(common::crc_t sceneCrc, const PipelineInfo& rPipelineInfo, bool bAddModelDescriptors = true, bool bIsShadow = false);
	void RecordDrawIndirect(int64_t iCommandBuffer, VkCommandBuffer vkCommandBuffer, const XMFLOAT4& rf4PushConstants = {}, ModelDrawPass ePass = ModelDrawPass::kAll);
	void WriteIndirectBuffer(int64_t iCommandBuffer, int64_t iCount);
	void UpdateStorageBufferDescriptors(int64_t iFramebuffer, int64_t iBinding, Buffer* pBuffer);

	int64_t miMaterialCount = 0;
	Pipeline mpPipelines[common::SceneHeader::kiMaxMaterials] {};
	int64_t mpiIndexCounts[common::SceneHeader::kiMaxMaterials] {};
	int64_t mpiFirstIndices[common::SceneHeader::kiMaxMaterials] {};
	common::crc_t mSceneCrc = 0;
	bool mbTexturesRequested = false;
	bool mpbTransparentMaterials[common::SceneHeader::kiMaxMaterials] {};
	bool mbHasTransparentMaterials = false;
};

} // namespace engine
