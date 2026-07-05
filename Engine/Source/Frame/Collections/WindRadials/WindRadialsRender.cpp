#include "WindRadials.h"

#if defined(BT_CLIENT)

#include "Ui/GraphicsSettingsWrappersBase.h"
#include "Ui/WrapperBase.h"

namespace engine
{

void WindRadialsInterpolate::GraphicsResources()
{
	gpBufferManager->CreateDynamicBuffer(kCrc, kBufferMain, kName, sizeof(shaders::AxisAlignedQuadLayout));
	gpPipelineManager->mDynamicPipelines.CreatePipelineWindDepositAxisAlignedA(kCrc, kName, sizeof(shaders::AxisAlignedQuadLayout));
	gpPipelineManager->mDynamicPipelines.CreatePipelineWindDepositAxisAlignedB(kCrc, kName);
}

static int64_t siRendered = 0;

void WindRadialsInterpolate::BeginRender([[maybe_unused]] int64_t iCommandBuffer, const std::unordered_map<GridCoord, game::FrameInterpolate>& rRenderInterpolates, const std::vector<GridCoord>& rActiveCoords)
{
	siRendered = 0;

	if (!gWindEnabled.Get<bool>())
	{
		return;
	}

	int64_t iTotalCapacity = AccumulateRenderCapacity(rRenderInterpolates, rActiveCoords,
		[](const game::FrameInterpolate& rInterpolate) -> const auto& { return rInterpolate.windRadials; });

	if (iTotalCapacity == 0)
	{
		return;
	}

	if (Buffer* pBuffer = gpBufferManager->ResizeDynamicBufferIfNeeded(kCrc, kBufferMain, kName, sizeof(shaders::AxisAlignedQuadLayout), iTotalCapacity, iCommandBuffer))
	{
		gpPipelineManager->mDynamicPipelines.mPipelineMaps[kDynamicPipelineWindDepositAxisAlignedA].at(kCrc)->UpdateStorageBufferDescriptor(iCommandBuffer, 1, pBuffer);
		gpPipelineManager->mDynamicPipelines.mPipelineMaps[kDynamicPipelineWindDepositAxisAlignedB].at(kCrc)->UpdateStorageBufferDescriptor(iCommandBuffer, 1, pBuffer);
	}
}

void WindRadialsInterpolate::Render([[maybe_unused]] const game::FrameInterpolate& __restrict rFrameInterpolate, [[maybe_unused]] int64_t iCommandBuffer)
{
	const WindRadialsInterpolate& rCurrent = rFrameInterpolate.windRadials;

	if (!gWindEnabled.Get<bool>() || rCurrent.iCount == 0)
	{
		return;
	}

	auto [pLayouts, iBufferCapacity] = gpBufferManager->GetDynamicStorageBuffer<shaders::AxisAlignedQuadLayout>(kCrc, kBufferMain, iCommandBuffer);
	ASSERT(siRendered + rCurrent.iCount <= iBufferCapacity);

	for (int64_t i = 0; i < rCurrent.iCount; ++i)
	{
		// Load
		XMVECTOR vecPosition = rCurrent.pVecPositions[i];
		float fIntensity = rCurrent.pfIntensities[i];
		float fSize = rCurrent.pfSizes[i];

		// Visibility culling
		XMFLOAT4A f4Position {};
		if (!IsPointVisible(vecPosition, f4Position))
		{
			continue;
		}

		// Project to base height
		XMVECTOR vecBasePosition = ProjectToBaseHeight(vecPosition);
		XMStoreFloat4A(&f4Position, vecBasePosition);

		// Build AxisAlignedQuadLayout
		// Per-quad params: {intensity, 0, 0, 1} where .w = 1.0 is the radial flag for WindDeposit.frag
		XMFLOAT4A f4Params = {fIntensity, 0.0f, 0.0f, 1.0f};
		BuildAxisAlignedQuad(pLayouts[siRendered], f4Position, fSize, f4Params, 0xFFFFFFFF);

		++siRendered;
	}
}

void WindRadialsInterpolate::EndRender([[maybe_unused]] int64_t iCommandBuffer)
{
	gpPipelineManager->mDynamicPipelines.mPipelineMaps[kDynamicPipelineWindDepositAxisAlignedA].at(kCrc)->WriteIndirectBuffer(iCommandBuffer, giWindTextureIndex == 0 ? siRendered : 0);
	gpPipelineManager->mDynamicPipelines.mPipelineMaps[kDynamicPipelineWindDepositAxisAlignedB].at(kCrc)->WriteIndirectBuffer(iCommandBuffer, giWindTextureIndex == 1 ? siRendered : 0);
}

} // namespace engine

#endif // BT_CLIENT
