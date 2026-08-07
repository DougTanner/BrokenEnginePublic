#pragma once

#if defined(BT_CLIENT)

namespace engine
{

class Pipeline;
class Shader;
class Texture;

class WorldLightingShadowPipelines
{
public:

	WorldLightingShadowPipelines(std::unordered_map<common::crc_t, Shader>& rShaders, Pipeline* pPipelines, Pipeline* pSpreadPipelines, std::string* pSpreadPipelineNames, Pipeline& rCombinePipeline, Pipeline& rLightingTemporalPipeline, Pipeline& rLightingHistoryCopyPipeline, Texture** ppWaterNormalTextures);

	void CreateLightingPipelines();
	void CreatePipelineShadows();
	void CreateLightingBlurPipelines();
	void CreateLightingShadowDependentPipelines();

private:

	std::unordered_map<common::crc_t, Shader>& mrShaders;
	Pipeline* mpPipelines;
	Pipeline* mpSpreadPipelines;
	std::string* mpSpreadPipelineNames;
	Pipeline& mrCombinePipeline;
	Pipeline& mrLightingTemporalPipeline;
	Pipeline& mrLightingHistoryCopyPipeline;
	Texture** mppWaterNormalTextures;
};

} // namespace engine

#endif // defined(BT_CLIENT)
