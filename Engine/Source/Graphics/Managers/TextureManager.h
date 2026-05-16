#pragma once

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
	static float DetailTextureAspectRatio();

	TextureManager();
	~TextureManager();

	void DestroyScreenDependentResources();
	void CreateScreenDependentResources();

	void DestroySamplers();
	void CreateSamplers();

	VkSampler GetSampler(DescriptorFlags_t flags);

	// Process newly loaded textures from lazy loading system
	void ProcessPendingTextures(int64_t iFramebufferIndex);

	// Wait for textures to be loaded and update their data
	void WaitForTextures(std::span<const common::crc_t> crcs);
	void WaitForTextures(std::span<Texture* const> textures);

	// Water normal map atlas: ordered alphabetically by stripped display name; index used by Wrapper indices and shader.
	static inline constexpr int64_t kiWaterNormalCount = 17;
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
	static inline constexpr int64_t kiWaterNormalSeaWavesIndex = 11;

	// IBL cubemap CRCs (loaded eagerly in ctor; referenced by Model and Water pipelines).
	static inline constexpr common::crc_t kIrradianceCrc       = data::kTexturesCKloofendalPuresky_IrradianceR16G16B16A16_SFLOATCrc;
	static inline constexpr common::crc_t kPrefilteredCrc      = data::kTexturesCKloofendalPuresky_PrefilteredR16G16B16A16_SFLOATCrc;
	static inline constexpr common::crc_t kPrefilteredWaterCrc = data::kTexturesCRyfjallet_PrefilteredR16G16B16A16_SFLOATCrc;

	static inline std::vector<common::crc_t> smPriorityTextures
	{
		data::kTexturesUiBC4NotoSansRegularpngCrc,
		data::kTexturesUiBC4NotoSansSCLightpngCrc,
		data::kTexturesWaterDepthLutpngCrc,
		data::kTexturesWaterBC4NoisepngCrc,
		// All 17 BC5 water normals (mirror of kpWaterNormalCrcs) so chevron switches never show a placeholder frame.
		data::kTexturesWaterBC50pngCrc,
		data::kTexturesWaterBC53jpgCrc,
		data::kTexturesWaterBC5FoamjpgCrc,
		data::kTexturesWaterBC5FoamBjpgCrc,
		data::kTexturesWaterBC5GreenCalmjpgCrc,
		data::kTexturesWaterBC5GreenSeajpgCrc,
		data::kTexturesWaterBC5GreenSeaBjpgCrc,
		data::kTexturesWaterBC5LakejpgCrc,
		data::kTexturesWaterBC5PondSedimentjpgCrc,
		data::kTexturesWaterBC5PooljpgCrc,
		data::kTexturesWaterBC5SeaDistantjpgCrc,
		data::kTexturesWaterBC5SeaWavesjpgCrc,
		data::kTexturesWaterBC5SeaWavesBjpgCrc,
		data::kTexturesWaterBC5SlimyWaterjpgCrc,
		data::kTexturesWaterBC5SlimyWaterBjpgCrc,
		data::kTexturesWaterBC5StonesAndRipplesjpgCrc,
		data::kTexturesWaterBC5WaterFalljpgCrc,
		data::kTexturesTerrainBC7Rock0jpgCrc,
		data::kTexturesTerrainBC5RockNormal1jpgCrc,
		data::kTexturesTerrainBC5RockNormal2jpgCrc,
		data::kTexturesTerrainBC5RockNormal4jpgCrc,
		data::kTexturesTerrainBC7SandpngCrc,
		data::kTexturesTerrainBC5SandNormal0jpgCrc,
		data::kTexturesTerrainBC5SandNormal1pngCrc,
		data::kTexturesTerrainBC5SandNormal2pngCrc,
	};

	RenderTargetTextures mRenderTargetTextures;
	TextureCache mTextureCache;
	TextureDescriptors mTextureDescriptors;

	VkSampler mVkSamplerSmoke = VK_NULL_HANDLE;
	VkSampler mVkSamplerWindClamp = VK_NULL_HANDLE;
	VkSampler mVkSamplerBorder = VK_NULL_HANDLE;
	VkSampler mVkSamplerClamp = VK_NULL_HANDLE;
	VkSampler mVkSamplerElevation = VK_NULL_HANDLE;
	VkSampler mVkSamplerRepeat = VK_NULL_HANDLE;
	VkSampler mVkSamplerMirroredRepeat = VK_NULL_HANDLE;
	Texture mWhiteTexture;
	Texture mWhiteCubeTexture;

	// Slot-0 island bindless-array anchor. Programmatic 1x1 textures with neutral per-channel
	// values: sea-level elevation, mid-gray color, up-vector normals, no-AO. Never adopted by a
	// real island (miNextTextureSlot starts at 1); higher slots alias these until their real
	// chunks reach kReady via RestorationSweep.
	Texture mIslandPlaceholderElevation;
	Texture mIslandPlaceholderColor;
	Texture mIslandPlaceholderNormals;
	Texture mIslandPlaceholderAmbientOcclusion;

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
