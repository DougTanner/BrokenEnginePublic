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

	void RegisterTextureBinding(common::crc_t crc, Pipeline* pPipeline, int64_t iBinding, DescriptorFlags_t samplerFlags, Texture* pTexture = nullptr, Texture** ppTextures = nullptr, int64_t iCount = 0, int64_t iArrayIndex = -1);
	void RegisterStandaloneSamplerBinding(Pipeline* pPipeline, int64_t iBinding, DescriptorFlags_t samplerFlags);
	void UpdateDescriptorsForTexture(common::crc_t crc);
	// Array-binding-only descriptor refresh keyed on a binding map slot that has no mTextureMap entry
	// (e.g. per-island elevation, whose Texture lives on IslandTemplate rather than in TextureManager).
	// Writes per-pipeline array descriptors from the registered Texture** snapshot; skips the bindless
	// Set 0 mImageInfos slot that UpdateDescriptorsForTexture would also touch.
	void UpdateArrayBindingsForKey(common::crc_t bindingKey);
	void RewriteSamplerDescriptors();
	void ClearTextureBindings();

	float CrcToIndex(common::crc_t crc);
	float CrcToBlurredIndex(common::crc_t crc);

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
		// Owned copy of texture pointers for array bindings (water normals, island channels).
		// iArrayIndex < 0: descriptor write covers the full array. iArrayIndex >= 0: write a single
		// element at that index (used by per-island-slot bindings so out-of-order chunk-ready writes
		// don't clobber other slots whose snapshots have stale placeholder pointers).
		std::vector<Texture*> textures;
		// pTexture->muiGeneration / textures[i]->muiGeneration snapshotted at descriptor-write time.
		// PipelineManager::VerifyAllDescriptorGenerations breaks if the live texture's generation
		// has drifted (texture destroyed/recreated since this descriptor was written).
		uint64_t uiTextureGeneration = 0;
		std::vector<uint64_t> uiTextureGenerations;
		int64_t iArrayIndex = -1;
	};

	// Standalone sampler binding tracking for sampler recreation
	struct StandaloneSamplerBinding
	{
		Pipeline* pPipeline = nullptr;
		int64_t iBinding = -1;
		DescriptorFlags_t samplerFlags;
	};

	void WriteArrayBindingDescriptors(TextureBinding& rBinding, VkSampler vkSampler);

	// Consumer entry for a bindless texture array whose per-slot binding key is supplied lazily by
	// the data subsystem (e.g., IslandTerrain). Populated by PipelineDescriptorWriter when it sees
	// a DescriptorInfo flagged with kBindlessArrayConsumer. Map key is the array pointer
	// (e.g., RenderTargetTextures::mElevationTextures.data()), so the per-entry array pointer is
	// implicit in the map key and not duplicated here.
	struct BindlessArrayConsumer
	{
		Pipeline* pPipeline = nullptr;
		int64_t iBinding = -1;
		DescriptorFlags_t samplerFlags;
		int64_t iCount = 0;
	};

	std::unordered_map<common::crc_t, std::vector<TextureBinding>> mTextureBindings;
	std::unordered_map<Texture**, std::vector<BindlessArrayConsumer>> mBindlessArrayConsumers;
	std::vector<StandaloneSamplerBinding> mStandaloneSamplerBindings;

	std::vector<VkDescriptorImageInfo> mImageInfos;
	std::unordered_map<common::crc_t, int64_t> mImageInfosMap;
	int64_t miNextTextureIndex = 0;

private:

	TextureManager& mrTextureManager;
};

} // namespace engine
