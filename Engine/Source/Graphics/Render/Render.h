#pragma once

#if defined(BT_CLIENT)

namespace game
{

struct FrameInterpolate;

}

namespace engine
{

void RenderFrameGlobal(int64_t iCommandBuffer, float fCurrentTime);
void RenderFrameMain(int64_t iCommandBuffer, const std::unordered_map<GridCoord, game::FrameInterpolate>& rRenderInterpolates, const std::vector<GridCoord>& rActiveCoords, GridCoord cameraCoord);

// Shadow
inline bool gbShadowTemporalReset = false; // Set by CreateShadowTextures; re-arms the PopulateShadowParameters first-frame guard so a recreate doesn't blend stale history for one frame
inline int64_t giShadowActivePixelsX = 0; // Profile GPU-screen readout: ray-marched sub-window width in texels (PopulateShadowParameters)
inline int64_t giShadowActivePixelsY = 0; // Profile GPU-screen readout: ray-marched sub-window height in texels

// Lighting
inline bool gbLightingTemporalReset = false; // Set by CreateLightingTextures; re-arms the PopulateLightingParameters first-frame guard so a recreate doesn't blend stale history for one frame
inline int64_t giLightingDepositPixelsX = 0; // Profile GPU-screen readout: full deposit texture width in texels (deposit is not windowed)
inline int64_t giLightingDepositPixelsY = 0; // Profile GPU-screen readout: full deposit texture height in texels
inline int64_t giLightingSpreadStartActivePixelsX = 0; // Profile GPU-screen readout: cropped on-screen window width in start-pass (gSpreadTextureMultiplierStart) spread texels
inline int64_t giLightingSpreadStartActivePixelsY = 0; // Profile GPU-screen readout: cropped on-screen window height in start-pass spread texels
inline int64_t giLightingSpreadEndActivePixelsX = 0; // Profile GPU-screen readout: cropped on-screen window width in end-pass (gSpreadTextureMultiplierEnd) spread texels
inline int64_t giLightingSpreadEndActivePixelsY = 0; // Profile GPU-screen readout: cropped on-screen window height in end-pass spread texels
void RenderLightingGlobal(int64_t iCommandBuffer);
void RenderLightingMain(int64_t iCommandBuffer);

// Smoke
inline bool gbSmokeClear = true;

void RenderSmokeGlobal(int64_t iCommandBuffer);

// Wind
inline int64_t giWindTextureIndex = 0; // 0 = write TextureOne, 1 = write TextureTwo

void RenderWindGlobal(int64_t iCommandBuffer);

} // namespace engine

#endif // defined(BT_CLIENT)
