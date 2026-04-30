#pragma once

namespace engine
{

struct CommandBufferRecordGlobal
{
	static void Record(int64_t iFramebuffer);

private:

	static void RecordTerrainPasses(VkCommandBuffer vkCommandBuffer, int64_t iCommandBuffer, Pipeline* pPipelines);
	static void RecordWindSpreadPipeline(VkCommandBuffer vkCommandBuffer, int64_t iCommandBuffer, uint32_t uiWindWidth, Pipeline* pPipelines);
	static void RecordSmokeSpreadPipeline(VkCommandBuffer vkCommandBuffer, int64_t iCommandBuffer, uint32_t uiSmokeTilesX, Pipeline* pPipelines);
	static void RecordParticleUpdatePasses(VkCommandBuffer vkCommandBuffer, int64_t iCommandBuffer, Pipeline* pPipelines);
};

} // namespace engine
