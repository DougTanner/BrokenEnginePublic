#include "AreaLights.h"

#include "Frame/Frame.h"
#include "Graphics/Camera.h"
#include "Graphics/Islands.h"
#include "Graphics/Managers/BufferManager.h"
#include "Graphics/Managers/PipelineManager.h"
#include "Graphics/Managers/ShaderManager.h"
#include "Graphics/Managers/TextureManager.h"
#include "Profile/ProfileManager.h"

namespace engine
{

static std::vector<AreaLightType> sAreaLightTypes;

uint8_t AreaLightsPostRender::RegisterType(const AreaLightType& rType)
{
	uint8_t uiIndex = static_cast<uint8_t>(sAreaLightTypes.size());
	sAreaLightTypes.push_back(rType);
	return uiIndex;
}

const AreaLightType& AreaLightsPostRender::GetType(uint8_t uiTypeIndex)
{
	ASSERT(uiTypeIndex < sAreaLightTypes.size());
	return sAreaLightTypes[uiTypeIndex];
}

void AreaLightsInterpolate::CreatePipelines()
{
	gpPipelineManager->mpPipelines[engine::kPipelineAreaLights].Create(
	{
		.pcName = "AreaLights",
		.flags = {engine::PipelineFlags::kRenderTarget, engine::PipelineFlags::kPushConstants, engine::PipelineFlags::kIndirectHostVisible, engine::PipelineFlags::kMax},
		.ppShaders = {&gpShaderManager->mShaders.at(data::kShadersQuadsVisibleAreavertCrc), &gpShaderManager->mShaders.at(data::kShadersAreaLightfragCrc)},
		.pVertexBuffer = &gpBufferManager->mQuadsVertexBuffer,
		.vkRenderPass = gpTextureManager->mLightingVkRenderPass,
		.vkExtent3D = gpTextureManager->mpLightingTextures[0].mInfo.extent,
		.pDescriptorInfos =
		{
			{.flags = engine::DescriptorFlags::kPerCommandBufferUniformBuffers, .pBuffers = gpBufferManager->mGlobalLayoutUniformBuffers.data()},
			{.flags = engine::DescriptorFlags::kPerCommandBufferStorageBuffers, .pBuffers = gpBufferManager->mAreaLightsStorageBuffers.data()},
			{.flags = engine::DescriptorFlags::kSamplerRepeat},
			{.flags = engine::DescriptorFlags::kTextures},
		},
	});
}

void AreaLightsInterpolate::Update(game::FrameInterpolate& __restrict rCurrentFrame, const game::Frame& __restrict rPreviousFrame, float fDeltaTime)
{
	AreaLightsInterpolate& rCurrent = rCurrentFrame.areaLights;

	if (rCurrent.pData == nullptr)
	{
		return;
	}

	// Owner collections (Blasters, Player, etc.) are responsible for writing AREA_LIGHTS_INTERPOLATE_LIST data every frame
}

void AreaLightsInterpolate::Render(int64_t iCommandBuffer) const
{
	if (uiCount == 0 || pData == nullptr)
	{
		return;
	}

	PROFILE_SET_COUNT(kCpuCounterAreaLightsRendered, uiCount);

	auto pVisibleLightsLayouts = reinterpret_cast<shaders::VisibleLightQuadLayout*>(gpBufferManager->mVisibleLightsStorageBuffers.at(iCommandBuffer).mpMappedMemory);
	auto pAreaLightsLayouts = reinterpret_cast<shaders::QuadLayout*>(gpBufferManager->mAreaLightsStorageBuffers.at(iCommandBuffer).mpMappedMemory);

	int64_t iVisibleLightsRendered = 0;
	int64_t iAreaLightsRendered = 0;

	for (int64_t i = 0; i < uiCount; ++i)
	{
		// Load visible positions
		XMVECTOR vecVisiblePos0 = pVecVisiblePositions[0][i];
		XMVECTOR vecVisiblePos1 = pVecVisiblePositions[1][i];
		XMVECTOR vecVisiblePos2 = pVecVisiblePositions[2][i];
		XMVECTOR vecVisiblePos3 = pVecVisiblePositions[3][i];

		// Calculate center and get type configuration
		XMVECTOR vecCenter = (vecVisiblePos0 + vecVisiblePos1 + vecVisiblePos2 + vecVisiblePos3) * 0.25f;
		const AreaLightType& rType = AreaLightsPostRender::GetType(puiTypeIndices[i]);
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
		shaders::VisibleLightQuadLayout& rVisibleLayout = pVisibleLightsLayouts[iVisibleLightsRendered];
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

		rVisibleLayout.fIntensity = rType.fVisibleIntensity;
		rVisibleLayout.fRotation = 0.0f;
		rVisibleLayout.uiTextureIndex = static_cast<uint32_t>(CrcToIndex(rType.crc));

		// Populate area light quad with base height positions for ground shadow effect
		shaders::QuadLayout& rAreaLayout = pAreaLightsLayouts[iAreaLightsRendered];

		// Calculate per-vertex base heights for lighting positions
		float fElevation0 = gpIslands->GlobalElevation(vecLightingPos0);
		float fElevation1 = gpIslands->GlobalElevation(vecLightingPos1);
		float fElevation2 = gpIslands->GlobalElevation(vecLightingPos2);
		float fElevation3 = gpIslands->GlobalElevation(vecLightingPos3);

		XMVECTOR vecBaseLighting0 = common::ToBaseHeight(vecLightingPos0, game::gpCamera->mVecEyePosition, std::max(fElevation0, gBaseHeight.Get()));
		XMVECTOR vecBaseLighting1 = common::ToBaseHeight(vecLightingPos1, game::gpCamera->mVecEyePosition, std::max(fElevation1, gBaseHeight.Get()));
		XMVECTOR vecBaseLighting2 = common::ToBaseHeight(vecLightingPos2, game::gpCamera->mVecEyePosition, std::max(fElevation2, gBaseHeight.Get()));
		XMVECTOR vecBaseLighting3 = common::ToBaseHeight(vecLightingPos3, game::gpCamera->mVecEyePosition, std::max(fElevation3, gBaseHeight.Get()));

		XMFLOAT4A f4Base {};
		XMStoreFloat4A(&f4Base, vecBaseLighting0);
		rAreaLayout.pf4VerticesTexcoords[0] = {f4Base.x, f4Base.y, rType.pf2Texcoords[0].x, rType.pf2Texcoords[0].y};
		XMStoreFloat4A(&f4Base, vecBaseLighting1);
		rAreaLayout.pf4VerticesTexcoords[1] = {f4Base.x, f4Base.y, rType.pf2Texcoords[1].x, rType.pf2Texcoords[1].y};
		XMStoreFloat4A(&f4Base, vecBaseLighting2);
		rAreaLayout.pf4VerticesTexcoords[2] = {f4Base.x, f4Base.y, rType.pf2Texcoords[2].x, rType.pf2Texcoords[2].y};
		XMStoreFloat4A(&f4Base, vecBaseLighting3);
		rAreaLayout.pf4VerticesTexcoords[3] = {f4Base.x, f4Base.y, rType.pf2Texcoords[3].x, rType.pf2Texcoords[3].y};

		XMFLOAT4A f4Misc {};
		f4Misc.x = CrcToIndex(rType.crc);
		f4Misc.y = rType.fLightingIntensity;
		rAreaLayout.pf4Misc[0] = f4Misc;
		rAreaLayout.pf4Misc[1] = f4Misc;
		rAreaLayout.pf4Misc[2] = f4Misc;
		rAreaLayout.pf4Misc[3] = f4Misc;
		XMStoreFloat4(&rAreaLayout.f4Misc, pVecDirectionMultipliers[i]);
		rAreaLayout.uiColor = rType.puiColors[0];

		// Increment both counters for dual rendering passes
		++iVisibleLightsRendered;
		++iAreaLightsRendered;
	}

	// Update profiling counters and write indirect draw buffers
	PROFILE_SET_COUNT(kCpuCounterVisibleLightsRendered, iVisibleLightsRendered);
	gpPipelineManager->mpPipelines[kPipelineVisibleLights].WriteIndirectBuffer(iCommandBuffer, iVisibleLightsRendered);

	PROFILE_SET_COUNT(kCpuCounterAreaLightsRendered, iAreaLightsRendered);
	gpPipelineManager->mpPipelines[kPipelineAreaLights].WriteIndirectBuffer(iCommandBuffer, iAreaLightsRendered);
}

void AreaLightsPostRender::Update(game::FramePostRender& __restrict rCurrentFramePostRender, const game::Frame& __restrict rPreviousFrame, float fDeltaTime)
{
	AreaLightsPostRender& rCurrent = rCurrentFramePostRender.areaLights;

	if (rCurrent.pData == nullptr)
	{
		return;
	}

	const AreaLightsPostRender& rPrevious = rPreviousFrame.postRender.areaLights;
	for (int64_t i = 0; i < rCurrent.uiCount; ++i)
	{
		// Load
		area_lights_t id = rPrevious.puiIds[i];

		// Save
		rCurrent.puiIds[i] = id;
	}
}

area_lights_t AreaLightsPostRender::Add(game::Frame& __restrict rFrame, uint8_t uiTypeIndex)
{
	AreaLightsInterpolate& rCurrentInterpolate = rFrame.interpolate.areaLights;
	AreaLightsPostRender& rCurrentPostRender = rFrame.postRender.areaLights;

	int64_t iNewCapacity = engine::CalculateGrowthCapacity(rCurrentInterpolate);
	if (iNewCapacity > 0)
	{
		ASSERT(rCurrentInterpolate.uiCount == rCurrentPostRender.uiCount);
		engine::GrowCapacityWithCopy(rCurrentInterpolate, iNewCapacity, rCurrentInterpolate.uiCount, AREA_LIGHTS_INTERPOLATE_LIST(rCurrentInterpolate));
		engine::GrowCapacityWithCopy(rCurrentPostRender, iNewCapacity, rCurrentPostRender.uiCount, AREA_LIGHTS_POST_RENDER_LIST(rCurrentPostRender));
	}

	int64_t iSpawnIndex = engine::IncrementCountsAndGetSpawnIndex(rCurrentInterpolate, rCurrentPostRender);
	area_lights_t newId = area_lights_t::Generate(rFrame.postRender);
	rCurrentInterpolate.idToIndexMap[newId] = iSpawnIndex;
	for (size_t j = 0; j < 4; ++j)
	{
		rCurrentInterpolate.pVecVisiblePositions[j][iSpawnIndex] = XMVectorZero();
	}
	rCurrentInterpolate.puiTypeIndices[iSpawnIndex] = uiTypeIndex;
	rCurrentInterpolate.pVecDirectionMultipliers[iSpawnIndex] = XMVectorSet(1.0f, 1.0f, 1.0f, 1.0f);
	rCurrentPostRender.puiIds[iSpawnIndex] = newId;
	return newId;
}

void AreaLightsPostRender::Remove(game::Frame& __restrict rFrame, area_lights_t id)
{
	AreaLightsInterpolate& rCurrentInterpolate = rFrame.interpolate.areaLights;
	AreaLightsPostRender& rCurrentPostRender = rFrame.postRender.areaLights;

	uint64_t uiIndex = rCurrentInterpolate.idToIndexMap.at(id);

	if (rCurrentInterpolate.uiCount - 1 > uiIndex) [[likely]]
	{
		area_lights_t lastId = rCurrentPostRender.puiIds[rCurrentInterpolate.uiCount - 1];

		engine::SwapElement(rCurrentInterpolate, uiIndex, AREA_LIGHTS_INTERPOLATE_LIST(rCurrentInterpolate));
		engine::SwapElement(rCurrentPostRender, uiIndex, AREA_LIGHTS_POST_RENDER_LIST(rCurrentPostRender));

		rCurrentInterpolate.idToIndexMap[lastId] = uiIndex;
	}

	--rCurrentInterpolate.uiCount;
	--rCurrentPostRender.uiCount;

	rCurrentInterpolate.idToIndexMap.erase(id);
}

bool AreaLightsInterpolate::operator==(const AreaLightsInterpolate& rOther) const
{
	bool bEqual = true;
	bEqual &= common::BreakOnNotEqual<Collection>(*this, rOther);

	for (int64_t i = 0; i < uiCount; ++i)
	{
		for (size_t j = 0; j < 4; ++j)
		{
			bEqual &= common::BreakOnNotEqual(pVecVisiblePositions[j][i], rOther.pVecVisiblePositions[j][i]);
		}
		bEqual &= common::BreakOnNotEqual(puiTypeIndices[i], rOther.puiTypeIndices[i]);
		bEqual &= common::BreakOnNotEqual(pVecDirectionMultipliers[i], rOther.pVecDirectionMultipliers[i]);
	}

	return bEqual;
}

bool AreaLightsPostRender::operator==(const AreaLightsPostRender& rOther) const
{
	bool bEqual = true;
	bEqual &= common::BreakOnNotEqual<Collection>(*this, rOther);

	for (int64_t i = 0; i < uiCount; ++i)
	{
		bEqual &= common::BreakOnNotEqual(puiIds[i], rOther.puiIds[i]);
	}

	return bEqual;
}

} // namespace engine
