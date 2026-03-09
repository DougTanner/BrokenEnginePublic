#include "AreaLights.h"

#if defined(BT_CLIENT)

#include "Profile/ProfileManager.h"

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

	int64_t iTotalCapacity = 0;
	for (const GridCoord& rCoord : rActiveCoords)
	{
		auto it = rRenderInterpolates.find(rCoord);
		if (it != rRenderInterpolates.end())
		{
			iTotalCapacity += it->second.areaLights.iCapacity;
		}
	}

	if (iTotalCapacity == 0)
	{
		return;
	}

	int64_t iFramebuffer = iCommandBuffer;
	if (Buffer* pBuffer = gpBufferManager->ResizeDynamicBufferIfNeeded(kCrc, kBufferMain, kName, sizeof(shaders::QuadLayout), iTotalCapacity, iCommandBuffer))
	{
		gpPipelineManager->mDynamicPipelines.mPipelineMaps[kDynamicPipelineLighting].at(kCrc)->UpdateStorageBufferDescriptor(iFramebuffer, 1, pBuffer);
	}
	if (Buffer* pBuffer = gpBufferManager->ResizeDynamicBufferIfNeeded(kCrc, kBufferVisibleLights, kName, sizeof(shaders::VisibleLightQuadLayout), iTotalCapacity, iCommandBuffer))
	{
		gpPipelineManager->mDynamicPipelines.mPipelineMaps[kDynamicPipelineVisibleLights].at(kCrc)->UpdateStorageBufferDescriptor(iFramebuffer, 2, pBuffer);
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

	for (int64_t i = 0; i < rCurrent.iCount; ++i)
	{
		// Load visible positions
		XMVECTOR vecVisiblePos0 = rCurrent.pVecVisiblePositions[0][i];
		XMVECTOR vecVisiblePos1 = rCurrent.pVecVisiblePositions[1][i];
		XMVECTOR vecVisiblePos2 = rCurrent.pVecVisiblePositions[2][i];
		XMVECTOR vecVisiblePos3 = rCurrent.pVecVisiblePositions[3][i];

		// Calculate center and get type configuration
		XMVECTOR vecCenter = (vecVisiblePos0 + vecVisiblePos1 + vecVisiblePos2 + vecVisiblePos3) * 0.25f;
		const AreaLightsType& rType = AreaLightsInterpolate::GetType(rCurrent.puiTypeIndices[i]);
		float fIntensityMultiplier = rCurrent.pfIntensityMultipliers[i];
		float fLightingSize = rType.fLightingSize;

		// Calculate lighting quad vertices from center expansion
		XMFLOAT4A f4Center {};
		XMStoreFloat4A(&f4Center, vecCenter);

		XMVECTOR vecLightingPos0 = XMVectorSet(f4Center.x - fLightingSize, f4Center.y + fLightingSize, f4Center.z, 1.0f);
		XMVECTOR vecLightingPos1 = XMVectorSet(f4Center.x + fLightingSize, f4Center.y + fLightingSize, f4Center.z, 1.0f);
		XMVECTOR vecLightingPos2 = XMVectorSet(f4Center.x - fLightingSize, f4Center.y - fLightingSize, f4Center.z, 1.0f);
		XMVECTOR vecLightingPos3 = XMVectorSet(f4Center.x + fLightingSize, f4Center.y - fLightingSize, f4Center.z, 1.0f);

		// Frustum culling: compute AABB of all 8 vertices and test intersection
		auto [vecMin, vecMax] = common::ComputeAabb(vecVisiblePos0, vecVisiblePos1, vecVisiblePos2, vecVisiblePos3, vecLightingPos0, vecLightingPos1, vecLightingPos2, vecLightingPos3);
		if (!game::gpCamera->AabbIntersectsVisibleArea(game::gpCamera->f4RenderVisibleArea, vecMin, vecMax))
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

		rVisibleLayout.fIntensity = rType.fVisibleIntensity * fIntensityMultiplier;
		rVisibleLayout.fRotation = 0.0f;
		rVisibleLayout.uiTextureIndex = static_cast<uint32_t>(gpTextureManager->mTextureDescriptors.CrcToIndex(rType.crc));

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
		f4Params.x = gpTextureManager->mTextureDescriptors.CrcToIndex(rType.crc);
		f4Params.y = rType.fLightingIntensity * fIntensityMultiplier;
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
	gpPipelineManager->mDynamicPipelines.mPipelineMaps[kDynamicPipelineVisibleLights].at(kCrc)->WriteIndirectBuffer(iCommandBuffer, siRendered);
}

} // namespace engine

#endif // BT_CLIENT
