#pragma once

#include "Graphics/Objects/Pipeline.h"
#include "Graphics/Objects/ModelPipeline.h"

namespace engine
{

class DeviceManager;

struct ModelPipelineSpec
{
	std::string_view name;
	common::crc_t sceneCrc = 0;
	PipelineInfo pipelineInfo {};
	bool bAddModelDescriptors = true;
	bool bIsPipelineShadow = false;
};

enum Pipelines
{
	kPipelineLog,

	kPipelineObjectShadowsBlur,
	kPipelineShadow,
	kPipelineShadowBlur,
	kPipelineShadowElevation,

	kPipelineLongParticlesLighting,
	kPipelineSquareParticlesLighting,
	kPipelineRedLightingCombine,
	kPipelineGreenLightingCombine,
	kPipelineBlueLightingCombine,

	kPipelineTerrainElevation,
	kPipelineTerrainColor,
	kPipelineTerrainNormal,
	kPipelineTerrainAmbientOcclusion,
	kPipelineTerrain,

	kPipelineProfileText,

	kPipelineWater,

	kPipelineSmokeClearOne,
	kPipelineSmokeClearTwo,
	kPipelineSmokeSpreadOne,
	kPipelineSmokeSpreadTwo,

	kPipelineWindClear,
	kPipelineWindSpread,
	kPipelineWindClearTwo,
	kPipelineWindSpreadTwo,

	kPipelineLongParticlesSpawn,
	kPipelineLongParticlesUpdate,
	kPipelineLongParticlesRender,
	kPipelineSquareParticlesSpawn,
	kPipelineSquareParticlesUpdate,
	kPipelineSquareParticlesRender,

	kPipelineCount
};

enum DynamicPipelineType
{
	kDynamicPipelineLighting,
	kDynamicPipelineAxisAlignedLighting,
	kDynamicPipelineVisibleLights,
	kDynamicPipelineBillboards,
	kDynamicPipelineSmokeAxisAligned,
	kDynamicPipelineSmoke,
	kDynamicPipelineWindDeposit,
	kDynamicPipelineWindDepositTwo,
	kDynamicPipelineWindDepositAxisAligned,
	kDynamicPipelineWindDepositAxisAlignedTwo,
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

class PipelineManager
{
public:

	PipelineManager();
	~PipelineManager();

	void CreateLightingBlurCombinePipelines(Pipelines eCombinePipeline, Texture* pLightingTexture, Pipeline (&pLightingBlurPipelines)[shaders::kiMaxLightingBlurCount], Texture (&pLightingBlurTextures)[shaders::kiMaxLightingBlurCount]);
	void CreateLightingPipelines();
	void CreatePipelineShadows();
	void CreateLightingShadowDependantPipelines();

	Pipeline mpPipelines[kPipelineCount];

	Pipeline mpRedLightingBlurPipelines[shaders::kiMaxLightingBlurCount];
	Pipeline mpGreenLightingBlurPipelines[shaders::kiMaxLightingBlurCount];
	Pipeline mpBlueLightingBlurPipelines[shaders::kiMaxLightingBlurCount];

	std::vector<std::unique_ptr<Pipeline>> mDynamicPipelines;

	ModelPipeline* CreateModelPipeline(const ModelPipelineSpec& spec);
	void CreateDynamicModelPipeline(common::crc_t crc, std::string_view name, common::crc_t sceneCrc, Buffer* pStorageBuffers);
	void CreateDynamicModelPipelineShadow(common::crc_t crc, std::string_view name, common::crc_t sceneCrc, Buffer* pStorageBuffers);
	void CreateDynamicPipelineLighting(common::crc_t crc, std::string_view name, int64_t iBufferSize);
	void CreateDynamicPipelineVisibleLights(common::crc_t crc, std::string_view name, Buffer* pStorageBuffers);
	void CreateDynamicPipelineAxisAlignedLighting(common::crc_t crc, std::string_view name, int64_t iBufferSize);
	void CreateDynamicPipelineBillboards(common::crc_t crc, std::string_view name, int64_t iBufferSize);
	void CreateDynamicPipelineSmokeAxisAligned(common::crc_t crc, std::string_view name, int64_t iBufferSize);
	void CreateDynamicPipelineSmoke(common::crc_t crc, std::string_view name, int64_t iBufferSize);
	void CreateDynamicPipelineWindDeposit(common::crc_t crc, std::string_view name, int64_t iBufferSize);
	void CreateDynamicPipelineWindDepositTwo(common::crc_t crc, std::string_view name, int64_t iBufferSize);
	void CreateDynamicPipelineWindDepositAxisAligned(common::crc_t crc, std::string_view name, int64_t iBufferSize);
	void CreateDynamicPipelineWindDepositAxisAlignedTwo(common::crc_t crc, std::string_view name, int64_t iBufferSize);
	void CreateDynamicPipelineHexShields(common::crc_t crc, std::string_view name, int64_t iBufferSize);
	void CreateDynamicPipelineHexShieldsLighting(common::crc_t crc, std::string_view name);
	std::vector<std::unique_ptr<ModelPipeline>> mDynamicModelPipelines;
	std::array<std::unordered_map<common::crc_t, Pipeline*>, kDynamicPipelineCount> mDynamicPipelineMaps;
	std::array<std::unordered_map<common::crc_t, ModelPipeline*>, kDynamicModelPipelineCount> mDynamicModelPipelineMaps;
	std::unordered_map<common::crc_t, std::string> mShadowPipelineNames; // Owns shadow pipeline name strings
};

inline PipelineManager* gpPipelineManager = nullptr;

} // namespace engine
