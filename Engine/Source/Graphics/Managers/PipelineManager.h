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
	bool bIsShadowPipeline = false;
};

struct GltfPipelinePairSpec
{
	const char* pcName = nullptr;
	common::crc_t gltfCrc = 0;
	common::crc_t modelVertexBufferCrc = 0;
	Buffer* pStorageBuffers = nullptr;
};

struct GltfPipelinePair
{
	GltfPipeline* pPipeline = nullptr;
	GltfPipeline* pShadowPipeline = nullptr;
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

	kPipelineVisibleLights,

	kPipelineAreaLights,
	kPipelinePointLights,
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
	void CreateShadowPipelines();
	void CreateLightingShadowDependantPipelines();

	Pipeline mpPipelines[kPipelineCount];

	Pipeline mpRedLightingBlurPipelines[shaders::kiMaxLightingBlurCount];
	Pipeline mpGreenLightingBlurPipelines[shaders::kiMaxLightingBlurCount];
	Pipeline mpBlueLightingBlurPipelines[shaders::kiMaxLightingBlurCount];

	std::vector<std::unique_ptr<Pipeline>> mDynamicPipelines;

	GltfPipeline* CreateGltfPipeline(const GltfPipelineSpec& spec);
	GltfPipelinePair CreateGltfPipelinePair(const GltfPipelinePairSpec& spec);
	std::vector<std::unique_ptr<GltfPipeline>> mDynamicGltfPipelines;
	std::vector<GltfPipeline*> mRegisteredGltfPipelines;
	std::vector<GltfPipeline*> mRegisteredGltfShadowPipelines;

	game::GltfPipelines mGltfPipelines;
};

inline PipelineManager* gpPipelineManager = nullptr;

} // namespace engine
