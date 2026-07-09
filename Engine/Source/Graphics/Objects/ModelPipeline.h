#pragma once

#if defined(BT_CLIENT)

#include "Pipeline.h"

namespace engine
{

enum class ModelDrawPass : uint8_t { kAll, kOpaque, kTransparent };

enum class ModelPipelineFlags : uint64_t
{
	kTexturesRequested       = 0x0001, // Bindless texture load latched by the first non-empty indirect write
	kHasTransparentMaterials = 0x0002, // Any material exported with BLEND alpha mode (drives the transparent draw pass)
};
using ModelPipelineFlags_t = common::Flags<ModelPipelineFlags>;

class ModelPipeline
{
public:

	ModelPipeline() = default;
	~ModelPipeline() = default;

	void Create(common::crc_t sceneCrc, const PipelineInfo& rPipelineInfo, bool bIsShadow = false);
	void RecordDrawIndirect(int64_t iCommandBuffer, VkCommandBuffer vkCommandBuffer, const XMFLOAT4& rf4PushConstants = {}, ModelDrawPass ePass = ModelDrawPass::kAll);
	void WriteIndirectBuffer(int64_t iCommandBuffer, int64_t iCount);
	void UpdateStorageBufferDescriptors(int64_t iFramebuffer, int64_t iBinding, Buffer* pBuffer);

	int64_t miMaterialCount = 0;
	std::vector<Pipeline> mpPipelines;
	std::vector<int64_t> mpiIndexCounts;
	std::vector<int64_t> mpiFirstIndices;
	common::crc_t mSceneCrc = 0;
	std::vector<bool> mpbTransparentMaterials;
	ModelPipelineFlags_t mFlags;
};

} // namespace engine

#endif // defined(BT_CLIENT)
