#include "PointLights.h"

#include "Frame/Frame.h"
#include "Frame/Render.h"
#include "Graphics/Graphics.h"
#include "Profile/ProfileManager.h"

namespace engine
{

void PointLightsInterpolate::Register()
{
}

void PointLightsInterpolate::GraphicsResources()
{
	AllocatePipelines();
}

void PointLightsInterpolate::AllocateAndCopy(PointLightsInterpolate& rCurrent, const PointLightsInterpolate& rPrevious)
{
	Allocate(rCurrent, rPrevious, rCurrent.Members());

	if (rCurrent.iCount > 0)
	{
		std::memcpy(rCurrent.puiTypeIndices, rPrevious.puiTypeIndices, rCurrent.iCount * sizeof(rCurrent.puiTypeIndices[0]));
		std::memcpy(rCurrent.puiControllerTypeIndices, rPrevious.puiControllerTypeIndices, rCurrent.iCount * sizeof(rCurrent.puiControllerTypeIndices[0]));
		std::memcpy(rCurrent.pfStartTimes, rPrevious.pfStartTimes, rCurrent.iCount * sizeof(rCurrent.pfStartTimes[0]));
		std::memcpy(rCurrent.pfBaseRotations, rPrevious.pfBaseRotations, rCurrent.iCount * sizeof(rCurrent.pfBaseRotations[0]));
	}
}

void PointLightsInterpolate::Sync(game::FrameInterpolate& rFrameInterpolate, id_t id, const SyncData& rData)
{
	PointLightsInterpolate& rPointLights = rFrameInterpolate.pointLights;
	int64_t iIndex = rPointLights.IdToIndex(id);

	rPointLights.pVecPositions[iIndex] = rData.vecPosition;
	rPointLights.pfVisibleAreas[iIndex] = rData.fVisibleArea;
	rPointLights.pfVisibleIntensities[iIndex] = rData.fVisibleIntensity;
	rPointLights.pfLightingAreas[iIndex] = rData.fLightingArea;
	rPointLights.pfLightingIntensities[iIndex] = rData.fLightingIntensity;
	rPointLights.pfRotations[iIndex] = rData.fRotation;
}

void PointLightsInterpolate::Update([[maybe_unused]] game::FrameInterpolate& __restrict rFrameInterpolate, [[maybe_unused]] const game::Frame& __restrict rPreviousFrame)
{
	PointLightsInterpolate& __restrict rCurrent = rFrameInterpolate.pointLights;
	const PointLightsInterpolate& rPrevious = rPreviousFrame.interpolate.pointLights;
	float fCurrentTime = rPreviousFrame.interpolate.fCurrentTime + rFrameInterpolate.fDeltaTime;

	if (rCurrent.iCount == 0)
	{
		return;
	}

	// Note: Owner is responsible for writing position each frame via IdToIndex

	for (int64_t i = 0; i < rCurrent.iCount; ++i)
	{
		// Load
		XMVECTOR vecPosition = rPrevious.pVecPositions[i];
		float fRotation = rPrevious.pfRotations[i];
		float fVisibleArea = rPrevious.pfVisibleAreas[i];
		float fVisibleIntensity = rPrevious.pfVisibleIntensities[i];
		float fLightingArea = rPrevious.pfLightingAreas[i];
		float fLightingIntensity = rPrevious.pfLightingIntensities[i];

		// Load controller fields (copied in AllocateAndCopy)
		uint8_t uiControllerTypeIndex = rCurrent.puiControllerTypeIndices[i];
		float fStartTime = rCurrent.pfStartTimes[i];
		float fBaseRotation = rCurrent.pfBaseRotations[i];

		// Apply controller interpolation if this is a controlled light
		if (uiControllerTypeIndex != kuiInvalidControllerType)
		{
			float fElapsedTime = fCurrentTime - fStartTime;
			const ControllerType& rController = sControllerTypes.at(uiControllerTypeIndex);
			ControllerKeyframe interpolated = InterpolateKeyframes(rController, fElapsedTime);

			fVisibleArea = interpolated.fVisibleArea;
			fVisibleIntensity = interpolated.fVisibleIntensity;
			fLightingArea = interpolated.fLightingArea;
			fLightingIntensity = interpolated.fLightingIntensity;
			fRotation = fBaseRotation + interpolated.fRotation;
		}

		// Save
		rCurrent.pVecPositions[i] = vecPosition;
		rCurrent.pfRotations[i] = fRotation;
		rCurrent.pfVisibleAreas[i] = fVisibleArea;
		rCurrent.pfVisibleIntensities[i] = fVisibleIntensity;
		rCurrent.pfLightingAreas[i] = fLightingArea;
		rCurrent.pfLightingIntensities[i] = fLightingIntensity;
	}
}

void PointLightsPostRender::AllocateAndCopy(PointLightsPostRender& rCurrent, const PointLightsPostRender& rPrevious)
{
	Allocate(rCurrent, rPrevious, rCurrent.Members());

	if (rCurrent.iCount > 0)
	{
		std::memcpy(rCurrent.puiIds, rPrevious.puiIds, rCurrent.iCount * sizeof(rCurrent.puiIds[0]));
	}
}

void PointLightsPostRender::Update([[maybe_unused]] game::Frame& __restrict rFrame, [[maybe_unused]] const game::Frame& __restrict rPreviousFrame)
{
}

void PointLightsPostRender::PreCollision([[maybe_unused]] game::Frame& __restrict rFrame, [[maybe_unused]] const game::Frame& __restrict rPreviousFrame)
{
}

void PointLightsPostRender::Add(game::Frame& __restrict rFrame, point_lights_t& rId, uint8_t uiTypeIndex)
{
	ASSERT(!rId.IsValid());

	PointLightsInterpolate& rInterpolate = rFrame.interpolate.pointLights;
	PointLightsPostRender& rPostRender = rFrame.postRender.pointLights;

	GrowPairedCollections(rInterpolate, rPostRender, rInterpolate.Members(), rPostRender.Members());
	auto [uiSpawnIndex, newId] = AddIndexableElement(rInterpolate, rPostRender, rFrame.postRender);
	rId = newId;
	rPostRender.puiIds[uiSpawnIndex] = newId;

	// Zero-init all members
	rInterpolate.pVecPositions[uiSpawnIndex] = XMVectorZero();
	rInterpolate.puiTypeIndices[uiSpawnIndex] = uiTypeIndex;
	rInterpolate.pfRotations[uiSpawnIndex] = 0.0f;
	rInterpolate.pfVisibleAreas[uiSpawnIndex] = 0.0f;
	rInterpolate.pfVisibleIntensities[uiSpawnIndex] = 0.0f;
	rInterpolate.pfLightingAreas[uiSpawnIndex] = 0.0f;
	rInterpolate.pfLightingIntensities[uiSpawnIndex] = 0.0f;

	// Controller fields: not controlled
	rInterpolate.puiControllerTypeIndices[uiSpawnIndex] = kuiInvalidControllerType;
	rInterpolate.pfStartTimes[uiSpawnIndex] = 0.0f;
	rInterpolate.pfBaseRotations[uiSpawnIndex] = 0.0f;
}

void PointLightsPostRender::Remove(game::Frame& __restrict rFrame, point_lights_t& rId)
{
	ASSERT(rId.IsValid());

	PointLightsInterpolate& rInterpolate = rFrame.interpolate.pointLights;
	PointLightsPostRender& rPostRender = rFrame.postRender.pointLights;

	RemoveIndexableElement(rInterpolate, rPostRender, rId, rInterpolate.Members(), rPostRender.Members());

	rId = {};
}

void XM_CALLCONV PointLightsPostRender::AddControlled(game::Frame& __restrict rFrame, float fCurrentTime, uint8_t uiControllerTypeIndex, FXMVECTOR vecPosition, float fRotation)
{
	PointLightsInterpolate& rInterpolate = rFrame.interpolate.pointLights;
	PointLightsPostRender& rPostRender = rFrame.postRender.pointLights;

	// Get controller type and base type
	const ControllerType& rController = PointLightsInterpolate::sControllerTypes.at(uiControllerTypeIndex);

	GrowPairedCollections(rInterpolate, rPostRender, rInterpolate.Members(), rPostRender.Members());
	auto [uiSpawnIndex, newId] = AddIndexableElement(rInterpolate, rPostRender, rFrame.postRender);
	rPostRender.puiIds[uiSpawnIndex] = newId;

	// Set position and base type from controller
	rInterpolate.pVecPositions[uiSpawnIndex] = vecPosition;
	rInterpolate.puiTypeIndices[uiSpawnIndex] = rController.uiBaseTypeIndex;

	// Initialize per-instance values from first keyframe
	rInterpolate.pfVisibleAreas[uiSpawnIndex] = rController.keyframes[0].fVisibleArea;
	rInterpolate.pfVisibleIntensities[uiSpawnIndex] = rController.keyframes[0].fVisibleIntensity;
	rInterpolate.pfLightingAreas[uiSpawnIndex] = rController.keyframes[0].fLightingArea;
	rInterpolate.pfLightingIntensities[uiSpawnIndex] = rController.keyframes[0].fLightingIntensity;
	rInterpolate.pfRotations[uiSpawnIndex] = fRotation + rController.keyframes[0].fRotation;

	// Set controller fields
	rInterpolate.puiControllerTypeIndices[uiSpawnIndex] = uiControllerTypeIndex;
	rInterpolate.pfStartTimes[uiSpawnIndex] = fCurrentTime;
	rInterpolate.pfBaseRotations[uiSpawnIndex] = fRotation;
}

void PointLightsPostRender::PostCollision([[maybe_unused]] game::Frame& __restrict rFrame, [[maybe_unused]] const game::Frame& __restrict rPreviousFrame)
{
}

void PointLightsPostRender::AreaDamage([[maybe_unused]] game::Frame& __restrict rFrame, [[maybe_unused]] const game::Frame& __restrict rPreviousFrame)
{
}

void PointLightsPostRender::Spawn([[maybe_unused]] game::Frame& __restrict rFrame)
{
}

void PointLightsPostRender::Destroy(game::Frame& __restrict rFrame)
{
	PointLightsInterpolate& rInterpolate = rFrame.interpolate.pointLights;
	PointLightsPostRender& rPostRender = rFrame.postRender.pointLights;

	float fCurrentTime = rFrame.interpolate.fCurrentTime;

	for (int64_t i = 0; i < rInterpolate.iCount; ++i)
	{
		uint8_t uiControllerTypeIndex = rInterpolate.puiControllerTypeIndices[i];

		// Skip non-controlled lights
		if (uiControllerTypeIndex == kuiInvalidControllerType)
		{
			continue;
		}

		const ControllerType& rController = PointLightsInterpolate::sControllerTypes.at(uiControllerTypeIndex);

		// Skip if not auto-destroy
		if (!rController.bDestroysSelf)
		{
			continue;
		}

		// Check if animation has expired
		float fStartTime = rInterpolate.pfStartTimes[i];
		float fElapsedTime = fCurrentTime - fStartTime;
		bool bExpired = fElapsedTime > rController.pfTimes[rController.uiKeyframeCount - 1];

		if (bExpired) [[unlikely]]
		{
			// Remove the point light using swap-and-pop
			point_lights_t id = rPostRender.puiIds[i];
			engine::RemoveIndexableElement(rInterpolate, rPostRender, id, rInterpolate.Members(), rPostRender.Members());
			--i; // Re-check this index (new element swapped in)
		}
	}
}

void PointLightsInterpolate::Render([[maybe_unused]] const game::FrameInterpolate& __restrict rFrameInterpolate, [[maybe_unused]] int64_t iCommandBuffer)
{
	const PointLightsInterpolate& rCurrent = rFrameInterpolate.pointLights;
	gpProfileManager->SetCount(kCpuCounterPointLights, rCurrent.iCount);

	if (rCurrent.iCount == 0)
	{
		WritePipelineIndirectBuffers(iCommandBuffer, 0);
		return;
	}

	ResizeBufferUpdateDescriptor(rCurrent, iCommandBuffer);

	auto [pPointLightsLayouts, iPointLightsBufferCapacity] = gpBufferManager->GetDynamicStorageBuffer<shaders::AxisAlignedQuadLayout>(kCrc, iCommandBuffer);
	auto [pVisibleLightsLayouts, iVisibleLightsBufferCapacity] = gpBufferManager->GetDynamicStorageBuffer<shaders::VisibleLightQuadLayout>(kCrc | kVisibleLightsCrcFlag, iCommandBuffer);
	ASSERT(rCurrent.iCount <= iPointLightsBufferCapacity);
	ASSERT(rCurrent.iCount <= iVisibleLightsBufferCapacity);

	int64_t iPointLightsRendered = 0;

	for (int64_t i = 0; i < rCurrent.iCount; ++i)
	{
		// Load
		XMVECTOR vecPosition = rCurrent.pVecPositions[i];
		const PointLightsType& rType = PointLightsInterpolate::GetType(rCurrent.puiTypeIndices[i]);
		float fRotation = rCurrent.pfRotations[i];
		float fLightingArea = rCurrent.pfLightingAreas[i];
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
		XMFLOAT4A f4Misc {};
		f4Misc.x = CrcToIndex(rType.crc);
		f4Misc.y = fLightingIntensity;
		f4Misc.z = fRotation;
		BuildAxisAlignedQuad(pPointLightsLayouts[iPointLightsRendered], f4Position, fLightingArea, f4Misc, rType.uiColor);

		// Build VisibleLightQuadLayout for visible sprite pass (uses original world position)
		float fVisibleArea = rCurrent.pfVisibleAreas[i];
		float fVisibleIntensity = rCurrent.pfVisibleIntensities[i];

		shaders::VisibleLightQuadLayout& rVisibleLayout = pVisibleLightsLayouts[iPointLightsRendered];

		// 4 corners for billboard-style quad
		rVisibleLayout.pf4Vertices[0] = {f4VisiblePosition.x - fVisibleArea, f4VisiblePosition.y + fVisibleArea, f4VisiblePosition.z, 1.0f};
		rVisibleLayout.pf4Vertices[1] = {f4VisiblePosition.x + fVisibleArea, f4VisiblePosition.y + fVisibleArea, f4VisiblePosition.z, 1.0f};
		rVisibleLayout.pf4Vertices[2] = {f4VisiblePosition.x - fVisibleArea, f4VisiblePosition.y - fVisibleArea, f4VisiblePosition.z, 1.0f};
		rVisibleLayout.pf4Vertices[3] = {f4VisiblePosition.x + fVisibleArea, f4VisiblePosition.y - fVisibleArea, f4VisiblePosition.z, 1.0f};

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
		rVisibleLayout.uiTextureIndex = static_cast<uint32_t>(CrcToIndex(rType.crc));

		++iPointLightsRendered;
	}

	gpProfileManager->SetCount(kCpuCounterPointLightsRendered, iPointLightsRendered);
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
		bEqual &= common::BreakOnNotEqual(puiControllerTypeIndices[i], rOther.puiControllerTypeIndices[i]);
		bEqual &= common::BreakOnNotEqual(pfStartTimes[i], rOther.pfStartTimes[i]);
		bEqual &= common::BreakOnNotEqual(pfBaseRotations[i], rOther.pfBaseRotations[i]);
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
