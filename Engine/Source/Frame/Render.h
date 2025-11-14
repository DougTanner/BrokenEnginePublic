#pragma once

#include "Graphics/Managers/PipelineManager.h"

namespace game
{

struct Frame;
struct FrameInputHeld;
struct FrameInputPressed;

}

namespace engine
{

enum CpuCounters;

void RenderFrameGlobal(int64_t iCommandBuffer, const game::Frame& __restrict rFrame);
void RenderFrameMain(int64_t iCommandBuffer, const game::Frame& __restrict rFrame);

void XM_CALLCONV RenderObjects(shaders::ObjectLayout* pLayouts, int64_t iCommandBuffer, int64_t iCount, const XMVECTOR* pVecPositions, const XMVECTOR* pVecDirections, FXMMATRIX matScale, CXMMATRIX matRotation, CpuCounters eCounter, Pipelines ePipeline, Pipelines ePipelineShadow = kPipelineCount);

float DayPercent(const game::Frame& __restrict rFrame);
float NightPercent(const game::Frame& __restrict rFrame);

} // namespace engine
