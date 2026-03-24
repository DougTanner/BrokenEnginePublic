#pragma once

#include "Data/Texture.h"

#include "RenderTargetTextures.h"
#include "TextureCache.h"
#include "TextureDescriptors.h"

namespace engine
{

struct LazyChunk;

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

	static inline std::vector<common::crc_t> smPriorityTextures
	{
		data::kTexturesUiBC4NotoSansRegularpngCrc,
		data::kTexturesUiBC4NotoSansSCLightpngCrc,
		data::kTexturesWaterDepthLutpngCrc,
		data::kTexturesWaterBC4NoisepngCrc,
		data::kTexturesWaterBC70pngCrc,
		data::kTexturesWaterBC73jpgCrc,
		data::kTexturesTerrainBC7Rock0jpgCrc,
		data::kTexturesTerrainBC7RockNormal1jpgCrc,
		data::kTexturesTerrainBC7RockNormal2jpgCrc,
		data::kTexturesTerrainBC7RockNormal4jpgCrc,
		data::kTexturesTerrainBC7SandpngCrc,
		data::kTexturesTerrainBC7SandNormal0jpgCrc,
		data::kTexturesTerrainBC7SandNormal1pngCrc,
		data::kTexturesTerrainBC7SandNormal2pngCrc,
	};

	RenderTargetTextures mRenderTargetTextures;
	TextureCache mTextureCache;
	TextureDescriptors mTextureDescriptors;

	VkSampler mVkSamplerSmoke = VK_NULL_HANDLE;
	VkSampler mVkSamplerWindClamp = VK_NULL_HANDLE;
	VkSampler mVkSamplerBorder = VK_NULL_HANDLE;
	VkSampler mVkSamplerClamp = VK_NULL_HANDLE;
	VkSampler mVkSamplerRepeat = VK_NULL_HANDLE;
	VkSampler mVkSamplerMirroredRepeat = VK_NULL_HANDLE;
	Texture mWhiteTexture;
	Texture mWhiteCubeTexture;

	std::unordered_map<common::crc_t, Texture> mTextureMap;

	VkCommandPool mAcquireVkCommandPool = VK_NULL_HANDLE;
	std::vector<VkCommandBuffer> mAcquireVkCommandBuffers;
	int64_t miAcquireFramebufferIndex = 0;
	bool mbHasPendingAcquireBarriers = false;
};

inline TextureManager* gpTextureManager = nullptr;

} // namespace engine
