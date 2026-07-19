#pragma once

#if defined(BT_CLIENT)

namespace engine
{

struct CommandBufferRecordGlobal
{
	static void Record(int64_t iFramebuffer);

private:

	static void RecordShadowPasses(VkCommandBuffer vkCommandBuffer, int64_t iCommandBuffer, Pipeline* pPipelines);
	static void RecordTerrainPasses(VkCommandBuffer vkCommandBuffer, int64_t iCommandBuffer, Pipeline* pPipelines);
	static void RecordWindSpreadPipeline(VkCommandBuffer vkCommandBuffer, int64_t iCommandBuffer, uint32_t uiWindTilesX, uint32_t uiWindTilesY, Pipeline* pPipelines);
	static void RecordSmokeSpreadPipeline(VkCommandBuffer vkCommandBuffer, int64_t iCommandBuffer, uint32_t uiSmokeTilesX, uint32_t uiSmokeTilesY, Pipeline* pPipelines);
	static void RecordSmokeSpreadHalf(VkCommandBuffer vkCommandBuffer, int64_t iCommandBuffer, uint32_t uiDilateGroups, Pipeline& rDilatePipeline, VkBuffer vkOutputOccupancyBuffer, Texture& rSmokeTexture, Pipeline& rSpreadPipeline);
	static void RecordParticleUpdatePasses(VkCommandBuffer vkCommandBuffer, int64_t iCommandBuffer, Pipeline* pPipelines);
};

} // namespace engine

#endif // defined(BT_CLIENT)
