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

inline bool XM_CALLCONV InVisibleArea(XMFLOAT4 f4VisibleArea, XMFLOAT4 f4Position, float fAdjustLeft = 0.0f, float fAdjustRight = 0.0f, float fAdjustTop = 0.0f, float fAdjustBottom = 0.0f)
{
	return !(f4Position.x < f4VisibleArea.x - fAdjustLeft || f4Position.x > f4VisibleArea.z + fAdjustRight || f4Position.y > f4VisibleArea.y + fAdjustTop || f4Position.y < f4VisibleArea.w - fAdjustBottom);
}

inline bool XM_CALLCONV InVisibleArea(XMFLOAT4 f4VisibleArea, FXMVECTOR vecPosition, float fAdjustLeft = 0.0f, float fAdjustRight = 0.0f, float fAdjustTop = 0.0f, float fAdjustBottom = 0.0f)
{
	XMFLOAT4A f4Position {};
	XMStoreFloat4A(&f4Position, vecPosition);
	return InVisibleArea(f4VisibleArea, f4Position, fAdjustLeft, fAdjustRight, fAdjustTop, fAdjustBottom);
}

} // namespace engine
