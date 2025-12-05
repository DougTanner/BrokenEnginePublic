#pragma once

#include "Graphics/Managers/PipelineManager.h"

namespace game
{

struct Frame;
struct FrameInput;
struct FrameInterpolate;

}

namespace engine
{

enum CpuCounters;

// DT: TODO RenderFrameGlobal should not use rFrame? Gets everything from Camera now
//          Except maybe need to move out fSunAngle into camera too, it's more global than a single frame anyway
void RenderFrameGlobal(int64_t iCommandBuffer, const game::FrameInterpolate& rFrameInterpolate);
void RenderFrameMain(int64_t iCommandBuffer, const game::FrameInterpolate& rFrameInterpolate);

void XM_CALLCONV RenderObjects(shaders::ObjectLayout* pLayouts, int64_t iCommandBuffer, int64_t iCount, const XMVECTOR* pVecPositions, const XMVECTOR* pVecDirections, FXMMATRIX matScale, CXMMATRIX matRotation, CpuCounters eCounter, Pipelines ePipeline, Pipelines ePipelineShadow = kPipelineCount);

float DayPercent(const game::FrameInterpolate& __restrict rFrameInterpolate);
float NightPercent(const game::FrameInterpolate& __restrict rFrameInterpolate);

// Lighting
XMVECTOR XM_CALLCONV DirectionToDirectionMultipliers(FXMVECTOR vecDirection);
void RenderLightingGlobal(int64_t iCommandBuffer);
void RenderLightingMain(int64_t iCommandBuffer, const game::FrameInterpolate& rFrameInterpolate);

// Smoke
inline bool gbSmokeClear = true;
inline float gbSmokeSpread = false;

void RenderSmokeGlobal(int64_t iCommandBuffer, const game::FrameInterpolate& __restrict rFrameInterpolate);

} // namespace engine
