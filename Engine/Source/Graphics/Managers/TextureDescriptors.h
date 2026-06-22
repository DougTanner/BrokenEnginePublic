#pragma once

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

	// Parameter bundle for RegisterTextureBinding. Encodes three mutually-exclusive binding shapes:
	// single texture (pTexture), full array (ppTextures + iCount), or single array element (also iArrayIndex >= 0).
	struct TextureBindingInfo
	{
		common::crc_t crc = 0;
		Pipeline* pPipeline = nullptr;
		int64_t iBinding = -1;
		DescriptorFlags_t samplerFlags;
		Texture* pTexture = nullptr;
		Texture** ppTextures = nullptr;
		int64_t iCount = 0;
		int64_t iArrayIndex = -1;
	};
	void RegisterTextureBinding(const TextureBindingInfo& rInfo);
	void RegisterStandaloneSamplerBinding(Pipeline* pPipeline, int64_t iBinding, DescriptorFlags_t samplerFlags);
	void UpdateDescriptorsForTexture(common::crc_t crc);
	// Array-binding-only descriptor refresh keyed on a binding map slot that has no mTextureMap entry
	// (e.g. per-island elevation, whose Texture lives on IslandTemplate rather than in TextureManager).
	// Writes per-pipeline array descriptors from the registered Texture** snapshot; skips the bindless
	// Set 0 mImageInfos slot that UpdateDescriptorsForTexture would also touch.
	void UpdateArrayBindingsForKey(common::crc_t bindingKey);
	// Erase all per-pipeline binding records registered under a key. Called by IslandTerrain eviction
	// when a slot is reclaimed: the records snapshot Texture*s whose images are about to be freed, and
	// PipelineManager::VerifyAllDescriptorGenerations would flag those stale (non-zero generation,
	// null image) snapshots at CB-record time. Re-mint re-registers fresh records.
	void UnregisterBindingsForKey(common::crc_t bindingKey);
	void RewriteSamplerDescriptors();
	void ClearTextureBindings();

	int64_t CrcToIndex(common::crc_t crc);
	float CrcToBlurredIndex(common::crc_t crc);

	// Salt XOR'd into a texture CRC to key its pre-blurred bindless-array variant. Single-sourced here;
	// referenced by both the blur-write site (TextureManager) and CrcToBlurredIndex (TextureDescriptors).
	static constexpr common::crc_t kBlurSalt = 0x424C5552; // "BLUR"

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
	// Write the single (non-array) combined-image-sampler descriptor for a binding, resolving the view from
	// rBinding.pTexture or, if null, the CRC's mTextureMap entry. Extracted from RewriteSamplerDescriptors.
	void WriteSingleTextureBinding(common::crc_t crc, TextureBinding& rBinding, VkSampler vkSampler);
	void WriteFullArrayDescriptors(Pipeline& rPipeline, int64_t iBinding, Texture* const* ppArray, int64_t iCount, VkSampler vkSampler);
	// Rewrite the single per-pipeline descriptor element at iIndex for every consumer of a bindless
	// array, reading the Texture* currently in the live array (ppArray[iIndex]). Used by
	// IslandTerrain::EvictionSweep to point a freed slot at the slot-0 placeholder, so a recycled slot
	// never samples the destroyed VkImageView the prior occupant left in the descriptor.
	void WriteArrayElementFromLive(Texture** ppArray, int64_t iIndex);

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
