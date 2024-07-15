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

DirectX::XMVECTOR XM_CALLCONV ScreenToWorld(DirectX::FXMVECTOR vecScreenPos, float fHeight);
void CalculateMatricesAndVisibleArea(const game::Frame& __restrict rFrame, bool bWriteVisibleArea);
void XM_CALLCONV RenderObjects(shaders::ObjectLayout* pLayouts, int64_t iCommandBuffer, int64_t iCount, const DirectX::XMVECTOR* pVecPositions, const DirectX::XMVECTOR* pVecDirections, DirectX::FXMMATRIX matScale, DirectX::CXMMATRIX matRotation, CpuCounters eCounter, Pipelines ePipeline, Pipelines ePipelineShadow = kPipelineCount);

inline DirectX::XMMATRIX gMatView {};
inline DirectX::XMMATRIX gMatPerspective {};

inline DirectX::XMFLOAT2 gf2VisibleAreaQuadSize {};
inline DirectX::XMFLOAT4 gf4RenderVisibleArea {};

inline DirectX::XMFLOAT4 gf4LargeVisibleArea {};

inline DirectX::XMFLOAT4 gf4VisibleTopLeft {};
inline DirectX::XMFLOAT4 gf4VisibleTopRight {};
inline DirectX::XMFLOAT4 gf4VisibleBottomLeft {};
inline DirectX::XMFLOAT4 gf4VisibleBottomRight {};

float DayPercent(const game::Frame& __restrict rFrame);
float NightPercent(const game::Frame& __restrict rFrame);

} // namespace engine
