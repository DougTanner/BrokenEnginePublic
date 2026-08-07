#pragma once

#if defined(BT_CLIENT)

#include "DynamicPipelines.h"
#include "WorldLightingShadowPipelines.h"

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
	kPipelineShadowTemporal,
	kPipelineShadowHistoryCopy,
	kPipelineShadowElevation,

	kPipelineTerrainElevation,
	kPipelineTerrain,

	kPipelineWater,
	kPipelineWaterDisplacement,

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

	kPipelineHdrResolve,

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

	Pipeline mpPipelines[kPipelineCount];

	// Texture* array for the kPipelineWater normal map atlas binding (sized sampler array).
	// Populated at the top of CreateLightingShadowDependentPipelines() before kPipelineWater is built.
	Texture* mppWaterNormalTextures[shaders::kiWaterNormalCount] {};

	// Spread pipelines [pass]: radial directional spread, fragment shader with MRT
	Pipeline mSpreadPipelines[shaders::kiMaxSpreadPasses];
	// Owns the spread pipeline name strings (PipelineInfo::name is a string_view that must outlive the Create call;
	// mirrors mShadowPipelineNames in DynamicPipelines)
	std::string mSpreadPipelineNames[shaders::kiMaxSpreadPasses];

	// Combine pipeline (tone map accumulate → UNORM, all 3 colors in one dispatch)
	Pipeline mCombinePipeline;

	// Temporal-accumulation pass after combine: reprojects + EMA-blends the 4 combine outputs in place against history
	Pipeline mLightingTemporalPipeline;

	// Publishes the temporal-blended combine outputs into the distinct persistent history images.
	Pipeline mLightingHistoryCopyPipeline;

	DynamicPipelines mDynamicPipelines;

private:

	WorldLightingShadowPipelines mWorldLightingShadowPipelines;
};

inline PipelineManager* gpPipelineManager = nullptr;

} // namespace engine

#endif // defined(BT_CLIENT)
