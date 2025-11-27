#pragma once

#include "Pipeline.h"
#include "CommandBuffers.h"

namespace engine
{

struct PipelineInfo;

class GltfPipeline
{
public:

	GltfPipeline() = default;
	~GltfPipeline();

	void Create(common::crc_t gltfCrc, const PipelineInfo& rPipelineInfo, bool bAddGltfDescriptors = true);
	void AllocateSecondaryBuffers(const char* pcName);
	void RecordDrawIndirect(int64_t iCommandBuffer, VkCommandBuffer vkCommandBuffer, const XMFLOAT4& rf4PushConstants = {});
	void WriteIndirectBuffer(int64_t iCommandBuffer, int64_t iCount);
	void UpdateStorageBufferDescriptors(int64_t iFramebuffer, int64_t iBinding, Buffer* pBuffer);
	void RerecordSecondary(int64_t iFramebuffer, VkRenderPass vkRenderPass, VkFramebuffer vkFramebuffer, const XMFLOAT4& rf4PushConstants = {});

	int64_t miMaterialCount = 0;
	Pipeline mpPipelines[common::GltfHeader::kiMaxMaterials] {};
	int64_t mpiIndexCounts[common::GltfHeader::kiMaxMaterials] {};
	int64_t mpiFirstIndices[common::GltfHeader::kiMaxMaterials] {};

	// Per-pipeline secondary command buffers [framebuffer]
	std::vector<VkCommandBuffer> mSecondaryBuffers;
};

} // namespace engine
