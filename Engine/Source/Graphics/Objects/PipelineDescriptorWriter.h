#pragma once

namespace engine
{

class Pipeline;
class Buffer;
struct PipelineInfo;

struct PipelineDescriptorWriter
{
	static void Write(Pipeline& rPipeline, const PipelineInfo& rInfo);
	static void UpdateStorageBuffer(Pipeline& rPipeline, int64_t iFramebuffer, int64_t iBinding, Buffer* pBuffer);
	static void UpdateCombinedImageSampler(Pipeline& rPipeline, int64_t iBinding, VkImageView vkImageView, VkSampler vkSampler);
	static void UpdateSampler(Pipeline& rPipeline, int64_t iBinding, VkSampler vkSampler);
};

} // namespace engine
