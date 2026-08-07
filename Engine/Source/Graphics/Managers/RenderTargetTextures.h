#pragma once

#if defined(BT_CLIENT)

namespace engine
{

struct RenderTargetTextures
{
	void Create();

	void DestroyLightingTextures();
	void CreateLightingTextures();
	void RegisterDebugTextures(int64_t iPassCount);
	void CreateShadowTextures();
	void CreateSmokeTextures();
	void CreateWindTextures();
	void CreateObjectShadowsTextures();
	void CreateTerrainTextures();
	void CreateWaterDisplacementTextures();

	Texture mLogTexture;

	Texture mTerrainElevationTexture;

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

	// Direction-averaged 0.25*(E+W+N+S) of the tone-mapped per-direction values.
	// Sampled by Terrain/Water in the ambient path to replace three EWNS samples with one.
	Texture mAmbientCombineTexture;

	// Persistent previous-frame combined lighting (3 directional + ambient), reprojected by LightingTemporal.comp to
	// de-flicker the texel-ramp resample (mirror of mShadowHistoryTexture). LightingHistoryCopy.comp publishes the
	// temporal result over the whole texture on refresh.
	Texture mpLightingHistoryTextures[3];
	Texture mAmbientHistoryTexture;

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
	Texture mShadowHistoryTexture; // Previous-frame final shadow, refreshed by ShadowHistoryCopy.comp for ShadowTemporal reprojection

	Texture mObjectShadowsTexture;
	Texture mObjectShadowsBlurTexture;
	Texture mObjectShadowsBlurIntermediateTexture;

	// Per-frame compute output: Gerstner-wave displacement + Jacobian normal sampled by Water.vert
	// (kPipelineWater) instead of the vertex shader recomputing the wave sum. Sized to
	// the LOD0 water-mesh vertex grid; active LOD writes only the top-left iWaterActiveQuadX+1 by
	// iWaterActiveQuadY+1 texel rectangle each frame.
	Texture mWaterDisplacementTexture;
	Texture mWaterDisplacementNormalTexture;

	std::vector<Texture*> mElevationTextures;
	std::vector<Texture*> mColorTextures;
	std::vector<Texture*> mNormalsTextures;
	std::vector<Texture*> mAmbientOcclusionTextures;
	std::vector<Texture*> mMasksTextures;
};

} // namespace engine

#endif // defined(BT_CLIENT)
