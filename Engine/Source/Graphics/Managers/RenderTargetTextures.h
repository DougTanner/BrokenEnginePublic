#pragma once

#include "Data/Texture.h"

namespace engine
{

struct RenderTargetTextures
{
	void Create();

	void DestroyLightingTextures();
	void CreateLightingTextures();
	void CreateBlurTextures(int64_t iLightingTextureX, int64_t iLightingTextureY, float fDownscale, int64_t iMaxCount);
	void CreateBlurRenderPassAndFramebuffer(int64_t iLevel, int64_t iBlurTextureX, int64_t iBlurTextureY);
	void CreateShadowTextures();
	void CreateSmokeTextures();
	void CreateWindTextures();
	void CreateObjectShadowsTextures();
	void CreateTerrainTextures();

	Texture mLogTexture;

	Texture mTerrainElevationTexture;
	Texture mTerrainColorTexture;
	Texture mTerrainNormalTexture;
	Texture mTerrainAmbientOcclusionTexture;

	Texture mSmokeGradientTexture;
	Texture mSmokeTextureOne;
	Texture mSmokeTextureTwo;

	Texture mWindTextureOne;
	Texture mWindTextureTwo;

	Texture mpLightingTextures[3];
	VkRenderPass mLightingVkRenderPass = VK_NULL_HANDLE;
	VkFramebuffer mLightingVkFramebuffer = VK_NULL_HANDLE;
	int64_t miLightingBlurCount = 0;
	Texture mpRedLightingBlurTextures[shaders::kiMaxLightingBlurCount] {};
	Texture mpGreenLightingBlurTextures[shaders::kiMaxLightingBlurCount] {};
	Texture mpBlueLightingBlurTextures[shaders::kiMaxLightingBlurCount] {};
	VkRenderPass mpLightingBlurVkRenderPasses[shaders::kiMaxLightingBlurCount] {};
	VkFramebuffer mpLightingBlurVkFramebuffers[shaders::kiMaxLightingBlurCount] {};
	Texture* mppLightingFinalTextures[3] {};

	Texture mShadowElevationTexture;
	Texture mShadowTexture;
	Texture mShadowBlurTexture;
	Texture mShadowBlurIntermediateTexture;

	Texture mObjectShadowsTexture;
	Texture mObjectShadowsBlurTexture;
	Texture mObjectShadowsBlurIntermediateTexture;

	std::vector<Texture*> mElevationTextures;
	std::vector<Texture*> mColorTextures;
	std::vector<Texture*> mNormalsTextures;
	std::vector<Texture*> mAmbientOcclusionTextures;
};

} // namespace engine
