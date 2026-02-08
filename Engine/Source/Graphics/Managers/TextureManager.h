#pragma once

#include "Data/Texture.h"
#include "Graphics/Objects/Pipeline.h"
#include "Graphics/Objects/Texture.h"

namespace engine
{

struct LazyChunk;

std::tuple<int64_t, int64_t> CombineTextureInfo();

inline constexpr common::ConstexprCrcArray<shaders::kiSquareParticlesCookieCount> kSquareParticleCrcs("Textures\\Particles\\[BC4]Square\\", ".png");
inline constexpr common::ConstexprCrcArray<shaders::kiLongParticlesCookieCount> kLongParticleCrcs("Textures\\Particles\\[BC4]Long\\", ".png");

struct TextureFileCacheHeader
{
	static constexpr int64_t kiMagic = 0xCACEF11E;
	static constexpr int64_t kiVersion = 2;  // Bumped for sourceCrc field

	int64_t iMagic = 0;
	int64_t iVersion = 0;
	VkFormat vkFormat = VK_FORMAT_UNDEFINED;
	int64_t iWidth = 0;
	int64_t iHeight = 0;
	int64_t iMipLevels = 0;
	int64_t iArrayLayers = 0;
	int64_t iDataSize = 0;
	common::crc_t sourceCrc = 0;  // CRC of source texture used to generate this cache
};

class TextureManager
{
public:

	static std::tuple<int64_t, int64_t> DetailTextureSize(float fMultiplier);
	static float DetailTextureAspectRatio();

	TextureManager();
	~TextureManager();

	void DestroySamplers();
	void CreateSamplers();

	void DestroyLightingTextures();
	void CreateLightingTextures();
	void CreateShadowTextures();
	void CreateSmokeTextures();
	void CreateObjectShadowsTextures();

	VkSampler GetSampler(DescriptorFlags_t flags);

	void GenerateGltfCubemap(bool bIrradiance, common::crc_t skyboxCrc);
	void GenerateGltfLutBrdf();

	// Gltf texture caching
	bool TryLoadCachedTexture(const std::filesystem::path& rCachePath, Texture& rTexture, VkFormat vkFormat, int64_t iWidth, int64_t iHeight, int64_t iMipLevels, int64_t iArrayLayers, common::crc_t sourceCrc = 0);
	void SaveTextureToCache(const std::filesystem::path& rCachePath, const Texture& rTexture, VkFormat vkFormat, common::crc_t sourceCrc = 0);

	// GPU to CPU image transfer helper
	static void CopyImageToHostMemory(VkImage srcImage, VkExtent3D extent, VkFormat format, uint32_t mipLevels, uint32_t arrayLayers, bool bFromSwapchain, std::vector<std::byte>& outData);

	// Process newly loaded textures from lazy loading system
	void ProcessPendingTextures(int64_t iFramebufferIndex);

	// Wait for textures to be loaded and update their data
	void WaitForTextures(std::span<const common::crc_t> crcs);
	void WaitForTextures(std::span<Texture* const> textures);

	static inline std::vector<common::crc_t> smPriorityTextures {data::kTexturesUiBC4NotoSansRegularpngCrc, data::kTexturesUiBC4NotoSansSCLightpngCrc, data::kTexturesWaterDepthLutpngCrc, data::kTexturesWaterBC4NoisepngCrc, data::kTexturesWaterBC70pngCrc, data::kTexturesWaterBC73jpgCrc, data::kTexturesTerrainBC7Rock0jpgCrc, data::kTexturesTerrainBC7RockNormal1jpgCrc, data::kTexturesTerrainBC7RockNormal2jpgCrc, data::kTexturesTerrainBC7RockNormal4jpgCrc, data::kTexturesTerrainBC7SandpngCrc, data::kTexturesTerrainBC7SandNormal0jpgCrc, data::kTexturesTerrainBC7SandNormal1pngCrc, data::kTexturesTerrainBC7SandNormal2pngCrc};

	VkSampler mVkSamplerSmoke = VK_NULL_HANDLE;
	VkSampler mVkSamplerBorder = VK_NULL_HANDLE;
	VkSampler mVkSamplerClamp = VK_NULL_HANDLE;
	VkSampler mVkSamplerRepeat = VK_NULL_HANDLE;
	VkSampler mVkSamplerMirroredRepeat = VK_NULL_HANDLE;
	VkSampler mVkSamplerNearestBorder = VK_NULL_HANDLE;

