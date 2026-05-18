#pragma once

#include "Pipeline.h"

namespace engine
{

inline constexpr int64_t kModelPipelineBindingMeshData = 15;
inline constexpr int64_t kModelPipelineBindingJointMatrix = 16;

enum class ModelDrawPass : uint8_t { kAll, kOpaque, kTransparent };

class ModelPipeline
{
public:

	ModelPipeline() = default;
	~ModelPipeline() = default;

	void Create(common::crc_t sceneCrc, const PipelineInfo& rPipelineInfo, bool bAddModelDescriptors = true, bool bIsShadow = false);
	// Recreate the underlying per-material Pipelines using the original PipelineInfo. Required after any
	// auto-appended texture (mppLightingFinalTextures / mShadowBlurTexture / mSmokeTextureOne) is
	// destroyed and recreated, since descriptor sets cache VkImageView at write time.
	void Recreate();
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

	// Snapshot of the original Create() inputs. PipelineInfo is the unmutated input (kModel flag not
	// yet appended), so Recreate() can re-run Create() and re-execute the kModel auto-append path in
	// Pipeline::Create cleanly.
	PipelineInfo mInfoTemplate {};
	bool mbAddModelDescriptors = false;
	bool mbIsShadow = false;
};

} // namespace engine
