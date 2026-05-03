#pragma once

#if defined(BT_CLIENT)

namespace game
{

struct Frame;
struct FrameInput;
struct FrameInterpolate;

}

namespace engine
{

enum CpuCounters;

void RenderFrameGlobal(int64_t iCommandBuffer, float fCurrentTime, int64_t iTick);
void RenderFrameMain(int64_t iCommandBuffer, const std::unordered_map<GridCoord, game::FrameInterpolate>& rRenderInterpolates, const std::vector<GridCoord>& rActiveCoords, GridCoord cameraCoord);

// Lighting
void RenderLightingGlobal(int64_t iCommandBuffer);
void RenderLightingMain(int64_t iCommandBuffer);

// Smoke
inline bool gbSmokeClear = true;

void RenderSmokeGlobal(int64_t iCommandBuffer);

// Wind
inline bool gbWindClear = false;
inline int64_t giWindTextureIndex = 0; // 0 = write TextureOne, 1 = write TextureTwo

void RenderWindGlobal(int64_t iCommandBuffer);

// DT: TEMP - sun/moon lighting debug snapshot, captured each frame in RenderFrameGlobal, displayed by HudScreen.
inline float gDebugSunMoonAngle = 0.0f;
inline XMFLOAT4 gDebugSunMoonNormal {};
inline XMFLOAT4 gDebugSunMoonColor {};
inline XMFLOAT4 gDebugAmbientColor {};
inline float gDebugSunMoonDayPercent = 0.0f;
inline float gDebugSunMoonNoonPercent = 0.0f;
inline float gDebugSunMoonShadowMoonMultiplier = 1.0f;
inline float gDebugSunMoonLightingWaterMoonBrightness = 1.0f;
inline float gDebugSunMoonWaterSunVisibility = 0.0f;
inline float gDebugSunMoonWaterDirectional = 0.0f;
inline float gDebugSunMoonTerrainSunBrightness = 0.0f;

} // namespace engine

#endif // defined(BT_CLIENT)
