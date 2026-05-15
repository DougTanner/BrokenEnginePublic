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
Wrapper gTerrainElevationTextureMultiplier(0.5f, 0.25f, 1.0f);
Wrapper gTerrainColorTextureMultiplier(2.0f, 1.0f, 3.0f);
Wrapper gTerrainNormalTextureMultiplier(2.0f, 1.0f, 3.0f);
Wrapper gTerrainAmbientOcclusionTextureMultiplier(0.5f, 0.25f, 1.0f);

// Smoke
Wrapper gSmokeNoiseInfluence(0.0f, 0.0f, 1000.0f);
Wrapper gSmokeTrailPower(1.0f, 0.1f, 10.0f);
Wrapper gSmokeTrailAlpha(0.8f, 0.0f, 1.0f);

// Particles

// Debug
Wrapper gDebugTexture(false);
Wrapper gDebugTextureIndex(0.0f, 0.0f, static_cast<float>(shaders::kiMaxDebugTextures - 1));

} // namespace engine
