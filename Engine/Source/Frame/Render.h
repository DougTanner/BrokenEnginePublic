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

void RenderFrameGlobal(int64_t iCommandBuffer);
void RenderFrameMain(int64_t iCommandBuffer, const game::FrameInterpolate& rFrameInterpolate);

void XM_CALLCONV RenderObjects(shaders::ObjectLayout* pLayouts, int64_t iCommandBuffer, int64_t iCount, const XMVECTOR* pVecPositions, const XMVECTOR* pVecDirections, FXMMATRIX matScale, CXMMATRIX matRotation, CpuCounters eCounter, Pipelines ePipeline, Pipelines ePipelineShadow = kPipelineCount);

float DayPercent();
float NightPercent();

// Lighting
XMVECTOR XM_CALLCONV DirectionToDirectionMultipliers(FXMVECTOR vecDirection);
void RenderLightingGlobal(int64_t iCommandBuffer);
void RenderLightingMain(int64_t iCommandBuffer, const game::FrameInterpolate& rFrameInterpolate);

// Smoke
inline bool gbSmokeClear = true;
inline bool gbSmokeSpread = false;

void RenderSmokeGlobal(int64_t iCommandBuffer);

// Wind
inline bool gbWindClear = false;

void RenderWindGlobal(int64_t iCommandBuffer);

// Shared rendering helpers for lighting and smoke collections
bool IsPointVisible(XMVECTOR vecPosition, XMFLOAT4A& rOutPosition);
XMVECTOR ProjectToBaseHeight(XMVECTOR vecPosition);
void BuildAxisAlignedQuad(shaders::AxisAlignedQuadLayout& rLayout, const XMFLOAT4A& f4Position, float fArea, const XMFLOAT4A& f4Params, uint32_t uiColor);

} // namespace engine
