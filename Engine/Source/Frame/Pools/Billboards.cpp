#include "Billboards.h"

#include "Frame/Render.h"
#include "Graphics/Managers/BufferManager.h"
#include "Graphics/Managers/SwapchainManager.h"
#include "Graphics/Managers/TextureManager.h"

#include "Frame/Frame.h"

using namespace DirectX;

namespace engine
{

using enum BillboardFlags;

void Billboards::RenderMain([[maybe_unused]] int64_t iCommandBuffer, [[maybe_unused]] const game::Frame& __restrict rFrame)
{
	const Billboards& rCurrent = rFrame.billboards;

	auto pLayouts = reinterpret_cast<shaders::BillboardLayout*>(gpBufferManager->mBillboardsStorageBuffers.at(iCommandBuffer).mpMappedMemory);

	int64_t iCount = 0;
	int64_t iRendered = 0;
	for (decltype(rCurrent.uiMaxIndex) i = 0; i <= rCurrent.uiMaxIndex; ++i)
	{
		if (!rCurrent.pbUsed[i])
		{
			continue;
		}

		const BillboardInfo& rBillboardInfo = rCurrent.pObjectInfos[i];
		
		++iCount;

		auto vecPosition = rBillboardInfo.vecPosition;

		auto vecProjection = XMVector4Transform(vecPosition, XMMatrixMultiply(gMatView, gMatPerspective));

		XMFLOAT4A f4Position {0.0f, 0.0f, 0.0f, 1.0f};
		XMStoreFloat4A(&f4Position, vecProjection);
		f4Position.x /= f4Position.w;
		f4Position.y /= f4Position.w;
		f4Position.z /= f4Position.w;
		f4Position.w /= f4Position.w;

		if (rBillboardInfo.flags & kOffscreenOnly && !(f4Position.x < -1.0f - rBillboardInfo.fExtra || f4Position.x > 1.0f + rBillboardInfo.fExtra || f4Position.y > 1.0f + rBillboardInfo.fExtra || f4Position.y < -1.0f - rBillboardInfo.fExtra))
		{
			continue;
		}

		float fSize = rBillboardInfo.fSize;
		
		if (rBillboardInfo.flags & kOffscreenOnly)
		{
			f4Position.x = std::clamp(f4Position.x, -1.0f + fSize / gpSwapchainManager->mfAspectRatio, 1.0f - fSize / gpSwapchainManager->mfAspectRatio);
			f4Position.y = std::clamp(f4Position.y, -1.0f + fSize, 1.0f - fSize);
		}

		float fRotation = rBillboardInfo.fRotation;
		if (rBillboardInfo.flags & kOffscreenRotate)
		{
			fRotation = XM_PI + XM_PIDIV2 + common::RotationFromPosition(XMVector3Normalize(XMLoadFloat4A(&f4Position)));
			if (f4Position.y > 0.0f)
			{
				fRotation = XM_PI - fRotation;
			}
		}
			
		shaders::BillboardLayout& rBillboardLayout = pLayouts[iRendered];
		rBillboardLayout.f4Position = f4Position;
		rBillboardLayout.f4Misc = {fSize, CrcToIndex(rBillboardInfo.crc), fRotation, rBillboardInfo.fAlpha};

		++iRendered;
	}
	PROFILE_SET_COUNT(kCpuCounterBillboards, iCount);
	PROFILE_SET_COUNT(kCpuCounterBillboardsRendered, iRendered);

#if defined(ENABLE_RECORDING)
	gpPipelineManager->mpPipelines[kPipelineBillboards].WriteIndirectBuffer(iCommandBuffer, 0);
#else
	gpPipelineManager->mpPipelines[kPipelineBillboards].WriteIndirectBuffer(iCommandBuffer, iRendered);
#endif
}

} // namespace engine
