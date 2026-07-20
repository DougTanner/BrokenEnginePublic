#pragma once

#if defined(BT_CLIENT)

#include "Data/Texture.h"

#include "RenderTargetTextures.h"
#include "TextureCache.h"
#include "TextureDescriptors.h"

namespace engine
{

class TextureManager
{
public:

	static std::tuple<int64_t, int64_t> DetailTextureSize(float fMultiplier);
	static std::tuple<int64_t, int64_t> LightingDetailTextureSize(float fMultiplier);
	static std::tuple<int64_t, int64_t> WaterDetailTextureSize(float fMultiplier);
	static float DetailTextureAspectRatio();

	TextureManager();
	~TextureManager();
	// Starts asynchronous uploads and performs fatal-rethrowing boot waits after Graphics owns this manager.
	void InitializeBootTextures();

	void DestroyScreenDependentResources();
	void CreateScreenDependentResources();

	void DestroySamplers();
	void CreateSamplers();

	VkSampler GetSampler(DescriptorFlags_t flags);

	// Process newly loaded textures from lazy loading system
	void ProcessPendingTextures(int64_t iFramebufferIndex);

	// Allocate the per-framebuffer acquire-barrier command pool + buffers (mAcquireVkCommandPool /
	// mAcquireVkCommandBuffers). Shared by the ctor and CreateScreenDependentResources.
	void CreateAcquireCommandBuffers();

	// Create a 1x1 programmatic placeholder Texture. Shared by the seven ctor placeholders (white / white
	// cube / the five island bindless-array anchors); only name/flags/format/layers/view-type/pixel differ.
	void CreatePlaceholderTexture(Texture& rTexture, std::string_view name, VkImageCreateFlags vkImageCreateFlags, VkFormat vkFormat, uint32_t uiArrayLayers, VkImageViewType vkImageViewType, const std::function<void(void*, int64_t, int64_t)>& rPixelWriter);

	// ProcessPendingTextures seam helpers: adopt one transfer-queue-uploaded chunk (descriptor write +
	// optional acquire barrier + lighting blur), and lazily begin the acquire command buffer on first use.
	void AdoptUploadedChunk(common::crc_t crc, Texture& rTexture, bool bNeedAcquireBarrier, VkCommandBuffer vkAcquireCommandBuffer, bool& brRecordedBarriers);
	void EnsureAcquireCommandBufferBegun(VkCommandBuffer vkAcquireCommandBuffer, bool& brRecordedBarriers);

	// True when ProcessPendingTextures will adopt a chunk this frame (and thus write descriptor
	// elements). Drives the RenderGlobal all-framebuffer-fence drain so those writes don't race an
	// in-flight frame still sampling the slot.
	bool AnyAdoptionPending() const;

	// Wait for textures to be loaded and update their data
	void WaitForTextures(std::span<const common::crc_t> crcs);
	void WaitForTextures(std::span<Texture* const> textures);

	// Water normal map atlas: ordered alphabetically by stripped display name; index used by Wrapper indices and shader.
	static inline constexpr int64_t kiWaterNormalCount = shaders::kiWaterNormalCount;
	static inline constexpr common::crc_t kpWaterNormalCrcs[kiWaterNormalCount]
	{
		data::kTexturesWaterBC50pngCrc,                  // 0
		data::kTexturesWaterBC53jpgCrc,                  // 3
		data::kTexturesWaterBC5FoamjpgCrc,               // Foam
		data::kTexturesWaterBC5FoamBjpgCrc,              // FoamB
		data::kTexturesWaterBC5GreenCalmjpgCrc,          // GreenCalm
		data::kTexturesWaterBC5GreenSeajpgCrc,           // GreenSea
		data::kTexturesWaterBC5GreenSeaBjpgCrc,          // GreenSeaB
		data::kTexturesWaterBC5LakejpgCrc,               // Lake
		data::kTexturesWaterBC5PondSedimentjpgCrc,       // PondSediment
		data::kTexturesWaterBC5PooljpgCrc,               // Pool
		data::kTexturesWaterBC5SeaDistantjpgCrc,         // SeaDistant
		data::kTexturesWaterBC5SeaWavesjpgCrc,           // SeaWaves
		data::kTexturesWaterBC5SeaWavesBjpgCrc,          // SeaWavesB
		data::kTexturesWaterBC5SlimyWaterjpgCrc,         // SlimyWater
		data::kTexturesWaterBC5SlimyWaterBjpgCrc,        // SlimyWaterB
		data::kTexturesWaterBC5StonesAndRipplesjpgCrc,   // StonesAndRipples
		data::kTexturesWaterBC5WaterFalljpgCrc,          // WaterFall
	};
	static inline constexpr std::string_view kpWaterNormalNames[kiWaterNormalCount]
	{
		"0", "3", "Foam", "FoamB", "GreenCalm", "GreenSea", "GreenSeaB", "Lake",
		"PondSediment", "Pool", "SeaDistant", "SeaWaves", "SeaWavesB", "SlimyWater",
		"SlimyWaterB", "StonesAndRipples", "WaterFall",
	};

