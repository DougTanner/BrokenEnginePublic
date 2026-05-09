#include "WrapperBase.h"

namespace engine
{

// Internal-only wrappers (not bound to any UI: not Tweaks, not GraphicsMenuScreen, not SoundMenuScreen).
Wrapper gFov(45.0f, 25.0f, 110.0f);
Wrapper gWireframe(true);
Wrapper gBaseHeight(6.0f, 0.0f, 20.0f);

// Islands & terrain
Wrapper gVisibleAreaExtraTop(0.046f, 0.0f, 1.0f);
Wrapper gVisibleAreaExtraBottom(0.16f, 0.0f, 1.0f);
Wrapper gIslandAmbientOcclusion(0.6f, 0.0f, 1.0f);
Wrapper gTerrainEarlyOut(-0.1f, -1.0f, 0.0f);
Wrapper gWaterEarlyOut(0.0f, -0.1f, 0.1f);
Wrapper gTerrainElevationTextureMultiplier(0.5f, 0.25f, 1.0f);
Wrapper gTerrainColorTextureMultiplier(2.0f, 1.0f, 3.0f);
Wrapper gTerrainNormalTextureMultiplier(2.0f, 1.0f, 3.0f);
Wrapper gTerrainAmbientOcclusionTextureMultiplier(0.5f, 0.25f, 1.0f);

// Water
Wrapper gWaterHeight(0.0f, -0.1f, 0.03f);
Wrapper gWaterTerrainFadeClamp(0.0f, 0.0f, 0.5f);
Wrapper gWaterNoiseFrequency(0.007f, 0.0f, 0.02f);
Wrapper gWaterNoiseAmount(1.4f, 0.0f, 2.0f);
Wrapper gWaterColorNoiseFrequency(0.0013f, 0.0f, 0.01f);
Wrapper gWaterColorNoiseAmount(0.1f, 0.0f, 0.2f);
Wrapper gWaterDepthLutFeather(4.0f, 0.01f, 10.0f);
Wrapper gWaterDepthColorFeather(5.2f, 0.1f, 20.0f);
Wrapper gWaterFresnel(0.07f, 0.0f, 0.2f);
Wrapper gWaterFresnel2(0.8f, 0.0f, 4.0f);
Wrapper gWaterColorBottom(0.134f, -2.0f, 2.0f);
Wrapper gWaterColorHeight(1.86f, 0.0f, 4.0f);

// Water - Low/Medium count + High frequency waves
Wrapper gWaterLowCount(31i64, std::move(std::vector<int64_t> {15, 31, 63, 127, 255}));
Wrapper gWaterMediumCount(63i64, std::move(std::vector<int64_t> {15, 31, 63, 127, 255}));
Wrapper gWaterHighMultiplier(0.204f, 0.0f, 0.5f);
Wrapper gWaterHighScaleOne(0.85f, 0.0f, 2.0f);
Wrapper gWaterHighScaleTwo(1.2f, 0.0f, 2.0f);

// Smoke
Wrapper gSmokeNoiseInfluence(0.0f, 0.0f, 1000.0f);
Wrapper gSmokeTrailPower(1.0f, 0.1f, 10.0f);
Wrapper gSmokeTrailAlpha(0.8f, 0.0f, 1.0f);

// Particles

// Debug
Wrapper gDebugTexture(false);
Wrapper gDebugTextureIndex(0.0f, 0.0f, static_cast<float>(shaders::kiMaxDebugTextures - 1));

} // namespace engine
