#include "Puffs.h"

#include "Frame/Frame.h"
#include "Frame/Render.h"
#include "Graphics/Graphics.h"
#include "Profile/ProfileManager.h"

namespace engine
{

void PuffsInterpolate::Register()
{
}

void PuffsInterpolate::GraphicsResources()
{
	AllocatePipelines();
}

void PuffsInterpolate::AllocateAndCopy(PuffsInterpolate& rCurrent, const PuffsInterpolate& rPrevious)
{
	Allocate(rCurrent, rPrevious, rCurrent.Members());

	if (rCurrent.iCount > 0)
	{
		std::memcpy(rCurrent.puiTypeIndices, rPrevious.puiTypeIndices, rCurrent.iCount * sizeof(rCurrent.puiTypeIndices[0]));
		std::memcpy(rCurrent.puiControllerTypeIndices, rPrevious.puiControllerTypeIndices, rCurrent.iCount * sizeof(rCurrent.puiControllerTypeIndices[0]));
		std::memcpy(rCurrent.pfStartTimes, rPrevious.pfStartTimes, rCurrent.iCount * sizeof(rCurrent.pfStartTimes[0]));
	}
}

void PuffsInterpolate::Update([[maybe_unused]] game::FrameInterpolate& __restrict rFrameInterpolate, [[maybe_unused]] const game::Frame& __restrict rPreviousFrame)
{
	PuffsInterpolate& __restrict rCurrent = rFrameInterpolate.puffs;
	const PuffsInterpolate& rPrevious = rPreviousFrame.interpolate.puffs;
	float fCurrentTime = rPreviousFrame.interpolate.fCurrentTime + rFrameInterpolate.fDeltaTime;

	for (int64_t i = 0; i < rCurrent.iCount; ++i)
	{
		// Load
		XMVECTOR vecPosition = rPrevious.pVecPositions[i];
		float fIntensity = rPrevious.pfIntensities[i];
		float fArea = rPrevious.pfAreas[i];
		float fRotation = rPrevious.pfRotations[i];

		// Load controller fields (copied in AllocateAndCopy)
		uint8_t uiControllerTypeIndex = rCurrent.puiControllerTypeIndices[i];
		float fStartTime = rCurrent.pfStartTimes[i];

		// Apply controller interpolation if this is a controlled puff
		if (uiControllerTypeIndex != kuiInvalidControllerType)
		{
			float fElapsedTime = fCurrentTime - fStartTime;
			const PuffControllerType& rController = PuffsInterpolate::GetControllerType(uiControllerTypeIndex);
			PuffKeyframe interpolated = InterpolatePuffKeyframes(rController, fElapsedTime);

			// Map PuffKeyframe fields to puff properties
			fArea = interpolated.fArea;
			fIntensity = interpolated.fIntensity;
			fRotation = interpolated.fRotation;
		}

		// Save
		rCurrent.pVecPositions[i] = vecPosition;
		rCurrent.pfIntensities[i] = fIntensity;
		rCurrent.pfAreas[i] = fArea;
		rCurrent.pfRotations[i] = fRotation;
	}
}

void PuffsPostRender::AllocateAndCopy(PuffsPostRender& rCurrent, const PuffsPostRender& rPrevious)
{
	Allocate(rCurrent, rPrevious, rCurrent.Members());
}

void PuffsPostRender::Update([[maybe_unused]] game::Frame& __restrict rFrame, [[maybe_unused]] const game::Frame& __restrict rPreviousFrame)
{
}

void PuffsPostRender::PreCollision([[maybe_unused]] game::Frame& __restrict rFrame, [[maybe_unused]] const game::Frame& __restrict rPreviousFrame)
{
}

void XM_CALLCONV PuffsPostRender::AddControlled(game::Frame& __restrict rFrame, float fCurrentTime, uint8_t uiControllerTypeIndex, FXMVECTOR vecPosition)
{
	PuffsInterpolate& rInterpolate = rFrame.interpolate.puffs;
	PuffsPostRender& rPostRender = rFrame.postRender.puffs;

	// Get controller type
	const PuffControllerType& rController = PuffsInterpolate::GetControllerType(uiControllerTypeIndex);

	GrowPairedCollections(rInterpolate, rPostRender, rInterpolate.Members(), rPostRender.Members());
	int64_t iSpawnIndex = AddElement(rInterpolate, rPostRender);

	// Set position and base type from controller
	rInterpolate.pVecPositions[iSpawnIndex] = vecPosition;
	rInterpolate.puiTypeIndices[iSpawnIndex] = rController.uiBaseTypeIndex;

	// Initialize per-instance values from first keyframe
	rInterpolate.pfAreas[iSpawnIndex] = rController.keyframes[0].fArea;
	rInterpolate.pfIntensities[iSpawnIndex] = rController.keyframes[0].fIntensity;
	rInterpolate.pfRotations[iSpawnIndex] = rController.keyframes[0].fRotation;

	// Set controller fields
	rInterpolate.puiControllerTypeIndices[iSpawnIndex] = uiControllerTypeIndex;
	rInterpolate.pfStartTimes[iSpawnIndex] = fCurrentTime;
}

void PuffsPostRender::PostCollision([[maybe_unused]] game::Frame& __restrict rFrame, [[maybe_unused]] const game::Frame& __restrict rPreviousFrame)
{
}

void PuffsPostRender::AreaDamage([[maybe_unused]] game::Frame& __restrict rFrame, [[maybe_unused]] const game::Frame& __restrict rPreviousFrame)
{
}

void PuffsPostRender::Spawn([[maybe_unused]] game::Frame& __restrict rFrame)
{
}

void PuffsPostRender::Destroy(game::Frame& __restrict rFrame)
{
	PuffsInterpolate& rInterpolate = rFrame.interpolate.puffs;
	PuffsPostRender& rPostRender = rFrame.postRender.puffs;

	float fCurrentTime = rFrame.interpolate.fCurrentTime;

	for (int64_t i = 0; i < rInterpolate.iCount; ++i)
	{
		uint8_t uiControllerTypeIndex = rInterpolate.puiControllerTypeIndices[i];

		// Skip non-controlled puffs (shouldn't exist, but defensive)
		if (uiControllerTypeIndex == kuiInvalidControllerType)
		{
			continue;
		}

		const PuffControllerType& rController = PuffsInterpolate::GetControllerType(uiControllerTypeIndex);

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
			DestroyElement(rInterpolate, rPostRender, i, rInterpolate.Members(), rPostRender.Members());
		}
	}
}

