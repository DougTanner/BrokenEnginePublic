#pragma once

#include "Graphics/Objects/Pipeline.h"
#include "Graphics/Objects/Shader.h"

#include "Graphics/GltfPipelines.h"

namespace engine
{

class DeviceManager;

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

	game::GltfPipelines mGltfPipelines;
};

inline PipelineManager* gpPipelineManager = nullptr;

} // namespace engine
