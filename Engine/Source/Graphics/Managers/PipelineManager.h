#pragma once

#include "Graphics/Objects/Pipeline.h"
#include "Graphics/Objects/GltfPipeline.h"
#include "Graphics/Objects/Shader.h"

#include "Graphics/GltfPipelines.h"

namespace engine
{

class DeviceManager;

struct GltfPipelineSpec
{
	const char* pcName = nullptr;
	common::crc_t gltfCrc = 0;
	PipelineInfo pipelineInfo {};
	bool bAddGltfDescriptors = true;
	bool bIsPipelineShadow = false;
};


enum Pipelines
{
	kPipelineLog,

	kPipelineBillboards,

	kPipelineHexShields,

	kPipelineObjectShadowsBlur,
	kPipelineShadow,
	kPipelineShadowBlur,
	kPipelineShadowElevation,

	kPipelineHexShieldsLighting,
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
	kPipelineWidgets,

	kPipelineWater,

	kPipelineSmokeClearOne,
	kPipelineSmokeClearTwo,
	kPipelineSmokeSpreadOne,
	kPipelineSmokeSpreadTwo,
	kPipelineSmokePuffs,
	kPipelineSmokeTrails,

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

#if defined(ENABLE_NEW_LIGHTING)
#else
	void CreateLightingBlurCombinePipelines(Pipelines eCombinePipeline, Texture* pLightingTexture, Pipeline (&pLightingBlurPipelines)[shaders::kiMaxLightingBlurCount], Texture (&pLightingBlurTextures)[shaders::kiMaxLightingBlurCount]);
#endif
	void CreateLightingPipelines();
	void CreatePipelineShadows();
	void CreateLightingShadowDependantPipelines();

	Pipeline mpPipelines[kPipelineCount];

	Pipeline mpRedLightingBlurPipelines[shaders::kiMaxLightingBlurCount];
	Pipeline mpGreenLightingBlurPipelines[shaders::kiMaxLightingBlurCount];
	Pipeline mpBlueLightingBlurPipelines[shaders::kiMaxLightingBlurCount];

	std::vector<std::unique_ptr<Pipeline>> mDynamicPipelines;

	GltfPipeline* CreateGltfPipeline(const GltfPipelineSpec& spec);
	void CreateDynamicGltfPipeline(common::crc_t crc, const char* pcName, common::crc_t gltfCrc, common::crc_t modelVertexBufferCrc, Buffer* pStorageBuffers);
	void CreateDynamicGltfPipelineShadow(common::crc_t crc, const char* pcName, common::crc_t gltfCrc, common::crc_t modelVertexBufferCrc, Buffer* pStorageBuffers);
	void CreateDynamicPipelineLighting(common::crc_t crc, const char* pcName, int64_t iBufferSize);
	void CreateDynamicPipelineVisibleLights(common::crc_t crc, const char* pcName, Buffer* pStorageBuffers);
	void CreateDynamicPipelineAxisAlignedLighting(common::crc_t crc, const char* pcName, int64_t iBufferSize);
	// DT: TEMP Why are there six of these, should there be 3 maps only?
	std::vector<std::unique_ptr<GltfPipeline>> mDynamicGltfPipelines;
	std::unordered_map<common::crc_t, Pipeline*> mDynamicPipelinesLightingMap;
	std::unordered_map<common::crc_t, Pipeline*> mDynamicPipelinesVisibleLightsMap;
	std::unordered_map<common::crc_t, Pipeline*> mDynamicPipelinesAxisAlignedLightingMap;
	std::unordered_map<common::crc_t, GltfPipeline*> mDynamicGltfPipelineMap;
	std::unordered_map<common::crc_t, GltfPipeline*> mDynamicGltfPipelineShadowMap;

	// DT: TEMP Remove?
	game::GltfPipelines mGltfPipelines;
};

inline PipelineManager* gpPipelineManager = nullptr;

} // namespace engine
