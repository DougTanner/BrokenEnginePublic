#include "AreaLights.h"

#if defined(BT_CLIENT)

#include "Profile/ProfileManager.h"
#include "Ui/LightingWrappersBase.h"
#include "Ui/WrapperBase.h"

namespace engine
{

void AreaLightsInterpolate::GraphicsResources()
{
	gpBufferManager->CreateDynamicBuffer(kCrc, kBufferMain, kName, sizeof(shaders::QuadLayout));
	gpPipelineManager->mDynamicPipelines.CreatePipelineLighting(kCrc, kName, sizeof(shaders::QuadLayout));
	Buffer* pVisibleLightsBuffers = gpBufferManager->CreateDynamicBuffer(kCrc, kBufferVisibleLights, kName, sizeof(shaders::VisibleLightQuadLayout));
	gpPipelineManager->mDynamicPipelines.CreatePipelineVisibleLights(kCrc, kName, pVisibleLightsBuffers);
}

static int64_t siRendered = 0;
static int64_t siTotalCount = 0;

void AreaLightsInterpolate::BeginRender([[maybe_unused]] int64_t iCommandBuffer, const std::unordered_map<GridCoord, game::FrameInterpolate>& rRenderInterpolates, const std::vector<GridCoord>& rActiveCoords)
{
	siRendered = 0;
	siTotalCount = 0;

	int64_t iTotalCapacity = AccumulateRenderCapacity(rRenderInterpolates, rActiveCoords,
		[](const game::FrameInterpolate& rInterpolate) -> const auto& { return rInterpolate.areaLights; });

	if (iTotalCapacity == 0)
	{
		return;
	}

	if (Buffer* pBuffer = gpBufferManager->ResizeDynamicBufferIfNeeded(kCrc, kBufferMain, kName, sizeof(shaders::QuadLayout), iTotalCapacity, iCommandBuffer))
	{
		gpPipelineManager->mDynamicPipelines.mPipelineMaps[kDynamicPipelineLighting].at(kCrc)->UpdateStorageBufferDescriptor(iCommandBuffer, 1, pBuffer);
	}
	if (Buffer* pBuffer = gpBufferManager->ResizeDynamicBufferIfNeeded(kCrc, kBufferVisibleLights, kName, sizeof(shaders::VisibleLightQuadLayout), iTotalCapacity, iCommandBuffer))
	{
		gpPipelineManager->mDynamicPipelines.mPipelineMaps[kDynamicPipelineVisibleLights].at(kCrc)->UpdateStorageBufferDescriptor(iCommandBuffer, 2, pBuffer);
	}
}

void AreaLightsInterpolate::Render([[maybe_unused]] const game::FrameInterpolate& __restrict rFrameInterpolate, [[maybe_unused]] int64_t iCommandBuffer)
{
	const AreaLightsInterpolate& rCurrent = rFrameInterpolate.areaLights;
	siTotalCount += rCurrent.iCount;

	if (rCurrent.iCount == 0)
	{
		return;
	}

	auto [pVisibleLightsLayouts, iVisibleLightsBufferCapacity] = gpBufferManager->GetDynamicStorageBuffer<shaders::VisibleLightQuadLayout>(kCrc, kBufferVisibleLights, iCommandBuffer);
	auto [pAreaLightsLayouts, iAreaLightsBufferCapacity] = gpBufferManager->GetDynamicStorageBuffer<shaders::QuadLayout>(kCrc, kBufferMain, iCommandBuffer);
	ASSERT(siRendered + rCurrent.iCount <= iVisibleLightsBufferCapacity);
	ASSERT(siRendered + rCurrent.iCount <= iAreaLightsBufferCapacity);

	float fMinLightingSize = MinLightingDepositSize();

	for (int64_t i = 0; i < rCurrent.iCount; ++i)
	{
		// Load visible positions
		XMVECTOR vecVisiblePos0 = rCurrent.pVecVisiblePositions[0][i];
		XMVECTOR vecVisiblePos1 = rCurrent.pVecVisiblePositions[1][i];
		XMVECTOR vecVisiblePos2 = rCurrent.pVecVisiblePositions[2][i];
		XMVECTOR vecVisiblePos3 = rCurrent.pVecVisiblePositions[3][i];

		// Calculate center and get type configuration
		XMVECTOR vecCenter = XMVectorScale(XMVectorAdd(XMVectorAdd(vecVisiblePos0, vecVisiblePos1), XMVectorAdd(vecVisiblePos2, vecVisiblePos3)), 0.25f);
		const AreaLightsType& rType = AreaLightsInterpolate::GetType(rCurrent.puiTypeIndices[i]);
		int64_t iTextureIndex = gpTextureManager->mTextureDescriptors.CrcToIndex(rType.crc);
		float fBlurredTextureIndex = gpTextureManager->mTextureDescriptors.CrcToBlurredIndex(rType.crc);
		float fIntensityMultiplier = rCurrent.pfIntensityMultipliers[i];
		float fVisibleIntensity = rType.pVisibleIntensityWrapper != nullptr ? rType.pVisibleIntensityWrapper->Get() : rType.fVisibleIntensity;
		float fLightingSize = std::max(rType.pLightingSizeWrapper != nullptr ? rType.pLightingSizeWrapper->Get() : rType.fLightingSize, fMinLightingSize);
		float fLightingIntensity = rType.pLightingIntensityWrapper != nullptr ? rType.pLightingIntensityWrapper->Get() : rType.fLightingIntensity;

		// Scale visible positions around center to create oriented lighting quad
		XMVECTOR vecOffset0 = XMVectorSubtract(vecVisiblePos0, vecCenter);
		float fVisibleExtent = XMVectorGetX(XMVector2Length(vecOffset0));
		float fScale = fVisibleExtent > 0.0f ? fLightingSize / fVisibleExtent : 1.0f;
		XMVECTOR vecScaleFactor = XMVectorReplicate(fScale);

		XMVECTOR vecLightingPos0 = XMVectorMultiplyAdd(vecOffset0, vecScaleFactor, vecCenter);
		XMVECTOR vecLightingPos1 = XMVectorMultiplyAdd(XMVectorSubtract(vecVisiblePos1, vecCenter), vecScaleFactor, vecCenter);
		XMVECTOR vecLightingPos2 = XMVectorMultiplyAdd(XMVectorSubtract(vecVisiblePos2, vecCenter), vecScaleFactor, vecCenter);
		XMVECTOR vecLightingPos3 = XMVectorMultiplyAdd(XMVectorSubtract(vecVisiblePos3, vecCenter), vecScaleFactor, vecCenter);

		// Frustum culling: compute AABB of all 8 vertices and test intersection
		auto [vecMin, vecMax] = common::ComputeAabb(vecVisiblePos0, vecVisiblePos1, vecVisiblePos2, vecVisiblePos3, vecLightingPos0, vecLightingPos1, vecLightingPos2, vecLightingPos3);
		if (!common::AabbIntersectsArea(game::gpCamera->f4RenderVisibleArea, vecMin, vecMax))
		{
			continue;
		}

		// Populate visible light quad with actual visible positions
		shaders::VisibleLightQuadLayout& rVisibleLayout = pVisibleLightsLayouts[siRendered];
		XMStoreFloat4(&rVisibleLayout.pf4Vertices[0], vecVisiblePos0);
		XMStoreFloat4(&rVisibleLayout.pf4Vertices[1], vecVisiblePos1);
		XMStoreFloat4(&rVisibleLayout.pf4Vertices[2], vecVisiblePos2);
		XMStoreFloat4(&rVisibleLayout.pf4Vertices[3], vecVisiblePos3);

		rVisibleLayout.pf4Texcoords[0] = {rType.pf2Texcoords[0].x, rType.pf2Texcoords[0].y, 0.0f, 0.0f};
		rVisibleLayout.pf4Texcoords[1] = {rType.pf2Texcoords[1].x, rType.pf2Texcoords[1].y, 0.0f, 0.0f};
		rVisibleLayout.pf4Texcoords[2] = {rType.pf2Texcoords[2].x, rType.pf2Texcoords[2].y, 0.0f, 0.0f};
		rVisibleLayout.pf4Texcoords[3] = {rType.pf2Texcoords[3].x, rType.pf2Texcoords[3].y, 0.0f, 0.0f};

		rVisibleLayout.puiColors[0] = rType.puiColors[0];
		rVisibleLayout.puiColors[1] = rType.puiColors[1];
		rVisibleLayout.puiColors[2] = rType.puiColors[2];
		rVisibleLayout.puiColors[3] = rType.puiColors[3];

		rVisibleLayout.fIntensity = fVisibleIntensity * fIntensityMultiplier;
		rVisibleLayout.fRotation = 0.0f;
		rVisibleLayout.uiTextureIndex = static_cast<uint32_t>(iTextureIndex);

		// Populate area light quad with base height positions for ground shadow effect
		shaders::QuadLayout& rAreaLayout = pAreaLightsLayouts[siRendered];

		// Project lighting positions to base height
		XMVECTOR vecBaseLighting0 = ProjectToBaseHeight(vecLightingPos0);
		XMVECTOR vecBaseLighting1 = ProjectToBaseHeight(vecLightingPos1);
		XMVECTOR vecBaseLighting2 = ProjectToBaseHeight(vecLightingPos2);
		XMVECTOR vecBaseLighting3 = ProjectToBaseHeight(vecLightingPos3);

		XMFLOAT4A f4Base {};
		XMStoreFloat4A(&f4Base, vecBaseLighting0);
		rAreaLayout.pf4VerticesTexcoords[0] = {f4Base.x, f4Base.y, rType.pf2Texcoords[0].x, rType.pf2Texcoords[0].y};
		XMStoreFloat4A(&f4Base, vecBaseLighting1);
		rAreaLayout.pf4VerticesTexcoords[1] = {f4Base.x, f4Base.y, rType.pf2Texcoords[1].x, rType.pf2Texcoords[1].y};
		XMStoreFloat4A(&f4Base, vecBaseLighting2);
		rAreaLayout.pf4VerticesTexcoords[2] = {f4Base.x, f4Base.y, rType.pf2Texcoords[2].x, rType.pf2Texcoords[2].y};
		XMStoreFloat4A(&f4Base, vecBaseLighting3);
		rAreaLayout.pf4VerticesTexcoords[3] = {f4Base.x, f4Base.y, rType.pf2Texcoords[3].x, rType.pf2Texcoords[3].y};

		XMFLOAT4A f4Params {};
		f4Params.x = fBlurredTextureIndex;
		f4Params.y = fLightingIntensity * fIntensityMultiplier;
		rAreaLayout.pf4Params[0] = f4Params;
		rAreaLayout.pf4Params[1] = f4Params;
		rAreaLayout.pf4Params[2] = f4Params;
		rAreaLayout.pf4Params[3] = f4Params;
		rAreaLayout.uiColor = rType.puiColors[0];

		++siRendered;
	}
}

void AreaLightsInterpolate::EndRender([[maybe_unused]] int64_t iCommandBuffer)
{
	gpProfileManager->SetCount(kCpuCounterAreaLights, siTotalCount);
	gpProfileManager->SetCount(kCpuCounterAreaLightsRendered, siRendered);
	gpPipelineManager->mDynamicPipelines.mPipelineMaps[kDynamicPipelineLighting].at(kCrc)->WriteIndirectBuffer(iCommandBuffer, siRendered);
	giLightingDepositInstances += siRendered; // Feeds the spread-chain gate (RenderLightingSpreadIndirect); pair any change here with the deposit write above
	gpPipelineManager->mDynamicPipelines.mPipelineMaps[kDynamicPipelineVisibleLights].at(kCrc)->WriteIndirectBuffer(iCommandBuffer, siRendered);
}

} // namespace engine

#endif // BT_CLIENT
