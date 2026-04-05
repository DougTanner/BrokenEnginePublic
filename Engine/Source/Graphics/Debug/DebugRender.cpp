#include "DebugRender.h"

#if defined(BT_CLIENT)

namespace engine
{

static constexpr int64_t kiMaxDebugRender = 4096;

struct DebugRenderType
{
	common::crc_t crc;
	Pipelines ePipeline;
	int64_t iCount;
	shaders::DebugRenderLayout pLayouts[kiMaxDebugRender];
};

static DebugRenderType sTypes[]
{
	{common::CrcConsteval("DebugBox"),    kPipelineDebugBox,    0, {}},
	{common::CrcConsteval("DebugSphere"), kPipelineDebugSphere, 0, {}},
	{common::CrcConsteval("DebugCircle"), kPipelineDebugCircle, 0, {}},
	{common::CrcConsteval("DebugLine"),   kPipelineDebugLine,   0, {}},
};

static constexpr int64_t kiBox = 0;
static constexpr int64_t kiSphere = 1;
static constexpr int64_t kiCircle = 2;
static constexpr int64_t kiLine = 3;

static void AddLayout(int64_t iType, const XMFLOAT4A& f4Row0, const XMFLOAT4A& f4Row1, const XMFLOAT4A& f4Row2, const XMFLOAT4A& f4Color)
{
	if constexpr (!kbDebugRender) return;

	DebugRenderType& rType = sTypes[iType];
	ASSERT(rType.iCount < kiMaxDebugRender);

	shaders::DebugRenderLayout& rLayout = rType.pLayouts[rType.iCount];
	rLayout.f3x4Transform[0] = f4Row0;
	rLayout.f3x4Transform[1] = f4Row1;
	rLayout.f3x4Transform[2] = f4Row2;
	rLayout.f4Color = f4Color;
	++rType.iCount;
}

void DebugRender::Box(const XMFLOAT3A& f3Position, const XMFLOAT3A& f3Scale, const XMFLOAT4A& f4Color)
{
	if constexpr (!kbDebugRender) return;

	// Row-major 3x4: scale on diagonal, translation in w
	AddLayout(kiBox,
		{f3Scale.x, 0.0f, 0.0f, f3Position.x},
		{0.0f, f3Scale.y, 0.0f, f3Position.y},
		{0.0f, 0.0f, f3Scale.z, f3Position.z},
		f4Color);
}

void DebugRender::Sphere(const XMFLOAT3A& f3Center, float fRadius, const XMFLOAT4A& f4Color)
{
	if constexpr (!kbDebugRender) return;

	AddLayout(kiSphere,
		{fRadius, 0.0f, 0.0f, f3Center.x},
		{0.0f, fRadius, 0.0f, f3Center.y},
		{0.0f, 0.0f, fRadius, f3Center.z},
		f4Color);
}

void DebugRender::Circle(const XMFLOAT3A& f3Center, float fRadius, const XMFLOAT4A& f4Color)
{
	if constexpr (!kbDebugRender) return;

	AddLayout(kiCircle,
		{fRadius, 0.0f, 0.0f, f3Center.x},
		{0.0f, fRadius, 0.0f, f3Center.y},
		{0.0f, 0.0f, fRadius, f3Center.z},
		f4Color);
}

void DebugRender::Line(const XMFLOAT3A& f3Start, const XMFLOAT3A& f3End, const XMFLOAT4A& f4Color)
{
	if constexpr (!kbDebugRender) return;

	// Line mesh is (0,0,0) to (1,0,0) along +X
	// Transform: X axis = direction, translation = start
	float fDx = f3End.x - f3Start.x;
	float fDy = f3End.y - f3Start.y;
	float fDz = f3End.z - f3Start.z;

	AddLayout(kiLine,
		{fDx, 0.0f, 0.0f, f3Start.x},
		{fDy, 0.0f, 0.0f, f3Start.y},
		{fDz, 0.0f, 0.0f, f3Start.z},
		f4Color);
}

void DebugRender::BeginRender(int64_t iCommandBuffer)
{
	if constexpr (!kbDebugRender) return;

	for (DebugRenderType& rType : sTypes)
	{
		if (rType.iCount == 0)
		{
			continue;
		}

		if (Buffer* pBuffer = gpBufferManager->ResizeDynamicBufferIfNeeded(rType.crc, kBufferMain, "DebugRender", sizeof(shaders::DebugRenderLayout), rType.iCount, iCommandBuffer))
		{
			gpPipelineManager->mpPipelines[rType.ePipeline].UpdateStorageBufferDescriptor(iCommandBuffer, 2, pBuffer);
		}

		auto [pLayouts, iBufferCapacity] = gpBufferManager->GetDynamicStorageBuffer<shaders::DebugRenderLayout>(rType.crc, kBufferMain, iCommandBuffer);
		memcpy(pLayouts, rType.pLayouts, rType.iCount * sizeof(shaders::DebugRenderLayout));
	}
}

void DebugRender::EndRender(int64_t iCommandBuffer)
{
	if constexpr (!kbDebugRender) return;

	for (DebugRenderType& rType : sTypes)
	{
		gpPipelineManager->mpPipelines[rType.ePipeline].WriteIndirectBuffer(iCommandBuffer, rType.iCount);
		rType.iCount = 0;
	}
}

} // namespace engine

#endif // BT_CLIENT
