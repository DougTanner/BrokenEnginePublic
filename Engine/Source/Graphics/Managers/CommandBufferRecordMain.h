#pragma once

namespace engine
{

struct CommandBufferRecordMain
{
	static void Record(int64_t iFramebuffer);

private:

	static void RecordLightingSpreadPipeline(VkCommandBuffer vkCommandBuffer, int64_t iCommandBuffer);
};

} // namespace engine
