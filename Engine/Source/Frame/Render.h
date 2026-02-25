#pragma once

#ifdef BT_CLIENT

#include "Graphics/Managers/PipelineManager.h"
#include "Frame/GridCoord.h"

namespace game
{

struct Frame;
struct FrameInput;
struct FrameInterpolate;

}

namespace engine
{

enum CpuCounters;

void RenderFrameGlobal(int64_t iCommandBuffer, float fCurrentTime, int64_t iFrame);
void RenderFrameMain(int64_t iCommandBuffer, const std::unordered_map<GridCoord, game::FrameInterpolate>& rRenderInterpolates, const std::vector<GridCoord>& rActiveCoords, GridCoord cameraCoord, const std::unordered_map<GridCoord, std::unique_ptr<game::Frame>>& rCurrentFrames);

void XM_CALLCONV RenderObjects(shaders::ObjectLayout* pLayouts, int64_t iCommandBuffer, int64_t iCount, const XMVECTOR* pVecPositions, const XMVECTOR* pVecDirections, FXMMATRIX matScale, CXMMATRIX matRotation, CpuCounters eCounter, Pipelines ePipeline, Pipelines ePipelineShadow = kPipelineCount);

float DayPercent();
float NightPercent();

// Lighting
XMVECTOR XM_CALLCONV DirectionToDirectionMultipliers(FXMVECTOR vecDirection);
void RenderLightingGlobal(int64_t iCommandBuffer);
void RenderLightingMain(int64_t iCommandBuffer, const game::FrameInterpolate& rFrameInterpolate);

// Smoke
inline bool gbSmokeClear = true;

void RenderSmokeGlobal(int64_t iCommandBuffer);

// Wind
inline bool gbWindClear = false;
inline int giWindTextureIndex = 0; // 0 = write TextureOne, 1 = write TextureTwo

void RenderWindGlobal(int64_t iCommandBuffer);

} // namespace engine

#endif // BT_CLIENT
