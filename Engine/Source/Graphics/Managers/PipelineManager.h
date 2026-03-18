#pragma once

#include "DynamicPipelines.h"

namespace engine
{

class DeviceManager;

enum Pipelines
{
	kPipelineLog,

	kPipelineObjectShadowsBlurH,
	kPipelineObjectShadowsBlurV,
	kPipelineShadow,
	kPipelineShadowBlurH,
	kPipelineShadowBlurV,
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

	kPipelineSmokeClearA,
	kPipelineSmokeClearB,
	kPipelineSmokeSpreadComputeA,
	kPipelineSmokeSpreadComputeB,
	kPipelineSmokeOccupancyDilate,

	kPipelineWindClearA,
	kPipelineWindSpreadA,
	kPipelineWindClearB,
	kPipelineWindSpreadB,

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

	std::unordered_map<common::crc_t, Shader> mShaders;

	void CreateLightingCombinePipeline(Pipelines eCombinePipeline, Texture (&pLightingBlurTextures)[shaders::kiMaxLightingBlurCount]);
	void CreateLightingPipelines();
	void CreatePipelineShadows();
	void CreateLightingShadowDependantPipelines();
	void CreateTerrainDataPipelines();
	void CreateSmokeWindPipelines();
	void CreateParticlePipelines();
	void RecreatePipelineGroups(DestroyFlags_t flags);

	Pipeline mpPipelines[kPipelineCount];

	Pipeline mpLightingBlurPipelines[shaders::kiMaxLightingBlurCount];

	DynamicPipelines mDynamicPipelines;
};

inline PipelineManager* gpPipelineManager = nullptr;

} // namespace engine
