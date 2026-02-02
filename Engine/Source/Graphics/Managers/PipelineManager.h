#pragma once

#include "Graphics/Objects/Pipeline.h"
#include "Graphics/Objects/GltfPipeline.h"
#include "Graphics/Objects/Shader.h"

namespace engine
{

class DeviceManager;

struct GltfPipelineSpec
{
	std::string_view name;
	common::crc_t gltfCrc = 0;
	PipelineInfo pipelineInfo {};
	bool bAddGltfDescriptors = true;
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

	kPipelineLongParticlesSpawn,
	kPipelineLongParticlesUpdate,
	kPipelineLongParticlesRender,
	kPipelineSquareParticlesSpawn,
	kPipelineSquareParticlesUpdate,
	kPipelineSquareParticlesRender,

	kPipelineCount
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

	GltfPipeline* CreateGltfPipeline(const GltfPipelineSpec& spec);
	void CreateDynamicGltfPipeline(common::crc_t crc, std::string_view name, common::crc_t gltfCrc, Buffer* pStorageBuffers);
	void CreateDynamicGltfPipelineShadow(common::crc_t crc, std::string_view name, common::crc_t gltfCrc, Buffer* pStorageBuffers);
	void CreateDynamicPipelineLighting(common::crc_t crc, std::string_view name, int64_t iBufferSize);
	void CreateDynamicPipelineVisibleLights(common::crc_t crc, std::string_view name, Buffer* pStorageBuffers);
	void CreateDynamicPipelineAxisAlignedLighting(common::crc_t crc, std::string_view name, int64_t iBufferSize);
	void CreateDynamicPipelineBillboards(common::crc_t crc, std::string_view name, int64_t iBufferSize);
	void CreateDynamicPipelineSmokeAxisAligned(common::crc_t crc, std::string_view name, int64_t iBufferSize);
	void CreateDynamicPipelineSmoke(common::crc_t crc, std::string_view name, int64_t iBufferSize);
	void CreateDynamicPipelineHexShields(common::crc_t crc, std::string_view name, int64_t iBufferSize);
	void CreateDynamicPipelineHexShieldsLighting(common::crc_t crc, std::string_view name);
	// DT: TODO Why are there six of these, should there be 3 maps only?
	std::vector<std::unique_ptr<GltfPipeline>> mDynamicGltfPipelines;
	std::unordered_map<common::crc_t, Pipeline*> mDynamicPipelinesLightingMap;
	std::unordered_map<common::crc_t, Pipeline*> mDynamicPipelinesVisibleLightsMap;
	std::unordered_map<common::crc_t, Pipeline*> mDynamicPipelinesAxisAlignedLightingMap;
	std::unordered_map<common::crc_t, Pipeline*> mDynamicPipelinesBillboardsMap;
	std::unordered_map<common::crc_t, Pipeline*> mDynamicPipelinesSmokeAxisAlignedMap;
	std::unordered_map<common::crc_t, Pipeline*> mDynamicPipelinesSmokeMap;
	std::unordered_map<common::crc_t, Pipeline*> mDynamicPipelinesHexShieldsMap;
	std::unordered_map<common::crc_t, Pipeline*> mDynamicPipelinesHexShieldsLightingMap;
	std::unordered_map<common::crc_t, GltfPipeline*> mDynamicGltfPipelineMap;
	std::unordered_map<common::crc_t, GltfPipeline*> mDynamicGltfPipelineShadowMap;
	std::unordered_map<common::crc_t, std::string> mShadowPipelineNames; // Owns shadow pipeline name strings
};

inline PipelineManager* gpPipelineManager = nullptr;

} // namespace engine
