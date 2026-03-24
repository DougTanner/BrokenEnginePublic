#pragma once

#include "Data/Texture.h"

namespace engine
{

struct RenderTargetTextures
{
	void Create();

	void DestroyLightingTextures();
	void CreateLightingTextures();
	void CreateCascadeTextures(int64_t iLightingTextureX, int64_t iLightingTextureY);
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

	// First spread textures (intermediate between deposit and cascade)
	Texture mpFirstSpreadTextures[3]; // R, G, B — RGBA16F

	// Cascade spread textures (per level, 3 colors with EWNS in RGBA channels)
	static constexpr int64_t kiMaxCascadeLevels = 8;
	int64_t miCascadeLevelCount = 0;
	Texture mpCascadeTextures[kiMaxCascadeLevels][3]; // [level][color] RGBA16F

	// Final accumulated output
	Texture mpLightingAccumulateTextures[3]; // R, G, B
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
