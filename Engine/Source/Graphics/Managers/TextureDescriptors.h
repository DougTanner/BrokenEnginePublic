#pragma once

#if defined(BT_CLIENT)

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
	void InitializeIslandSlots();

	class ScopedBindlessWriteEpoch
	{
	public:
		explicit ScopedBindlessWriteEpoch(TextureDescriptors& rTextureDescriptors);
		~ScopedBindlessWriteEpoch();

	private:
		TextureDescriptors& mrTextureDescriptors;
	};

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
	void RegisterBindlessArrayConsumer(Texture** ppTextures, Pipeline* pPipeline, int64_t iBinding, DescriptorFlags_t samplerFlags, int64_t iCount);
	void RegisterStandaloneSamplerBinding(Pipeline* pPipeline, int64_t iBinding, DescriptorFlags_t samplerFlags);
	void UnregisterPipeline(Pipeline* pPipeline);
	void UpdateDescriptorsForTexture(common::crc_t crc);
	// Island slots own five coordinated array elements: template-owned elevation plus four lazy
	// chunk-backed channels. The registry keeps their metadata and descriptor registrations together.
	void MintIslandSlot(int64_t iSlot, common::crc_t islandCrc, Texture& rElevationTexture, const common::crc_t (&textureCrcs)[4]);
	// Redirects every island channel to slot-0 placeholders, updates Set 0/Set 1 descriptors, and
	// removes all five binding records before the caller destroys the old resources or reuses iSlot.
	void EvictIslandSlot(int64_t iSlot, common::crc_t islandCrc, const common::crc_t (&textureCrcs)[4]);
	// Template-owned elevation has no lazy chunk; restore it at the four-channel-ready transition.
	void RestoreIslandSlot(common::crc_t islandCrc);
	void VerifyAllDescriptorGenerations() const;
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
		// Owned copy of texture pointers for full-array bindings (e.g. water normals). Per-island
		// bindings retain only pTexture, the descriptor element they own.
		std::vector<Texture*> textures;
		// pTexture->muiGeneration / textures[i]->muiGeneration snapshotted at descriptor-write time.
		// VerifyAllDescriptorGenerations breaks if the live texture's generation
		// has drifted (texture destroyed/recreated since this descriptor was written).
		uint64_t uiTextureGeneration = 0;
		std::vector<uint64_t> uiTextureGenerations;
		// True when this descriptor element deliberately resolves to the placeholder because its
		// owned Texture has no view. Verification accepts that state until a real view appears.
		bool bTextureUsesPlaceholder = false;
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
	void SynchronizeFullArrayBindingGenerations(const TextureBinding& rBinding);
	// Write the single (non-array) combined-image-sampler descriptor for a binding, resolving the view from
	// rBinding.pTexture or, if null, the CRC's mTextureMap entry. Extracted from RewriteSamplerDescriptors.
	void WriteSingleTextureBinding(common::crc_t crc, TextureBinding& rBinding, VkSampler vkSampler);
	void WriteFullArrayDescriptors(Pipeline& rPipeline, int64_t iBinding, Texture* const* ppArray, int64_t iCount, VkSampler vkSampler);
	void WriteArrayElementFromLive(Texture** ppArray, int64_t iIndex);
	void RegisterIslandSlotBindings(common::crc_t bindingKey, Texture** ppTextures, int64_t iSlot);
	void UnregisterBindingsForKey(common::crc_t bindingKey);
	void AssertBindlessWriteEpoch() const;

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

	struct IslandSlot
	{
		common::crc_t islandCrc = 0;
		common::crc_t textureCrcs[4] {};
		Texture* pElevationTexture = nullptr;
	};

	std::unordered_map<common::crc_t, std::vector<TextureBinding>> mTextureBindings;
	std::unordered_map<Texture**, std::vector<BindlessArrayConsumer>> mBindlessArrayConsumers;
	std::unordered_map<int64_t, IslandSlot> mIslandSlots;
	std::vector<StandaloneSamplerBinding> mStandaloneSamplerBindings;

	std::vector<VkDescriptorImageInfo> mImageInfos;
	std::unordered_map<common::crc_t, int64_t> mImageInfosMap;
	int64_t miNextTextureIndex = 0;

private:

	TextureManager& mrTextureManager;
	bool mbBindlessWriteEpoch = false;
};

} // namespace engine

#endif // defined(BT_CLIENT)
