#pragma once

#include "Graphics/Objects/Pipeline.h"
#include "Graphics/Objects/Texture.h"

namespace engine
{

std::tuple<int64_t, int64_t> CombineTextureInfo();

inline constexpr common::ConstexprCrcArray<shaders::kiSquareParticlesCookieCount> kSquareParticleCrcs("Textures\\Particles\\[BC4]Square\\", ".png");
inline constexpr common::ConstexprCrcArray<shaders::kiLongParticlesCookieCount> kLongParticleCrcs("Textures\\Particles\\[BC4]Long\\", ".png");

class TextureManager
{
public:

	static std::tuple<int64_t, int64_t> DetailTextureSize(float fMultiplier);
	static float DetailTextureAspectRatio();

	TextureManager();
	~TextureManager();

	void DestroySamplers();
	void CreateSamplers();

	void CreateLightingTextures();
	void CreateShadowTextures();
	void CreateSmokeTextures();
	void CreateObjectShadowsTextures();

	VkSampler GetSampler(DescriptorFlags_t flags);

	void GenerateGltfCubemap(bool bIrradiance);
	void GenerateGltfLutBrdf();

	VkSampler mVkSamplerSmoke = VK_NULL_HANDLE;
	VkSampler mVkSamplerBorder = VK_NULL_HANDLE;
	VkSampler mVkSamplerClamp = VK_NULL_HANDLE;
	VkSampler mVkSamplerRepeat = VK_NULL_HANDLE;
	VkSampler mVkSamplerMirroredRepeat = VK_NULL_HANDLE;
	VkSampler mVkSamplerNearestBorder = VK_NULL_HANDLE;

	std::unordered_map<common::crc_t, Texture> mTextureMap;
	std::vector<VkDescriptorImageInfo> mImageInfos;
	std::unordered_map<common::crc_t, int64_t> mImageInfosMap;
	std::vector<VkDescriptorImageInfo> mUiImageInfos;
	std::unordered_map<common::crc_t, int64_t> mUiImageInfosMap;

#if defined(ENABLE_DEBUG_PRINTF_EXT)
	Texture mLogTexture;
#endif

	Texture mTerrainElevationTexture;
	Texture mTerrainColorTexture;
	Texture mTerrainNormalTexture;
	Texture mTerrainAmbientOcclusionTexture;

	Texture mSmokeGradientTexture;
	Texture mSmokeTextureOne;
	Texture mSmokeTextureTwo;

	Texture mpLightingTextures[3];
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

	Texture mMissileTexture;

	Texture* mpSquareParticleTextures[shaders::kiParticlesCookieCount] {};
	Texture* mpLongParticleTextures[shaders::kiParticlesCookieCount] {};

	std::vector<Texture*> mElevationTextures;
	std::vector<Texture*> mColorTextures;
	std::vector<Texture*> mNormalsTextures;
	std::vector<Texture*> mAmbientOcclusionTextures;

	int64_t miGltfCubeMipCount = 0;
	Texture mGltfIrradianceTexture;
	Texture mGltfPreFilteredTexture;
	Texture mGltfLutBrdfTexture;
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
	auto result = gpTextureManager->mUiImageInfosMap.find(crc);
	if (result != gpTextureManager->mUiImageInfosMap.end())
	{
		return static_cast<uint32_t>(result->second);
	}

	DEBUG_BREAK();
	return 0;
}

} // namespace engine
