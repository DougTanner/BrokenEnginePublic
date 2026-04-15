#pragma once

#include "Data/Texture.h"

namespace engine
{

struct RenderTargetTextures
{
	void Create();

	void DestroyLightingTextures();
	void CreateLightingTextures();
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
	Texture* mppLightingDepositTextures[3] {};
	VkRenderPass mLightingVkRenderPass = VK_NULL_HANDLE;
	VkFramebuffer mLightingVkFramebuffer = VK_NULL_HANDLE;

	// Spread output textures [pass][color R/G/B]
	// mpSpreadTextures: accumulated output fed into next pass
	// mpSpreadOnlyTextures: pre-accumulation snapshot read by LightCombine
	Texture mpSpreadTextures[shaders::kiMaxSpreadPasses][3];
	Texture mpSpreadOnlyTextures[shaders::kiMaxSpreadPasses][3];
	VkRenderPass mSpreadVkRenderPass = VK_NULL_HANDLE;
	VkFramebuffer mpSpreadVkFramebuffers[shaders::kiMaxSpreadPasses] {};

	Texture mpCombineTextures[3];
	Texture* mppLightingFinalTextures[3] {};

	// Debug texture array (channels A/B/C used by formats needing 3 textures per slot, e.g., spread direction combined)
	int64_t miDebugTextureCount = 0;
	Texture* mppDebugTextures[shaders::kiMaxDebugTextures] {};
	Texture* mppDebugTexturesB[shaders::kiMaxDebugTextures] {};
	Texture* mppDebugTexturesC[shaders::kiMaxDebugTextures] {};
	int64_t mpDebugTextureFormats[shaders::kiMaxDebugTextures] {};

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
