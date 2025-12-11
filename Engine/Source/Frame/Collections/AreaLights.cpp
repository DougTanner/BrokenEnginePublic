#include "AreaLights.h"

#include "Frame/Frame.h"
#include "Profile/ProfileManager.h"

namespace engine
{

void AreaLightsInterpolate::Register()
{
}

void AreaLightsInterpolate::GraphicsResources()
{
	AllocatePipelines();
}

void AreaLightsInterpolate::AllocateAndCopy(AreaLightsInterpolate& rCurrent, const AreaLightsInterpolate& rPrevious)
{
	engine::Allocate(rCurrent, rPrevious, rCurrent.Members());
}

void AreaLightsInterpolate::Sync(game::FrameInterpolate& rFrameInterpolate, id_t id, const SyncData& rData)
{
	AreaLightsInterpolate& rAreaLights = rFrameInterpolate.areaLights;
	int64_t iIndex = rAreaLights.IdToIndex(id);

	rAreaLights.puiTypeIndices[iIndex] = rData.uiTypeIndex;
	rAreaLights.pVecVisiblePositions[0][iIndex] = rData.vecVisiblePositions[0];
	rAreaLights.pVecVisiblePositions[1][iIndex] = rData.vecVisiblePositions[1];
	rAreaLights.pVecVisiblePositions[2][iIndex] = rData.vecVisiblePositions[2];
	rAreaLights.pVecVisiblePositions[3][iIndex] = rData.vecVisiblePositions[3];
}

void AreaLightsInterpolate::Update([[maybe_unused]] game::FrameInterpolate& __restrict rFrameInterpolate, [[maybe_unused]] const game::Frame& __restrict rPreviousFrame, [[maybe_unused]] float fDeltaTime)
{
	AreaLightsInterpolate& __restrict rCurrent = rFrameInterpolate.areaLights;
	const AreaLightsInterpolate& rPrevious = rPreviousFrame.interpolate.areaLights;

	if (rCurrent.iCount == 0)
	{
		return;
	}

	// Note: Owner is responsible for writing all other data (positions, etc.) each frame via Sync()
	std::memcpy(rCurrent.puiTypeIndices, rPrevious.puiTypeIndices, static_cast<size_t>(rCurrent.iCount) * sizeof(uint8_t));
}

void AreaLightsPostRender::AllocateAndCopy(AreaLightsPostRender& rCurrent, const AreaLightsPostRender& rPrevious)
{
	engine::Allocate(rCurrent, rPrevious, rCurrent.Members());
}

void AreaLightsPostRender::Update([[maybe_unused]] game::Frame& __restrict rFrame, [[maybe_unused]] const game::Frame& __restrict rPreviousFrame, [[maybe_unused]] float fDeltaTime)
{
	AreaLightsPostRender& __restrict rCurrent = rFrame.postRender.areaLights;
	const AreaLightsPostRender& __restrict rPrevious = rPreviousFrame.postRender.areaLights;

	for (int64_t i = 0; i < rCurrent.iCount; ++i)
	{
		// Load
		area_lights_t id = rPrevious.puiIds[i];

		// Save
		rCurrent.puiIds[i] = id;
	}
}

void AreaLightsPostRender::PreCollision([[maybe_unused]] game::Frame& __restrict rFrame, [[maybe_unused]] const game::Frame& __restrict rPreviousFrame, [[maybe_unused]] float fDeltaTime)
{
}

void AreaLightsPostRender::Add(game::Frame& __restrict rFrame, area_lights_t& rId, uint8_t uiTypeIndex)
{
	ASSERT(!rId.IsValid());

	AreaLightsInterpolate& rInterpolate = rFrame.interpolate.areaLights;
	AreaLightsPostRender& rPostRender = rFrame.postRender.areaLights;

	GrowPairedCollections(rInterpolate, rPostRender, rInterpolate.Members(), rPostRender.Members());
	auto [uiSpawnIndex, newId] = AddIndexableElement(rInterpolate, rPostRender, rFrame.postRender);
	rId = newId;
	rPostRender.puiIds[uiSpawnIndex] = newId;
	rInterpolate.puiTypeIndices[uiSpawnIndex] = uiTypeIndex;
}

void AreaLightsPostRender::Remove(game::Frame& __restrict rFrame, area_lights_t& rId)
{
	ASSERT(rId.IsValid());

	AreaLightsInterpolate& rInterpolate = rFrame.interpolate.areaLights;
	AreaLightsPostRender& rPostRender = rFrame.postRender.areaLights;

	RemoveIndexableElement(rInterpolate, rPostRender, rId, rInterpolate.Members(), rPostRender.Members());

	rId = {};
}

void AreaLightsPostRender::PostCollision([[maybe_unused]] game::Frame& __restrict rFrame, [[maybe_unused]] const game::Frame& __restrict rPreviousFrame, [[maybe_unused]] float fDeltaTime)
{
}

void AreaLightsPostRender::AreaDamage([[maybe_unused]] game::Frame& __restrict rFrame, [[maybe_unused]] const game::Frame& __restrict rPreviousFrame, [[maybe_unused]] float fDeltaTime)
{
}

void AreaLightsPostRender::Destroy([[maybe_unused]] game::Frame& __restrict rFrame, [[maybe_unused]] float fDeltaTime)
{
}

void AreaLightsPostRender::Spawn([[maybe_unused]] game::Frame& __restrict rFrame, [[maybe_unused]] float fDeltaTime)
{
}

void AreaLightsInterpolate::Render([[maybe_unused]] const game::FrameInterpolate& __restrict rFrameInterpolate, [[maybe_unused]] int64_t iCommandBuffer)
{
	const AreaLightsInterpolate& rCurrent = rFrameInterpolate.areaLights;
	PROFILE_SET_COUNT(kCpuCounterAreaLights, rCurrent.iCount);

	if (rCurrent.iCount == 0)
	{
		WritePipelineIndirectBuffers(iCommandBuffer, 0);
		return;
	}

	ResizeBufferUpdateDescriptor(rCurrent, iCommandBuffer);

	auto pVisibleLightsLayouts = reinterpret_cast<shaders::VisibleLightQuadLayout*>(gpBufferManager->mDynamicStorageBuffers.at(kCrc | kVisibleLightsCrcFlag).at(iCommandBuffer).mpMappedMemory);
	auto pAreaLightsLayouts = reinterpret_cast<shaders::QuadLayout*>(gpBufferManager->mDynamicStorageBuffers.at(kCrc).at(iCommandBuffer).mpMappedMemory);

	int64_t iVisibleLightsRendered = 0;
	int64_t iAreaLightsRendered = 0;

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
		rAreaLayout.uiColor = rType.puiColors[0];

		// Increment both counters for dual rendering passes
		++iVisibleLightsRendered;
		++iAreaLightsRendered;
	}

	// Update profiling counters and write indirect draw buffers
	PROFILE_SET_COUNT(kCpuCounterAreaLightsRendered, iAreaLightsRendered);
	WritePipelineIndirectBuffers(iCommandBuffer, iAreaLightsRendered);
}

bool AreaLightsInterpolate::operator==(const AreaLightsInterpolate& rOther) const
{
	bool bEqual = true;
	bEqual &= common::BreakOnNotEqual<Collection>(*this, rOther);

	for (int64_t i = 0; i < iCount; ++i)
	{
		for (size_t j = 0; j < 4; ++j)
		{
			bEqual &= common::BreakOnNotEqual(pVecVisiblePositions[j][i], rOther.pVecVisiblePositions[j][i]);
		}
		bEqual &= common::BreakOnNotEqual(puiTypeIndices[i], rOther.puiTypeIndices[i]);
	}

	return bEqual;
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

} // namespace engine