void PuffsInterpolate::Render([[maybe_unused]] const game::FrameInterpolate& __restrict rFrameInterpolate, [[maybe_unused]] int64_t iCommandBuffer)
{
	const PuffsInterpolate& rCurrent = rFrameInterpolate.puffs;
	gpProfileManager->SetCount(kCpuCounterPuffs, rCurrent.iCount);

	if (rCurrent.iCount == 0)
	{
		WritePipelineIndirectBuffers(iCommandBuffer, 0);
		return;
	}

	ResizeBufferUpdateDescriptor(rCurrent, iCommandBuffer);

	auto [pPuffsLayouts, iBufferCapacity] = gpBufferManager->GetDynamicStorageBuffer<shaders::AxisAlignedQuadLayout>(kCrc, iCommandBuffer);
	ASSERT(rCurrent.iCount <= iBufferCapacity);

	int64_t iPuffsRendered = 0;

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
		f4Params.y = fIntensity;  // Smoke.frag uses pow(f4Params.y, globalLayout.f4SmokeTwo.z)
		f4Params.w = fRotation;   // Smoke.frag uses this for Rotate()
		BuildAxisAlignedQuad(pPuffsLayouts[iPuffsRendered], f4Position, fArea, f4Params, rType.uiColor);

		++iPuffsRendered;
	}

	gpProfileManager->SetCount(kCpuCounterPuffsRendered, iPuffsRendered);
	WritePipelineIndirectBuffers(iCommandBuffer, iPuffsRendered);
}

bool PuffsInterpolate::operator==(const PuffsInterpolate& rOther) const
{
	bool bEqual = true;
	bEqual &= common::BreakOnNotEqual<Collection>(*this, rOther);

	for (int64_t i = 0; i < iCount; ++i)
	{
		bEqual &= common::BreakOnNotEqual(puiTypeIndices[i], rOther.puiTypeIndices[i]);
		bEqual &= common::BreakOnNotEqual(pVecPositions[i], rOther.pVecPositions[i]);
		bEqual &= common::BreakOnNotEqual(pfIntensities[i], rOther.pfIntensities[i]);
		bEqual &= common::BreakOnNotEqual(pfAreas[i], rOther.pfAreas[i]);
		bEqual &= common::BreakOnNotEqual(pfRotations[i], rOther.pfRotations[i]);
		bEqual &= common::BreakOnNotEqual(puiControllerTypeIndices[i], rOther.puiControllerTypeIndices[i]);
		bEqual &= common::BreakOnNotEqual(pfStartTimes[i], rOther.pfStartTimes[i]);
	}

	return bEqual;
}

bool PuffsPostRender::operator==(const PuffsPostRender& rOther) const
{
	bool bEqual = true;
	bEqual &= common::BreakOnNotEqual<Collection>(*this, rOther);
	return bEqual;
}

} // namespace engine
