#pragma once

namespace engine
{

struct ModelPipelineSpec
{
	std::string_view name;
	common::crc_t sceneCrc = 0;
	PipelineInfo pipelineInfo {};
	bool bAddModelDescriptors = true;
	bool bIsPipelineShadow = false;
};

enum DynamicPipelineType
{
	kDynamicPipelineLighting,
	kDynamicPipelineAxisAlignedLighting,
	kDynamicPipelineVisibleLights,
	kDynamicPipelineBillboards,
	kDynamicPipelineSmokeAxisAligned,
	kDynamicPipelineSmoke,
	kDynamicPipelineWindDepositA,
	kDynamicPipelineWindDepositB,
	kDynamicPipelineWindDepositAxisAlignedA,
	kDynamicPipelineWindDepositAxisAlignedB,
	kDynamicPipelineHexShields,
	kDynamicPipelineHexShieldsLighting,

	kDynamicPipelineCount,
};

enum DynamicModelPipelineType
{
	kDynamicModelPipelineModel,
	kDynamicModelPipelineModelShadow,

	kDynamicModelPipelineCount,
};

class DynamicPipelines
{
public:

	explicit DynamicPipelines(std::unordered_map<common::crc_t, Shader>& rShaders);

	ModelPipeline* CreateModelPipeline(const ModelPipelineSpec& rModelPipelineSpec);
	void CreateModelPipeline(common::crc_t crc, std::string_view name, common::crc_t sceneCrc, Buffer* pStorageBuffers);
	void CreateModelPipelineShadow(common::crc_t crc, std::string_view name, common::crc_t sceneCrc, Buffer* pStorageBuffers);
	void CreatePipelineLighting(common::crc_t crc, std::string_view name, int64_t iBufferSize);
	void CreatePipelineVisibleLights(common::crc_t crc, std::string_view name, Buffer* pStorageBuffers);
	void CreatePipelineAxisAlignedLighting(common::crc_t crc, std::string_view name, int64_t iBufferSize);
	void CreatePipelineBillboards(common::crc_t crc, std::string_view name, int64_t iBufferSize);
	void CreatePipelineSmokeAxisAligned(common::crc_t crc, std::string_view name, int64_t iBufferSize);
	void CreatePipelineSmoke(common::crc_t crc, std::string_view name, int64_t iBufferSize);
	void CreatePipelineWindDepositA(common::crc_t crc, std::string_view name, int64_t iBufferSize);
	void CreatePipelineWindDepositB(common::crc_t crc, std::string_view name);
	void CreatePipelineWindDepositAxisAlignedA(common::crc_t crc, std::string_view name, int64_t iBufferSize);
	void CreatePipelineWindDepositAxisAlignedB(common::crc_t crc, std::string_view name);
	void CreatePipelineHexShields(common::crc_t crc, std::string_view name, int64_t iBufferSize);
	void CreatePipelineHexShieldsLighting(common::crc_t crc, std::string_view name);

	std::vector<std::unique_ptr<Pipeline>> mPipelines;
	std::vector<std::unique_ptr<ModelPipeline>> mModelPipelines;
	std::unordered_map<common::crc_t, Pipeline*> mPipelineMaps[kDynamicPipelineCount];
	std::unordered_map<common::crc_t, ModelPipeline*> mModelPipelineMaps[kDynamicModelPipelineCount];
	std::unordered_map<common::crc_t, std::string> mShadowPipelineNames; // Owns shadow pipeline name strings

private:

	std::unordered_map<common::crc_t, Shader>& mrShaders;
};

} // namespace engine
