#pragma once

#if defined(BT_CLIENT)

namespace engine
{

class Pipeline;
class Buffer;

struct PipelineDescriptorWriter
{
	// Returns true if uiBinding has a non-zero descriptorCount in either shader's reflected layout.
	// Exposed so deferred-update callers (TextureDescriptors::Register*) can validate at register time.
	static bool BindingExistsInShaderLayout(const Pipeline& rPipeline, uint32_t uiBinding);

	static void Write(Pipeline& rPipeline);
	static void UpdateStorageBuffer(Pipeline& rPipeline, int64_t iFramebuffer, int64_t iBinding, Buffer* pBuffer);
	static void UpdateCombinedImageSampler(Pipeline& rPipeline, int64_t iBinding, VkImageView vkImageView, VkSampler vkSampler);
	static void UpdateSampler(Pipeline& rPipeline, int64_t iBinding, VkSampler vkSampler);
	static void UpdateStorageImage(Pipeline& rPipeline, int64_t iBinding, VkImageView vkImageView);
};

} // namespace engine

#endif // defined(BT_CLIENT)
