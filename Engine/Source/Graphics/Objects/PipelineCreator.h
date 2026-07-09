#pragma once

#if defined(BT_CLIENT)

namespace engine
{

class Pipeline;

struct PipelineCreator
{
	static void CreateGraphicsPipeline(Pipeline& rPipeline);
	static void CreateComputePipeline(Pipeline& rPipeline);
};

} // namespace engine

#endif // defined(BT_CLIENT)
