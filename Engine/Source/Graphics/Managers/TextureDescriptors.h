#pragma once

#include "Data/Texture.h"

namespace engine
{

class TextureManager;

class TextureDescriptors
{
public:

	explicit TextureDescriptors(TextureManager& rTextureManager);

	void Create();
	void Destroy();

	void WriteGlobalDescriptorSets();
	void UpdateTextureArrayDescriptors();

	void RegisterTextureBinding(common::crc_t crc, Pipeline* pPipeline, int64_t iBinding, DescriptorFlags_t samplerFlags, Texture* pTexture = nullptr, Texture** ppTextures = nullptr, int64_t iCount = 0);
	void RegisterStandaloneSamplerBinding(Pipeline* pPipeline, int64_t iBinding, DescriptorFlags_t samplerFlags);
	void UpdateDescriptorsForTexture(common::crc_t crc);
	void RewriteSamplerDescriptors();
	void ClearTextureBindings();

	float CrcToIndex(common::crc_t crc);

	// Global descriptor Set 0 shared by all graphics pipelines
	VkDescriptorSetLayout mGlobalDescriptorSetLayout = VK_NULL_HANDLE;
	std::vector<VkDescriptorSet> mGlobalDescriptorSets;

	// Texture binding tracking for deferred descriptor updates
	struct TextureBinding
	{
		Pipeline* pPipeline = nullptr;
		int64_t iBinding = -1;
		DescriptorFlags_t samplerFlags;
		Texture* pTexture = nullptr;
		// Owned copy of texture pointers for array bindings (islands, lighting blur)
		std::vector<Texture*> textures;
	};

	// Standalone sampler binding tracking for sampler recreation
	struct StandaloneSamplerBinding
	{
		Pipeline* pPipeline = nullptr;
		int64_t iBinding = -1;
		DescriptorFlags_t samplerFlags;
	};

	void WriteArrayBindingDescriptors(const TextureBinding& rBinding, VkSampler vkSampler);

	std::unordered_map<common::crc_t, std::vector<TextureBinding>> mTextureBindings;
	std::vector<StandaloneSamplerBinding> mStandaloneSamplerBindings;

	std::vector<VkDescriptorImageInfo> mImageInfos;
	std::unordered_map<common::crc_t, int64_t> mImageInfosMap;
	int64_t miNextTextureIndex = 0;

private:

	TextureManager& mrTextureManager;
};

} // namespace engine
