#include "AreaLights.h"

#include "Frame/Frame.h"
#include "Graphics/Camera.h"
#include "Graphics/Islands.h"
#include "Graphics/Managers/BufferManager.h"
#include "Graphics/Managers/PipelineManager.h"
#include "Profile/ProfileManager.h"

namespace engine
{

bool AreaLightsInterpolate::operator==(const AreaLightsInterpolate& rOther) const
{
	bool bEqual = true;
	bEqual &= common::BreakOnNotEqual<Collection>(*this, rOther);

	for (int64_t i = 0; i < iCount; ++i)
	{
		bEqual &= common::BreakOnNotEqual(pVecPositions[i], rOther.pVecPositions[i]);
	}

	return bEqual;
}

void AreaLightsInterpolate::Update(game::FrameInterpolate& __restrict rCurrentFrame, const game::Frame& __restrict rPreviousFrame, float fDeltaTime)
{
	AreaLightsInterpolate& rCurrent = rCurrentFrame.areaLights;
	const AreaLightsInterpolate& rPrevious = rPreviousFrame.interpolate.areaLights;

	rCurrent.uiNextId = rPrevious.uiNextId;
	rCurrent.idToIndexMap = rPrevious.idToIndexMap;

	if (!engine::ReallocateIfCapacityChanged(rCurrent, rPrevious, AREA_LIGHTS_INTERPOLATE_LIST(rCurrent)))
	{
		return;
	}

	// Positions will be filled in by owners
}

void AreaLightsInterpolate::Render(int64_t iCommandBuffer) const
{
	if (iCount == 0 || pData == nullptr)
	{
		return;
	}

	PROFILE_SET_COUNT(kCpuCounterAreaLightsRendered, iCount);

	auto pVisibleLightsLayouts = reinterpret_cast<shaders::VisibleLightQuadLayout*>(gpBufferManager->mVisibleLightsStorageBuffers.at(iCommandBuffer).mpMappedMemory);
	auto pAreaLightsLayouts = reinterpret_cast<shaders::QuadLayout*>(gpBufferManager->mAreaLightsStorageBuffers.at(iCommandBuffer).mpMappedMemory);

	int64_t iVisibleLightsRendered = 0;
	int64_t iAreaLightsRendered = 0;

	for (int64_t i = 0; i < iCount; ++i)
	{
		XMVECTOR vecPosition = pVecPositions[i];

		XMFLOAT4A f4Position {};
		XMStoreFloat4A(&f4Position, vecPosition);
		if (!game::gpCamera->InVisibleArea(game::gpCamera->f4RenderVisibleArea, f4Position))
		{
			continue;
		}

		constexpr float fDefaultSize = 0.5f;

		shaders::VisibleLightQuadLayout& rVisibleLayout = pVisibleLightsLayouts[iVisibleLightsRendered];
		rVisibleLayout.pf4Vertices[0] = {f4Position.x - fDefaultSize, f4Position.y + fDefaultSize, f4Position.z, 1.0f};
		rVisibleLayout.pf4Vertices[1] = {f4Position.x + fDefaultSize, f4Position.y + fDefaultSize, f4Position.z, 1.0f};
		rVisibleLayout.pf4Vertices[2] = {f4Position.x - fDefaultSize, f4Position.y - fDefaultSize, f4Position.z, 1.0f};
		rVisibleLayout.pf4Vertices[3] = {f4Position.x + fDefaultSize, f4Position.y - fDefaultSize, f4Position.z, 1.0f};

		rVisibleLayout.pf4Texcoords[0] = {0.0f, 0.0f, 0.0f, 0.0f};
		rVisibleLayout.pf4Texcoords[1] = {1.0f, 0.0f, 0.0f, 0.0f};
		rVisibleLayout.pf4Texcoords[2] = {0.0f, 1.0f, 0.0f, 0.0f};
		rVisibleLayout.pf4Texcoords[3] = {1.0f, 1.0f, 0.0f, 0.0f};

		rVisibleLayout.puiColors[0] = rVisibleLayout.puiColors[1] = rVisibleLayout.puiColors[2] = rVisibleLayout.puiColors[3] = 0xFFFFFFFF;

		rVisibleLayout.fIntensity = 1.0f;
		rVisibleLayout.fRotation = 0.0f;
		rVisibleLayout.uiTextureIndex = 0;

		shaders::QuadLayout& rAreaLayout = pAreaLightsLayouts[iAreaLightsRendered];

		float fElevation = gpIslands->GlobalElevation(vecPosition);
		auto vecBasePosition = common::ToBaseHeight(vecPosition, game::gpCamera->mVecEyePosition, std::max(fElevation, gBaseHeight.Get()));
		XMFLOAT4A f4BasePosition {};
		XMStoreFloat4A(&f4BasePosition, vecBasePosition);

		rAreaLayout.pf4VerticesTexcoords[0] = {f4BasePosition.x - fDefaultSize, f4BasePosition.y + fDefaultSize, 0.0f, 0.0f};
		rAreaLayout.pf4VerticesTexcoords[1] = {f4BasePosition.x + fDefaultSize, f4BasePosition.y + fDefaultSize, 1.0f, 0.0f};
		rAreaLayout.pf4VerticesTexcoords[2] = {f4BasePosition.x - fDefaultSize, f4BasePosition.y - fDefaultSize, 0.0f, 1.0f};
		rAreaLayout.pf4VerticesTexcoords[3] = {f4BasePosition.x + fDefaultSize, f4BasePosition.y - fDefaultSize, 1.0f, 1.0f};

		XMFLOAT4A f4Misc {0.0f, 1.0f, 0.0f, 0.0f};
		rAreaLayout.pf4Misc[0] = rAreaLayout.pf4Misc[1] = rAreaLayout.pf4Misc[2] = rAreaLayout.pf4Misc[3] = f4Misc;
		rAreaLayout.f4Misc = {0.0f, 0.0f, 0.0f, 0.0f};
		rAreaLayout.uiColor = 0xFFFFFFFF;

		++iVisibleLightsRendered;
		++iAreaLightsRendered;
	}

	PROFILE_SET_COUNT(kCpuCounterVisibleLightsRendered, iVisibleLightsRendered);
	gpPipelineManager->mpPipelines[kPipelineVisibleLights].WriteIndirectBuffer(iCommandBuffer, iVisibleLightsRendered);

	PROFILE_SET_COUNT(kCpuCounterAreaLightsRendered, iAreaLightsRendered);
	gpPipelineManager->mpPipelines[kPipelineAreaLights].WriteIndirectBuffer(iCommandBuffer, iAreaLightsRendered);
}

bool AreaLightsPostRender::operator==(const AreaLightsPostRender& rOther) const
{
	bool bEqual = true;
	bEqual &= common::BreakOnNotEqual<Collection>(*this, rOther);

	for (int64_t i = 0; i < iCount; ++i)
	{
		bEqual &= common::BreakOnNotEqual(puiIds[i], rOther.puiIds[i]);
	}

	return bEqual;
}

void AreaLightsPostRender::Update(game::FramePostRender& __restrict rCurrentFramePostRender, const game::Frame& __restrict rPreviousFrame, float fDeltaTime)
{
	AreaLightsPostRender& rCurrent = rCurrentFramePostRender.areaLights;

	const AreaLightsPostRender& rPrevious = rPreviousFrame.postRender.areaLights;
	if (!engine::ReallocateIfCapacityChanged(rCurrent, rPrevious, AREA_LIGHTS_POST_RENDER_LIST(rCurrent)))
	{
		return;
	}

	for (int64_t i = 0; i < rCurrent.iCount; ++i)
	{
		// Load
		area_light_t uiId = rPrevious.puiIds[i];

		//Save
		rCurrent.puiIds[i] = uiId;
	}
}

area_light_t AreaLightsPostRender::Add(game::Frame& __restrict rFrame)
{
	AreaLightsInterpolate& rCurrentInterpolate = rFrame.interpolate.areaLights;
	AreaLightsPostRender& rCurrentPostRender = rFrame.postRender.areaLights;

	int64_t iNewCapacity = engine::CalculateGrowthCapacity(rCurrentInterpolate);
	if (iNewCapacity > 0)
	{
		ASSERT(rCurrentInterpolate.iCount == rCurrentPostRender.iCount);
		engine::GrowCapacityWithCopy(rCurrentInterpolate, iNewCapacity, rCurrentInterpolate.iCount, AREA_LIGHTS_INTERPOLATE_LIST(rCurrentInterpolate));
		engine::GrowCapacityWithCopy(rCurrentPostRender, iNewCapacity, rCurrentPostRender.iCount, AREA_LIGHTS_POST_RENDER_LIST(rCurrentPostRender));
	}

	int64_t iSpawnIndex = engine::IncrementCountsAndGetSpawnIndex(rCurrentInterpolate, rCurrentPostRender);
	rCurrentInterpolate.idToIndexMap[rCurrentInterpolate.uiNextId] = iSpawnIndex;
	rCurrentPostRender.puiIds[iSpawnIndex] = rCurrentInterpolate.uiNextId;
	return rCurrentInterpolate.uiNextId++;
}

void AreaLightsPostRender::Remove(game::Frame& __restrict rFrame, area_light_t uiId)
{
	AreaLightsInterpolate& rCurrentInterpolate = rFrame.interpolate.areaLights;
	AreaLightsPostRender& rCurrentPostRender = rFrame.postRender.areaLights;

	area_light_t uiIndex = rCurrentInterpolate.idToIndexMap.at(uiId);

	if (rCurrentInterpolate.iCount - 1 > uiIndex) [[likely]]
	{
		area_light_t uiLastId = rCurrentPostRender.puiIds[rCurrentInterpolate.iCount - 1];

		engine::SwapElement(rCurrentInterpolate, uiIndex, AREA_LIGHTS_INTERPOLATE_LIST(rCurrentInterpolate));
		engine::SwapElement(rCurrentPostRender, uiIndex, AREA_LIGHTS_POST_RENDER_LIST(rCurrentPostRender));

		rCurrentInterpolate.idToIndexMap[uiLastId] = uiIndex;
	}

	--rCurrentInterpolate.iCount;
	--rCurrentPostRender.iCount;

	rCurrentInterpolate.idToIndexMap.erase(uiId);
}

} // namespace engine
