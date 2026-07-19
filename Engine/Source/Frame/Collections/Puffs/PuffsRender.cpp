#include "Puffs.h"

#if defined(BT_CLIENT)

#include "Profile/ProfileManager.h"
#include "Ui/GraphicsSettingsWrappersBase.h"

namespace engine
{

void PuffsInterpolate::GraphicsResources()
{
	gpBufferManager->CreateDynamicBuffer(kCrc, kBufferMain, kName, sizeof(shaders::AxisAlignedQuadLayout));
	gpPipelineManager->mDynamicPipelines.CreatePipelineSmokeAxisAligned(kCrc, kName, sizeof(shaders::AxisAlignedQuadLayout));
}

static int64_t siRendered = 0;
static int64_t siTotalCount = 0;

void PuffsInterpolate::BeginRender([[maybe_unused]] int64_t iCommandBuffer, const std::unordered_map<GridCoord, game::FrameInterpolate>& rRenderInterpolates, const std::vector<GridCoord>& rActiveCoords)
{
	siRendered = 0;
	siTotalCount = 0;

	int64_t iTotalCapacity = AccumulateRenderCapacity(rRenderInterpolates, rActiveCoords,
		[](const game::FrameInterpolate& rInterpolate) -> const auto& { return rInterpolate.puffs; });

	if (iTotalCapacity == 0)
	{
		return;
	}

	if (Buffer* pBuffer = gpBufferManager->ResizeDynamicBufferIfNeeded(kCrc, kBufferMain, kName, sizeof(shaders::AxisAlignedQuadLayout), iTotalCapacity, iCommandBuffer))
	{
		gpPipelineManager->mDynamicPipelines.mPipelineMaps[kDynamicPipelineSmokeAxisAligned].at(kCrc)->UpdateStorageBufferDescriptor(iCommandBuffer, 1, pBuffer);
	}
}

void PuffsInterpolate::Render([[maybe_unused]] const game::FrameInterpolate& __restrict rFrameInterpolate, [[maybe_unused]] int64_t iCommandBuffer)
{
	const PuffsInterpolate& rCurrent = rFrameInterpolate.puffs;
	siTotalCount += rCurrent.iCount;

	if (rCurrent.iCount == 0)
	{
		return;
	}

	auto [pPuffsLayouts, iBufferCapacity] = gpBufferManager->GetDynamicStorageBuffer<shaders::AxisAlignedQuadLayout>(kCrc, kBufferMain, iCommandBuffer);
	ASSERT(siRendered + rCurrent.iCount <= iBufferCapacity);

	for (int64_t i = 0; i < rCurrent.iCount; ++i)
	{
		// Load
		XMVECTOR vecPosition = rCurrent.pVecPositions[i];
		const PuffsType& rType = PuffsInterpolate::GetType(rCurrent.puiTypeIndices[i]);
		float fIntensity = rCurrent.pfIntensities[i];
		float fArea = rCurrent.pfAreas[i];
		float fRotation = rCurrent.pfRotations[i];

		// Visibility culling
		XMFLOAT4A f4Position {};
		if (!IsPointVisible(vecPosition, f4Position))
		{
			continue;
		}

		// Project to base height
		XMStoreFloat4A(&f4Position, ProjectToBaseHeight(vecPosition));

		// Build AxisAlignedQuadLayout
		XMFLOAT4A f4Params {};
		f4Params.x = fIntensity;  // Smoke.frag uses this as intensity multiplier
		f4Params.y = fIntensity;  // Smoke.frag uses pow(f4Params.y, globalLayout.fSmokeIntensityFalloff)
		f4Params.w = fRotation;   // Smoke.frag uses this for Rotate()
		BuildAxisAlignedQuad(pPuffsLayouts[siRendered], f4Position, fArea, f4Params, rType.uiColor);

		++siRendered;
	}
}

void PuffsInterpolate::EndRender([[maybe_unused]] int64_t iCommandBuffer)
{
	gpProfileManager->SetCount(kCpuCounterPuffs, siTotalCount);
	gpProfileManager->SetCount(kCpuCounterPuffsRendered, siRendered);
	gpPipelineManager->mDynamicPipelines.mPipelineMaps[kDynamicPipelineSmokeAxisAligned].at(kCrc)->WriteIndirectBuffer(iCommandBuffer, gSmokeEnabled.Get<bool>() ? siRendered : 0);
}

} // namespace engine

#endif // BT_CLIENT
