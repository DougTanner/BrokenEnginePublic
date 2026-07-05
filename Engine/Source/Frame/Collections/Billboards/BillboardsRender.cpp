#include "Billboards.h"

#if defined(BT_CLIENT)

#include "Profile/ProfileManager.h"

namespace engine
{

using enum BillboardFlags;

void BillboardsInterpolate::GraphicsResources()
{
	gpBufferManager->CreateDynamicBuffer(kCrc, kBufferMain, kName, sizeof(shaders::BillboardLayout));
	gpPipelineManager->mDynamicPipelines.CreatePipelineBillboards(kCrc, kName, sizeof(shaders::BillboardLayout));
}

static int64_t siRendered = 0;
static int64_t siTotalCount = 0;

void BillboardsInterpolate::BeginRender([[maybe_unused]] int64_t iCommandBuffer, const std::unordered_map<GridCoord, game::FrameInterpolate>& rRenderInterpolates, const std::vector<GridCoord>& rActiveCoords)
{
	siRendered = 0;
	siTotalCount = 0;

	int64_t iTotalCapacity = AccumulateRenderCapacity(rRenderInterpolates, rActiveCoords,
		[](const game::FrameInterpolate& rInterpolate) -> const auto& { return rInterpolate.billboards; });

	if (iTotalCapacity == 0)
	{
		return;
	}

	if (Buffer* pBuffer = gpBufferManager->ResizeDynamicBufferIfNeeded(kCrc, kBufferMain, kName, sizeof(shaders::BillboardLayout), iTotalCapacity, iCommandBuffer))
	{
		gpPipelineManager->mDynamicPipelines.mPipelineMaps[kDynamicPipelineBillboards].at(kCrc)->UpdateStorageBufferDescriptor(iCommandBuffer, 2, pBuffer);
	}
}

void BillboardsInterpolate::Render([[maybe_unused]] const game::FrameInterpolate& __restrict rFrameInterpolate, [[maybe_unused]] int64_t iCommandBuffer)
{
	const BillboardsInterpolate& rCurrent = rFrameInterpolate.billboards;
	siTotalCount += rCurrent.iCount;

	if (rCurrent.iCount == 0)
	{
		return;
	}

	auto [pLayouts, iBufferCapacity] = gpBufferManager->GetDynamicStorageBuffer<shaders::BillboardLayout>(kCrc, kBufferMain, iCommandBuffer);
	ASSERT(siRendered + rCurrent.iCount <= iBufferCapacity);

	for (int64_t i = 0; i < rCurrent.iCount; ++i)
	{
		// Load
		uint8_t uiTypeIndex = rCurrent.puiTypeIndices[i];
		BillboardFlags_t flags = rCurrent.pFlags[i];
		float fRotation = rCurrent.pfRotations[i];
		float fExtra = rCurrent.pfExtra[i];
		XMVECTOR vecPosition = rCurrent.pVecPositions[i];

		const BillboardsInterpolate::Type& rType = BillboardsInterpolate::sTypes.at(uiTypeIndex);

		// Project world position to clip space
		XMVECTOR vecProjection = XMVector4Transform(vecPosition, XMMatrixMultiply(game::gpCamera->mMatView, game::gpCamera->mMatPerspective));

		XMFLOAT4A f4Position {};
		XMStoreFloat4A(&f4Position, vecProjection);
		f4Position.x /= f4Position.w;
		f4Position.y /= f4Position.w;
		f4Position.z /= f4Position.w;
		f4Position.w = 1.0f;

		// Handle offscreen-only billboards (like offscreen indicators)
		if (flags & kOffscreenOnly && !(f4Position.x < -1.0f - fExtra || f4Position.x > 1.0f + fExtra || f4Position.y > 1.0f + fExtra || f4Position.y < -1.0f - fExtra))
		{
			continue;
		}

		float fSize = rType.fSize;

		if (flags & kOffscreenOnly)
		{
			f4Position.x = std::clamp(f4Position.x, -1.0f + fSize / gpSwapchainManager->mfAspectRatio, 1.0f - fSize / gpSwapchainManager->mfAspectRatio);
			f4Position.y = std::clamp(f4Position.y, -1.0f + fSize, 1.0f - fSize);
		}

		// Calculate rotation for offscreen rotate flag
		// RotationFromPosition now returns the signed atan2 heading, so the prior manual hemisphere flip is folded
		// into a single negation (3*pi/2 - theta), preserving the original orientation (mod 2*pi)
		if (flags & kOffscreenRotate)
		{
			fRotation = XM_PI + XM_PIDIV2 - common::RotationFromPosition(XMVector3Normalize(XMLoadFloat4A(&f4Position)));
		}

		// Populate GPU layout
		shaders::BillboardLayout& rBillboardLayout = pLayouts[siRendered];
		rBillboardLayout.f4Position = f4Position;
		rBillboardLayout.fSize = fSize;
		rBillboardLayout.fTextureIndex = static_cast<float>(gpTextureManager->mTextureDescriptors.CrcToIndex(rType.crc));
		rBillboardLayout.fRotation = fRotation;
		rBillboardLayout.fAlpha = rType.fAlpha;

		++siRendered;
	}
}

void BillboardsInterpolate::EndRender([[maybe_unused]] int64_t iCommandBuffer)
{
	gpProfileManager->SetCount(kCpuCounterBillboards, siTotalCount);
	gpProfileManager->SetCount(kCpuCounterBillboardsRendered, siRendered);

	gpPipelineManager->mDynamicPipelines.mPipelineMaps[kDynamicPipelineBillboards].at(kCrc)->WriteIndirectBuffer(iCommandBuffer, siRendered);
}

} // namespace engine

#endif // BT_CLIENT
