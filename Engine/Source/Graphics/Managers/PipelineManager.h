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
	kPipelineShadowTemporal,
	kPipelineShadowElevation,

	kPipelineTerrainElevation,
	kPipelineTerrain,

	kPipelineProfileText,

	kPipelineWater,
	kPipelineWaterSkyboxOne,
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

	kPipelineCount
};

// GLSL set=1 binding indices for the terrain pipeline group (kPipelineShadowElevation,
// kPipelineTerrainElevation, kPipelineTerrain). The bindings are split across three sites:
// the GLSL `layout(set = 1, binding = N)` declarations in Terrain*.frag, the implicit-by-position
// DescriptorInfo tables in PipelineManager.cpp, and the bare-integer RegisterTextureBinding calls
// in IslandTerrain::AcquireTextureSlot. Naming the indices keeps the three sites in lockstep.
namespace TerrainPipelineBindings
{
	constexpr int64_t kiElevation = 2;
	constexpr int64_t kiColor = 6;
	constexpr int64_t kiNormals = 7;
	constexpr int64_t kiAmbientOcclusion = 8;
	constexpr int64_t kiMasks = 20;
} // namespace TerrainPipelineBindings

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

	// Temporal-accumulation pass after combine: reprojects + EMA-blends the 4 combine outputs in place against history
	Pipeline mLightingTemporalPipeline;

	DynamicPipelines mDynamicPipelines;
};

inline PipelineManager* gpPipelineManager = nullptr;

} // namespace engine
