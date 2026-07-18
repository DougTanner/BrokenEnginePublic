#pragma once

#if defined(BT_CLIENT)

namespace engine
{

struct CommandBufferRecordMain
{
	static void Record(int64_t iFramebuffer);

private:

	static void RecordLightingDeposit(VkCommandBuffer vkCommandBuffer, int64_t iCommandBuffer);
	static void RecordLightingSpreadPipeline(VkCommandBuffer vkCommandBuffer, int64_t iCommandBuffer);
	static void RecordSmokeEmit(VkCommandBuffer vkCommandBuffer, int64_t iCommandBuffer);
	static void RecordWindDeposits(VkCommandBuffer vkCommandBuffer, int64_t iCommandBuffer);
	static void RecordObjectShadows(VkCommandBuffer vkCommandBuffer, int64_t iCommandBuffer);
	static void RecordObjectShadowsBlur(VkCommandBuffer vkCommandBuffer, int64_t iCommandBuffer);
	static void RecordWaterDisplacement(VkCommandBuffer vkCommandBuffer, int64_t iCommandBuffer);
	static void RecordImageRenderPass(VkCommandBuffer vkCommandBuffer, int64_t iCommandBuffer, int64_t iFramebuffer);
	static void RecordHighDynamicRangeResolve(VkCommandBuffer vkCommandBuffer, int64_t iCommandBuffer, int64_t iFramebuffer);
};

} // namespace engine

#endif // defined(BT_CLIENT)
