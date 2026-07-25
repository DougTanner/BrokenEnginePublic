#include "PointLights.h"

#if defined(BT_CLIENT)

#include "Profile/ProfileManager.h"
#include "Ui/LightingWrappersBase.h"

namespace engine
{

void PointLightsInterpolate::GraphicsResources()
{
	gpBufferManager->CreateDynamicBuffer(kCrc, kBufferMain, kName, sizeof(shaders::AxisAlignedQuadLayout));
	gpPipelineManager->mDynamicPipelines.CreatePipelineAxisAlignedLighting(kCrc, kName, sizeof(shaders::AxisAlignedQuadLayout));
	Buffer* pVisibleLightsBuffers = gpBufferManager->CreateDynamicBuffer(kCrc, kBufferVisibleLights, kName, sizeof(shaders::VisibleLightQuadLayout));
	gpPipelineManager->mDynamicPipelines.CreatePipelineVisibleLights(kCrc, kName, pVisibleLightsBuffers);
}

static int64_t siRendered = 0;
static int64_t siTotalCount = 0;

void PointLightsInterpolate::BeginRender([[maybe_unused]] int64_t iCommandBuffer, const std::unordered_map<GridCoord, game::FrameInterpolate>& rRenderInterpolates, const std::vector<GridCoord>& rActiveCoords)
{
	siRendered = 0;
	siTotalCount = 0;

	int64_t iTotalCapacity = AccumulateRenderCapacity(rRenderInterpolates, rActiveCoords,
		[](const game::FrameInterpolate& rInterpolate) -> const auto& { return rInterpolate.pointLights; });

	if (iTotalCapacity == 0)
	{
		return;
	}

	if (Buffer* pBuffer = gpBufferManager->ResizeDynamicBufferIfNeeded(kCrc, kBufferMain, kName, sizeof(shaders::AxisAlignedQuadLayout), iTotalCapacity, iCommandBuffer))
	{
		gpPipelineManager->mDynamicPipelines.mPipelineMaps[kDynamicPipelineAxisAlignedLighting].at(kCrc)->UpdateStorageBufferDescriptor(iCommandBuffer, 1, pBuffer);
	}
	if (Buffer* pBuffer = gpBufferManager->ResizeDynamicBufferIfNeeded(kCrc, kBufferVisibleLights, kName, sizeof(shaders::VisibleLightQuadLayout), iTotalCapacity, iCommandBuffer))
	{
		gpPipelineManager->mDynamicPipelines.mPipelineMaps[kDynamicPipelineVisibleLights].at(kCrc)->UpdateStorageBufferDescriptor(iCommandBuffer, 2, pBuffer);
	}
}

void PointLightsInterpolate::Render([[maybe_unused]] const game::FrameInterpolate& __restrict rFrameInterpolate, [[maybe_unused]] int64_t iCommandBuffer)
{
	const PointLightsInterpolate& rCurrent = rFrameInterpolate.pointLights;
	siTotalCount += rCurrent.iCount;

	if (rCurrent.iCount == 0)
	{
		return;
	}

	auto [pPointLightsLayouts, iPointLightsBufferCapacity] = gpBufferManager->GetDynamicStorageBuffer<shaders::AxisAlignedQuadLayout>(kCrc, kBufferMain, iCommandBuffer);
	auto [pVisibleLightsLayouts, iVisibleLightsBufferCapacity] = gpBufferManager->GetDynamicStorageBuffer<shaders::VisibleLightQuadLayout>(kCrc, kBufferVisibleLights, iCommandBuffer);
	ASSERT(siRendered + rCurrent.iCount <= iPointLightsBufferCapacity);
	ASSERT(siRendered + rCurrent.iCount <= iVisibleLightsBufferCapacity);

	XMVECTOR vecCameraRight = XMVector3Normalize(game::gpCamera->mMatView.r[0]);
	XMVECTOR vecCameraUp = XMVector3Normalize(game::gpCamera->mMatView.r[1]);

	float fMinLightingArea = MinLightingDepositSize();

	for (int64_t i = 0; i < rCurrent.iCount; ++i)
	{
		// Load
		XMVECTOR vecPosition = rCurrent.pVecPositions[i];
		const PointLightsType& rType = PointLightsInterpolate::GetType(rCurrent.puiTypeIndices[i]);
		float fRotation = rCurrent.pfRotations[i];
		float fLightingArea = std::max(rCurrent.pfLightingAreas[i], fMinLightingArea);
		float fLightingIntensity = rCurrent.pfLightingIntensities[i];

		// Visibility culling
		XMFLOAT4A f4Position {};
		if (!IsPointVisible(vecPosition, f4Position))
		{
			continue;
		}

		// Store original world position for visible light (before base height projection)
		XMFLOAT4A f4VisiblePosition = f4Position;

		// Project to base height for lighting
		XMStoreFloat4A(&f4Position, ProjectToBaseHeight(vecPosition));

		// Build AxisAlignedQuadLayout for lighting pass (uses base height projected position)
		int64_t iTextureIndex = gpTextureManager->mTextureDescriptors.CrcToIndex(rType.crc);
		float fBlurredTextureIndex = gpTextureManager->mTextureDescriptors.CrcToBlurredIndex(rType.crc);
		XMFLOAT4A f4Params {};
		f4Params.x = fBlurredTextureIndex;
		f4Params.y = fLightingIntensity;
		f4Params.z = fRotation;
		BuildAxisAlignedQuad(pPointLightsLayouts[siRendered], f4Position, fLightingArea, f4Params, rType.uiColor);

		// Build VisibleLightQuadLayout for visible sprite pass (uses original world position)
		float fVisibleArea = rCurrent.pfVisibleAreas[i];
		float fVisibleIntensity = rCurrent.pfVisibleIntensities[i];

		shaders::VisibleLightQuadLayout& rVisibleLayout = pVisibleLightsLayouts[siRendered];

		// 4 corners for quad
		if (rType.bCameraAligned)
		{
			XMVECTOR vecCenter = XMLoadFloat4A(&f4VisiblePosition);
			XMVECTOR vecRight = XMVectorScale(vecCameraRight, fVisibleArea);
			XMVECTOR vecUp = XMVectorScale(vecCameraUp, fVisibleArea);
			XMFLOAT4A f4Corner {};
			XMStoreFloat4A(&f4Corner, XMVectorAdd(XMVectorSubtract(vecCenter, vecRight), vecUp));
			rVisibleLayout.pf4Vertices[0] = {f4Corner.x, f4Corner.y, f4Corner.z, 1.0f};
			XMStoreFloat4A(&f4Corner, XMVectorAdd(XMVectorAdd(vecCenter, vecRight), vecUp));
			rVisibleLayout.pf4Vertices[1] = {f4Corner.x, f4Corner.y, f4Corner.z, 1.0f};
			XMStoreFloat4A(&f4Corner, XMVectorSubtract(XMVectorSubtract(vecCenter, vecRight), vecUp));
			rVisibleLayout.pf4Vertices[2] = {f4Corner.x, f4Corner.y, f4Corner.z, 1.0f};
			XMStoreFloat4A(&f4Corner, XMVectorSubtract(XMVectorAdd(vecCenter, vecRight), vecUp));
			rVisibleLayout.pf4Vertices[3] = {f4Corner.x, f4Corner.y, f4Corner.z, 1.0f};
		}
		else
		{
			rVisibleLayout.pf4Vertices[0] = {f4VisiblePosition.x - fVisibleArea, f4VisiblePosition.y + fVisibleArea, f4VisiblePosition.z, 1.0f};
			rVisibleLayout.pf4Vertices[1] = {f4VisiblePosition.x + fVisibleArea, f4VisiblePosition.y + fVisibleArea, f4VisiblePosition.z, 1.0f};
			rVisibleLayout.pf4Vertices[2] = {f4VisiblePosition.x - fVisibleArea, f4VisiblePosition.y - fVisibleArea, f4VisiblePosition.z, 1.0f};
			rVisibleLayout.pf4Vertices[3] = {f4VisiblePosition.x + fVisibleArea, f4VisiblePosition.y - fVisibleArea, f4VisiblePosition.z, 1.0f};
		}

		// Texture coordinates
		rVisibleLayout.pf4Texcoords[0] = {0.0f, 0.0f, 0.0f, 0.0f};
		rVisibleLayout.pf4Texcoords[1] = {1.0f, 0.0f, 0.0f, 0.0f};
		rVisibleLayout.pf4Texcoords[2] = {0.0f, 1.0f, 0.0f, 0.0f};
		rVisibleLayout.pf4Texcoords[3] = {1.0f, 1.0f, 0.0f, 0.0f};

		// Vertex colors
		rVisibleLayout.puiColors[0] = rType.uiColor;
		rVisibleLayout.puiColors[1] = rType.uiColor;
		rVisibleLayout.puiColors[2] = rType.uiColor;
		rVisibleLayout.puiColors[3] = rType.uiColor;

		rVisibleLayout.fIntensity = fVisibleIntensity;
		rVisibleLayout.fRotation = fRotation;
		rVisibleLayout.uiTextureIndex = static_cast<uint32_t>(iTextureIndex);

		++siRendered;
	}
}

void PointLightsInterpolate::EndRender([[maybe_unused]] int64_t iCommandBuffer)
{
	gpProfileManager->SetCount(kCpuCounterPointLights, siTotalCount);
	gpProfileManager->SetCount(kCpuCounterPointLightsRendered, siRendered);
	gpPipelineManager->mDynamicPipelines.mPipelineMaps[kDynamicPipelineAxisAlignedLighting].at(kCrc)->WriteIndirectBuffer(iCommandBuffer, siRendered);
	giLightingDepositInstances += siRendered; // Feeds the spread-chain gate (RenderLightingSpreadIndirect); pair any change here with the deposit write above
	gpPipelineManager->mDynamicPipelines.mPipelineMaps[kDynamicPipelineVisibleLights].at(kCrc)->WriteIndirectBuffer(iCommandBuffer, siRendered);
}

} // namespace engine

#endif // BT_CLIENT
