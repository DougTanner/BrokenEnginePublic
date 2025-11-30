#include "PointLights.h"

#include "Frame/Frame.h"
#include "Graphics/Graphics.h"
#include "Profile/ProfileManager.h"

namespace engine
{

void PointLightsInterpolate::Update([[maybe_unused]] PointLightsInterpolate& __restrict rCurrent, [[maybe_unused]] const PointLightsInterpolate& __restrict rPrevious)
{
	engine::ReallocateAndCopyMetadata(rCurrent, rPrevious, rCurrent.Members());

	if (rCurrent.pData == nullptr)
	{
		return;
	}

	for (int64_t i = 0; i < rCurrent.iCount; ++i)
	{
		// Load
		uint8_t uiTypeIndex = rPrevious.puiTypeIndices[i];
		XMVECTOR vecPosition = rPrevious.pVecPositions[i];
		float fRotation = rPrevious.pfRotations[i];
		float fVisibleArea = rPrevious.pfVisibleAreas[i];
		float fVisibleIntensity = rPrevious.pfVisibleIntensities[i];
		float fLightingArea = rPrevious.pfLightingAreas[i];
		float fLightingIntensity = rPrevious.pfLightingIntensities[i];

		// Save
		rCurrent.puiTypeIndices[i] = uiTypeIndex;
		rCurrent.pVecPositions[i] = vecPosition;
		rCurrent.pfRotations[i] = fRotation;
		rCurrent.pfVisibleAreas[i] = fVisibleArea;
		rCurrent.pfVisibleIntensities[i] = fVisibleIntensity;
		rCurrent.pfLightingAreas[i] = fLightingArea;
		rCurrent.pfLightingIntensities[i] = fLightingIntensity;
	}
}

void PointLightsInterpolate::Sync([[maybe_unused]] PointLightsInterpolate& __restrict rCurrent, [[maybe_unused]] const game::Frame& __restrict rPreviousFrame, [[maybe_unused]] float fDeltaTime)
{
}

void PointLightsPostRender::Update([[maybe_unused]] PointLightsPostRender& __restrict rCurrent, [[maybe_unused]] const PointLightsPostRender& __restrict rPrevious)
{
	engine::ReallocateAndCopyMetadata(rCurrent, rPrevious, rCurrent.Members());

	for (int64_t i = 0; i < rCurrent.iCount; ++i)
	{
		// Load
		point_lights_t id = rPrevious.puiIds[i];

		// Save
		rCurrent.puiIds[i] = id;
	}
}

uint8_t PointLightsPostRender::RegisterType(const PointLightsInterpolate::Type& rType)
{
	PointLightsInterpolate::sTypes.push_back(rType);
	return static_cast<uint8_t>(PointLightsInterpolate::sTypes.size() - 1);
}

const PointLightsInterpolate::Type& PointLightsPostRender::GetType(uint8_t uiIndex)
{
	return PointLightsInterpolate::sTypes.at(uiIndex);
}

point_lights_t PointLightsPostRender::Add(game::Frame& __restrict rFrame, uint8_t uiTypeIndex)
{
	PointLightsInterpolate& rInterpolate = rFrame.interpolate.pointLights;
	PointLightsPostRender& rPostRender = rFrame.postRender.pointLights;

	engine::GrowPairedCollections(rInterpolate, rPostRender, rInterpolate.Members(), rPostRender.Members());
	auto [uiSpawnIndex, newId] = engine::AddIndexableElement(rInterpolate, rPostRender, rFrame.postRender);

	// Get Type defaults
	const PointLightsInterpolate::Type& rType = PointLightsInterpolate::sTypes.at(uiTypeIndex);

	// Defaults
	rInterpolate.pVecPositions[uiSpawnIndex] = XMVectorZero();
	rInterpolate.puiTypeIndices[uiSpawnIndex] = uiTypeIndex;
	rInterpolate.pfRotations[uiSpawnIndex] = 0.0f;

	// Initialize per-instance values from Type defaults
	rInterpolate.pfVisibleAreas[uiSpawnIndex] = rType.fVisibleArea;
	rInterpolate.pfVisibleIntensities[uiSpawnIndex] = rType.fVisibleIntensity;
	rInterpolate.pfLightingAreas[uiSpawnIndex] = rType.fLightingArea;
	rInterpolate.pfLightingIntensities[uiSpawnIndex] = rType.fLightingIntensity;

	rPostRender.puiIds[uiSpawnIndex] = newId;

	return newId;
}

void PointLightsPostRender::Remove(game::Frame& __restrict rFrame, point_lights_t id)
{
	PointLightsInterpolate& rInterpolate = rFrame.interpolate.pointLights;
	PointLightsPostRender& rPostRender = rFrame.postRender.pointLights;

	engine::RemoveIndexableElement(rInterpolate, rPostRender, id, rInterpolate.Members(), rPostRender.Members());
}

void PointLightsInterpolate::Render([[maybe_unused]] const game::Frame& __restrict rFrame, [[maybe_unused]] int64_t iCommandBuffer)
{
	const PointLightsInterpolate& rCurrent = rFrame.interpolate.pointLights;
	PROFILE_SET_COUNT(kCpuCounterPointLights, rCurrent.iCount);

	if (rCurrent.iCount == 0)
	{
		WritePipelineIndirectBuffers(iCommandBuffer, 0);
		return;
	}

	ResizeBufferUpdateDescriptor(rCurrent, iCommandBuffer);

	auto pPointLightsLayouts = reinterpret_cast<shaders::AxisAlignedQuadLayout*>(gpBufferManager->mDynamicStorageBuffers.at(kCrc).at(iCommandBuffer).mpMappedMemory);

	int64_t iPointLightsRendered = 0;

	for (int64_t i = 0; i < rCurrent.iCount; ++i)
	{
		// Load
		XMVECTOR vecPosition = rCurrent.pVecPositions[i];
		const PointLightsInterpolate::Type& rType = PointLightsInterpolate::sTypes.at(rCurrent.puiTypeIndices[i]);
		float fRotation = rCurrent.pfRotations[i];
		float fLightingArea = rCurrent.pfLightingAreas[i];
		float fLightingIntensity = rCurrent.pfLightingIntensities[i];

		// Visibility culling
		XMFLOAT4A f4Position {};
		XMStoreFloat4A(&f4Position, vecPosition);
		if (f4Position.x < game::gpCamera->f4RenderVisibleArea.x || f4Position.x > game::gpCamera->f4RenderVisibleArea.z ||
		    f4Position.y > game::gpCamera->f4RenderVisibleArea.y || f4Position.y < game::gpCamera->f4RenderVisibleArea.w)
		{
			continue;
		}

		// Calculate terrain elevation and project to base height
		float fElevation = gpIslands->GlobalElevation(vecPosition);
		XMVECTOR vecBasePosition = common::ToBaseHeight(vecPosition, game::gpCamera->mVecEyePosition, std::max(fElevation, gBaseHeight.Get()));
		XMStoreFloat4A(&f4Position, vecBasePosition);

		// Build AxisAlignedQuadLayout for lighting pass
		XMFLOAT4 f4VertexRect = {f4Position.x - fLightingArea, f4Position.y + fLightingArea, 2.0f * fLightingArea, -2.0f * fLightingArea};
		XMFLOAT4 f4TextureRect = {0.0f, 0.0f, 1.0f, 1.0f};

		XMFLOAT4A f4Misc {};
		f4Misc.x = CrcToIndex(rType.crc);
		f4Misc.y = fLightingIntensity;
		f4Misc.z = fRotation;

		shaders::AxisAlignedQuadLayout& rLightingQuadLayout = pPointLightsLayouts[iPointLightsRendered];
		rLightingQuadLayout.f4VertexRect = f4VertexRect;
		rLightingQuadLayout.f4TextureRect = f4TextureRect;
		rLightingQuadLayout.f4Misc = f4Misc;
		rLightingQuadLayout.uiColor = rType.uiColor;

		++iPointLightsRendered;
	}

	PROFILE_SET_COUNT(kCpuCounterPointLightsRendered, iPointLightsRendered);
	WritePipelineIndirectBuffers(iCommandBuffer, iPointLightsRendered);
}

bool PointLightsInterpolate::operator==(const PointLightsInterpolate& rOther) const
{
	bool bEqual = true;
	bEqual &= common::BreakOnNotEqual<Collection>(*this, rOther);

	for (int64_t i = 0; i < iCount; ++i)
	{
		bEqual &= common::BreakOnNotEqual(pVecPositions[i], rOther.pVecPositions[i]);
		bEqual &= common::BreakOnNotEqual(puiTypeIndices[i], rOther.puiTypeIndices[i]);
		bEqual &= common::BreakOnNotEqual(pfRotations[i], rOther.pfRotations[i]);
		bEqual &= common::BreakOnNotEqual(pfVisibleAreas[i], rOther.pfVisibleAreas[i]);
		bEqual &= common::BreakOnNotEqual(pfVisibleIntensities[i], rOther.pfVisibleIntensities[i]);
		bEqual &= common::BreakOnNotEqual(pfLightingAreas[i], rOther.pfLightingAreas[i]);
		bEqual &= common::BreakOnNotEqual(pfLightingIntensities[i], rOther.pfLightingIntensities[i]);
	}

	return bEqual;
}

bool PointLightsPostRender::operator==(const PointLightsPostRender& rOther) const
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
