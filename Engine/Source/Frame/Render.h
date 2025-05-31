#pragma once

#include "Graphics/Managers/PipelineManager.h"

namespace game
{

struct Frame;
struct FrameInput;
struct FrameInputHeld;

}

namespace engine
{

enum CpuCounters;

void RenderFrameGlobal(int64_t iCommandBuffer, const game::Frame& __restrict rFrame);
void RenderFrameMain(int64_t iCommandBuffer, const game::Frame& __restrict rFrame);

XMVECTOR XM_CALLCONV ScreenToWorld(FXMVECTOR vecScreenPos, float fHeight);
void CalculateMatricesAndVisibleArea(const game::Frame& __restrict rFrame, bool bWriteVisibleArea);
void XM_CALLCONV RenderObjects(shaders::ObjectLayout* pLayouts, int64_t iCommandBuffer, int64_t iCount, const XMVECTOR* pVecPositions, const XMVECTOR* pVecDirections, FXMMATRIX matScale, CXMMATRIX matRotation, CpuCounters eCounter, Pipelines ePipeline, Pipelines ePipelineShadow = kPipelineCount);

inline XMMATRIX gMatView {};
inline XMMATRIX gMatPerspective {};

inline XMFLOAT2 gf2VisibleAreaQuadSize {};
inline XMFLOAT4 gf4RenderVisibleArea {};

inline XMFLOAT4 gf4LargeVisibleArea {};

inline XMFLOAT4 gf4VisibleTopLeft {};
inline XMFLOAT4 gf4VisibleTopRight {};
inline XMFLOAT4 gf4VisibleBottomLeft {};
inline XMFLOAT4 gf4VisibleBottomRight {};

float DayPercent(const game::Frame& __restrict rFrame);
float NightPercent(const game::Frame& __restrict rFrame);

} // namespace engine