	Texture mWhiteTexture;
	Texture mWhiteCubeTexture;

	std::unordered_map<common::crc_t, Texture> mTextureMap;
	std::vector<VkDescriptorImageInfo> mImageInfos;
	std::unordered_map<common::crc_t, int64_t> mImageInfosMap;
	std::vector<VkDescriptorImageInfo> mUiImageInfos;
	std::unordered_map<common::crc_t, int64_t> mUiImageInfosMap;

	Texture mLogTexture;

	Texture mTerrainElevationTexture;
	Texture mTerrainColorTexture;
	Texture mTerrainNormalTexture;
	Texture mTerrainAmbientOcclusionTexture;

	Texture mSmokeGradientTexture;
	Texture mSmokeTextureOne;
	Texture mSmokeTextureTwo;

	Texture mpLightingTextures[3];
	VkRenderPass mLightingVkRenderPass = VK_NULL_HANDLE;
	VkFramebuffer mLightingVkFramebuffer = VK_NULL_HANDLE;
	int64_t miLightingBlurCount = 0;
	Texture mpRedLightingBlurTextures[shaders::kiMaxLightingBlurCount] {};
	Texture mpGreenLightingBlurTextures[shaders::kiMaxLightingBlurCount] {};
	Texture mpBlueLightingBlurTextures[shaders::kiMaxLightingBlurCount] {};
	Texture* mppLightingFinalTextures[3] {};

	Texture mShadowElevationTexture;
	Texture mShadowTexture;
	Texture mShadowBlurTexture;

	Texture mObjectShadowsTexture;
	Texture mObjectShadowsBlurTexture;

	Texture* mpSquareParticleTextures[shaders::kiParticlesCookieCount] {};
	Texture* mpLongParticleTextures[shaders::kiParticlesCookieCount] {};

	std::vector<Texture*> mElevationTextures;
	std::vector<Texture*> mColorTextures;
	std::vector<Texture*> mNormalsTextures;
	std::vector<Texture*> mAmbientOcclusionTextures;

	VkCommandPool mAcquireVkCommandPool = VK_NULL_HANDLE;
	std::vector<VkCommandBuffer> mAcquireVkCommandBuffers;
	int64_t miAcquireFramebufferIndex = 0;
	bool mbHasPendingAcquireBarriers = false;

	int64_t miGltfCubeMipCount = 0;
	Texture mGltfIrradianceTexture;
	Texture mGltfPreFilteredTexture;
	Texture mGltfLutBrdfTexture;

	// Texture binding tracking for deferred descriptor updates
	struct TextureBinding
	{
		Pipeline* pPipeline = nullptr;
		int64_t iBinding = -1;
		VkSampler vkSampler = VK_NULL_HANDLE;
		// For array bindings (particles, islands):
		Texture** ppTextures = nullptr;
		int64_t iTextureCount = 0;
	};

	struct TextureArrayPipelineBinding
	{
		Pipeline* pPipeline = nullptr;
		int64_t iBinding = -1;
	};

	std::unordered_map<common::crc_t, std::vector<TextureBinding>> mTextureBindings;
	std::vector<TextureArrayPipelineBinding> mTextureArrayPipelines;
	std::vector<TextureArrayPipelineBinding> mUiTextureArrayPipelines;

	void RegisterTextureBinding(common::crc_t crc, Pipeline* pPipeline, int64_t iBinding, VkSampler vkSampler, Texture** ppTextures = nullptr, int64_t iTextureCount = 0);
	void RegisterTextureArrayPipeline(Pipeline* pPipeline, int64_t iBinding, bool bUi);
	void UpdateDescriptorsForTexture(common::crc_t crc);
	void UpdateTextureArrayDescriptors();
	void ClearTextureBindings();
};

inline TextureManager* gpTextureManager = nullptr;

inline float CrcToIndex(common::crc_t crc)
{
	// Make sure non-Ui textures are not in the Data/Textures/Ui/ directory
	return static_cast<float>(gpTextureManager->mImageInfosMap.at(crc));
}

inline uint32_t UiCrcToIndex(common::crc_t crc)
{
	// Make sure you add Ui textures to the Data/Textures/Ui/ directory
	auto it = gpTextureManager->mUiImageInfosMap.find(crc);
	if (it != gpTextureManager->mUiImageInfosMap.end())
	{
		return static_cast<uint32_t>(it->second);
	}

	common::DebugBreak();
	return 0;
}

} // namespace engine
