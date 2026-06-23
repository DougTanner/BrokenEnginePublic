#pragma once

namespace engine
{

class Pipeline;

struct PipelineCreator
{
	static void CreateGraphicsPipeline(Pipeline& rPipeline);
	static void CreateComputePipeline(Pipeline& rPipeline);
};

} // namespace engine
