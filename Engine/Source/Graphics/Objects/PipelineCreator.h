#pragma once

namespace engine
{

class Pipeline;
struct PipelineInfo;

struct PipelineCreator
{
	static void CreateGraphicsPipeline(Pipeline& rPipeline, const PipelineInfo& rInfo);
	static void CreateComputePipeline(Pipeline& rPipeline, const PipelineInfo& rInfo);
};

} // namespace engine
