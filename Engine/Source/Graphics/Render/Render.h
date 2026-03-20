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

// Shared spread quad helper (smoke/wind)
inline void WriteSpreadQuad(const XMFLOAT4& rPreviousArea, const XMFLOAT4& rCurrentArea, shaders::AxisAlignedQuadLayout& rQuad)
{
	float fXOffset = (rPreviousArea.x - rCurrentArea.x) / (rPreviousArea.z - rCurrentArea.x);
	float fYOffset = (rPreviousArea.y - rCurrentArea.y) / (rPreviousArea.w - rCurrentArea.y);
	rQuad.f4VertexRect = {-1.0f + 2.0f * fXOffset, 1.0f - 2.0f * fYOffset, 2.0f, -2.0f};
	rQuad.f4TextureRect = {0.0f, 0.0f, 1.0f, 1.0f};
	rQuad.f4Params = {};
}

// Wind
inline bool gbWindClear = false;
inline int64_t giWindTextureIndex = 0; // 0 = write TextureOne, 1 = write TextureTwo

void RenderWindGlobal(int64_t iCommandBuffer);

} // namespace engine

#endif // defined(BT_CLIENT)
