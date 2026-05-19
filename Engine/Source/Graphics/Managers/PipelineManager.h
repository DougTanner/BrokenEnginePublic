#pragma once

#include "DynamicPipelines.h"

namespace engine
{

enum Pipelines
{
	kPipelineLog,

	kPipelineObjectShadowsBlurH,
	kPipelineObjectShadowsBlurV,
	kPipelineShadow,
	kPipelineShadowBlurH,
	kPipelineShadowBlurV,
	kPipelineShadowElevation,

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
	kPipelineSmokeOccupancyDilateRemap,

	kPipelineWindOccupancyDilateA,
	kPipelineWindOccupancyDilateB,
	kPipelineWindSpreadComputeA,
	kPipelineWindSpreadComputeB,

	kPipelineLongParticlesSpawn,
	kPipelineLongParticlesUpdate,
	kPipelineLongParticlesRender,
	kPipelineSquareParticlesSpawn,
	kPipelineSquareParticlesUpdate,
	kPipelineSquareParticlesRender,

	kPipelineLightingBlurH,
	kPipelineLightingBlurV,

	kPipelineDebugTexture,

	kPipelineUiDepthPrepass,

	kPipelineDebugBox,
	kPipelineDebugSphere,
	kPipelineDebugCircle,
	kPipelineDebugLine,

	kPipelineCount
};

class PipelineManager
{
public:

	PipelineManager();
	~PipelineManager();

	std::unordered_map<common::crc_t, Shader> mShaders;

	void CreateLightingPipelines();
	void CreateLightingBlurPipelines();
	void CreatePipelineShadows();
	void CreateLightingShadowDependentPipelines();
	void CreateTerrainDataPipelines();
	void CreateSmokeWindPipelines();
	void CreateParticlePipelines();
	void CreateDebugRenderPipelines();

	// Walks every TextureBinding entry and breaks if any cached snapshot generation diverges
	// from the live Texture's muiGeneration. Catches descriptor staleness the Vulkan validation
	// layer cannot detect (handles stay valid; only the semantic binding becomes wrong).
	void VerifyAllDescriptorGenerations();

	Pipeline mpPipelines[kPipelineCount];

	// Texture* array for the kPipelineWater normal map atlas binding (sized sampler array).
	// Populated at the top of CreateLightingShadowDependentPipelines() before kPipelineWater is built.
	// Size must match TextureManager::kiWaterNormalCount (literal here because TextureManager.h is included after PipelineManager.h via Engine.h).
	Texture* mppWaterNormalTextures[17] {};

	// Spread pipelines [pass]: radial directional spread, fragment shader with MRT
	Pipeline mSpreadPipelines[shaders::kiMaxSpreadPasses];

	// Combine pipeline (tone map accumulate → UNORM, all 3 colors in one dispatch)
	Pipeline mCombinePipeline;

	DynamicPipelines mDynamicPipelines;
};

inline PipelineManager* gpPipelineManager = nullptr;

} // namespace engine
