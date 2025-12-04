#include "PointLights.h"

#include "Frame/Frame.h"
#include "Graphics/Graphics.h"
#include "Profile/ProfileManager.h"

namespace engine
{

void PointLightsInterpolate::Update([[maybe_unused]] PointLightsInterpolate& __restrict rCurrent, [[maybe_unused]] const game::Frame& __restrict rPreviousFrame, [[maybe_unused]] float fDeltaTime)
{
	const PointLightsInterpolate& rPrevious = rPreviousFrame.interpolate.pointLights;
	float fCurrentTime = rPreviousFrame.interpolate.fCurrentTime + fDeltaTime;

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

		// Load controller fields
		uint8_t uiControllerTypeIndex = rPrevious.puiControllerTypeIndices[i];
		float fStartTime = rPrevious.pfStartTimes[i];
		float fBaseRotation = rPrevious.pfBaseRotations[i];

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
		rCurrent.puiTypeIndices[i] = uiTypeIndex;
		rCurrent.pVecPositions[i] = vecPosition;
		rCurrent.pfRotations[i] = fRotation;
		rCurrent.pfVisibleAreas[i] = fVisibleArea;
		rCurrent.pfVisibleIntensities[i] = fVisibleIntensity;
		rCurrent.pfLightingAreas[i] = fLightingArea;
		rCurrent.pfLightingIntensities[i] = fLightingIntensity;

		// Save controller fields
		rCurrent.puiControllerTypeIndices[i] = uiControllerTypeIndex;
		rCurrent.pfStartTimes[i] = fStartTime;
		rCurrent.pfBaseRotations[i] = fBaseRotation;
	}
}

void PointLightsPostRender::Update([[maybe_unused]] PointLightsPostRender& __restrict rCurrent, [[maybe_unused]] const PointLightsPostRender& __restrict rPrevious)
{
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

void PointLightsPostRender::Add(game::Frame& __restrict rFrame, point_lights_t& rId)
{
	PointLightsInterpolate& rInterpolate = rFrame.interpolate.pointLights;
	PointLightsPostRender& rPostRender = rFrame.postRender.pointLights;

	engine::GrowPairedCollections(rInterpolate, rPostRender, rInterpolate.Members(), rPostRender.Members());
	auto [uiSpawnIndex, newId] = engine::AddIndexableElement(rInterpolate, rPostRender, rFrame.postRender);
	rId = newId;
	rPostRender.puiIds[uiSpawnIndex] = newId;

	// Zero-init all members
	rInterpolate.pVecPositions[uiSpawnIndex] = XMVectorZero();
	rInterpolate.puiTypeIndices[uiSpawnIndex] = 0;
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
	PointLightsInterpolate& rInterpolate = rFrame.interpolate.pointLights;
	PointLightsPostRender& rPostRender = rFrame.postRender.pointLights;

	engine::RemoveIndexableElement(rInterpolate, rPostRender, rId, rInterpolate.Members(), rPostRender.Members());

	rId = {};
}

void XM_CALLCONV PointLightsPostRender::AddControlled(game::Frame& __restrict rFrame, float fCurrentTime, uint8_t uiControllerTypeIndex, FXMVECTOR vecPosition, float fRotation)
{
	PointLightsInterpolate& rInterpolate = rFrame.interpolate.pointLights;
	PointLightsPostRender& rPostRender = rFrame.postRender.pointLights;

	// Get controller type and base type
	const ControllerType& rController = PointLightsInterpolate::sControllerTypes.at(uiControllerTypeIndex);

	engine::GrowPairedCollections(rInterpolate, rPostRender, rInterpolate.Members(), rPostRender.Members());
	auto [uiSpawnIndex, newId] = engine::AddIndexableElement(rInterpolate, rPostRender, rFrame.postRender);
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

void PointLightsPostRender::Destroy(game::Frame& __restrict rFrame, float fCurrentTime)
{
	PointLightsInterpolate& rInterpolate = rFrame.interpolate.pointLights;
	PointLightsPostRender& rPostRender = rFrame.postRender.pointLights;

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
	PROFILE_SET_COUNT(kCpuCounterPointLights, rCurrent.iCount);

	if (rCurrent.iCount == 0)
	{
		WritePipelineIndirectBuffers(iCommandBuffer, 0);
		return;
	}

	ResizeBufferUpdateDescriptor(rCurrent, iCommandBuffer);

	auto pPointLightsLayouts = reinterpret_cast<shaders::AxisAlignedQuadLayout*>(gpBufferManager->mDynamicStorageBuffers.at(kCrc).at(iCommandBuffer).mpMappedMemory);
	auto pVisibleLightsLayouts = reinterpret_cast<shaders::VisibleLightQuadLayout*>(gpBufferManager->mDynamicStorageBuffers.at(kCrc | kVisibleLightsCrcFlag).at(iCommandBuffer).mpMappedMemory);

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

		// Store original world position for visible light (before base height projection)
		XMFLOAT4A f4VisiblePosition = f4Position;

		// Calculate terrain elevation and project to base height for lighting
		float fElevation = gpIslands->GlobalElevation(vecPosition);
		XMVECTOR vecBasePosition = common::ToBaseHeight(vecPosition, game::gpCamera->mVecEyePosition, std::max(fElevation, gBaseHeight.Get()));
		XMStoreFloat4A(&f4Position, vecBasePosition);

		// Build AxisAlignedQuadLayout for lighting pass (uses base height projected position)
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