	// IBL cubemap CRCs (loaded by InitializeBootTextures; referenced by Model and Water pipelines).
	static inline constexpr common::crc_t kIrradianceCrc       = data::kTexturesCKloofendalPuresky_IrradianceR16G16B16A16_SFLOATCrc;
	static inline constexpr common::crc_t kPrefilteredCrc      = data::kTexturesCKloofendalPuresky_PrefilteredR16G16B16A16_SFLOATCrc;
	static inline constexpr common::crc_t kPrefilteredWaterCrc = data::kTexturesCRyfjallet_PrefilteredR16G16B16A16_SFLOATCrc;

	// Non-water priority textures preloaded at boot, surrounding the water-normal block below.
	static inline constexpr common::crc_t kpPriorityHead[]
	{
		data::kTexturesWaterDepthLutpngCrc,
		data::kTexturesWaterBC4NoisepngCrc,
	};
	static inline constexpr common::crc_t kpPriorityTail[]
	{
		data::kTexturesTerrainBC7Rock0jpgCrc,
		data::kTexturesTerrainBC5RockNormal1jpgCrc,
		data::kTexturesTerrainBC5RockNormal2jpgCrc,
		data::kTexturesTerrainBC5RockNormal4jpgCrc,
		data::kTexturesTerrainBC7SandpngCrc,
		data::kTexturesTerrainBC5SandNormal0jpgCrc,
		data::kTexturesTerrainBC5SandNormal1pngCrc,
		data::kTexturesTerrainBC5SandNormal2pngCrc,
	};

	// Priority textures preloaded at boot so chevron switches and the first terrain frame never show a
	// placeholder. The water-normal block is single-sourced from kpWaterNormalCrcs — do not re-list it here.
	static inline constexpr int64_t kiPriorityTextureCount = std::size(kpPriorityHead) + kiWaterNormalCount + std::size(kpPriorityTail);
	struct PriorityTextures
	{
		common::crc_t pCrcs[kiPriorityTextureCount] {};
	};
	static inline constexpr PriorityTextures kpPriorityTextures = []() consteval
	{
		PriorityTextures priorityTextures {};
		int64_t i = 0;
		for (common::crc_t crc : kpPriorityHead)
		{
			priorityTextures.pCrcs[i++] = crc;
		}
		for (common::crc_t crc : kpWaterNormalCrcs)
		{
			priorityTextures.pCrcs[i++] = crc;
		}
		for (common::crc_t crc : kpPriorityTail)
		{
			priorityTextures.pCrcs[i++] = crc;
		}
		return priorityTextures;
	}();

	RenderTargetTextures mRenderTargetTextures;
	TextureCache mTextureCache;
	TextureDescriptors mTextureDescriptors;

	// Sampler storage indexed by SamplerSlot. CreateSamplers builds each slot with its bespoke
	// VkSamplerCreateInfo; GetSampler maps a DescriptorFlags sampler bit to one of these.
	enum SamplerSlot : int64_t
	{
		kSamplerSlotSmoke,
		kSamplerSlotWindClamp,
		kSamplerSlotBorder,
		kSamplerSlotBorderWhite,
		kSamplerSlotClamp,
		kSamplerSlotElevation,
		kSamplerSlotRepeat,
		kSamplerSlotRepeatModelData,
		kSamplerSlotMirroredRepeat,
		kSamplerSlotMirroredRepeatWater,
		kSamplerSlotCount,
	};
	VkSampler mpSamplers[kSamplerSlotCount] {};

	// Per-mip Toksvig variance tables for the water normal maps, copied from TextureHeader::pfMipVariance
	// during the ctor chunk walk (headers are resident at startup; the lazy pixel data is not). Indexed by
	// water-normal atlas slot; LightingUniforms uploads the three selected octave groups' tables each frame.
	float mpfWaterNormalMipVariance[kiWaterNormalCount][common::TextureHeader::kiMipVarianceCount] {};

	Texture mWhiteTexture;
	Texture mWhiteCubeTexture;

	// Slot-0 island bindless-array anchor. Programmatic 1x1 textures with neutral per-channel
	// values: ocean-bottom elevation (submerged below the water), mid-gray color, up-vector normals,
	// no-AO. Never adopted by a real island (miNextTextureSlot starts at 1); higher slots alias these
	// until their real chunks reach kReady via RestorationSweep.
	Texture mIslandPlaceholderElevation;
	Texture mIslandPlaceholderColor;
	Texture mIslandPlaceholderNormals;
	Texture mIslandPlaceholderAmbientOcclusion;
	Texture mIslandPlaceholderMasks;

	std::unordered_map<common::crc_t, Texture> mTextureMap;

	// Pre-blur lighting textures
	std::unordered_set<common::crc_t> mLightingTextureCrcs;
	std::unordered_map<common::crc_t, Texture> mBlurredLightingTextures;
	std::unordered_map<common::crc_t, Texture> mBlurIntermediateTextures;
	void RegisterLightingTextureCrc(common::crc_t crc);
	void BlurLightingTexture(common::crc_t crc, bool bNeedAcquireBarrier = false);
	void ReblurAllLightingTextures();

	VkCommandPool mAcquireVkCommandPool = VK_NULL_HANDLE;
	std::vector<VkCommandBuffer> mAcquireVkCommandBuffers;
	int64_t miAcquireFramebufferIndex = 0;
	bool mbHasPendingAcquireBarriers = false;
};

inline TextureManager* gpTextureManager = nullptr;

} // namespace engine

#endif // defined(BT_CLIENT)
