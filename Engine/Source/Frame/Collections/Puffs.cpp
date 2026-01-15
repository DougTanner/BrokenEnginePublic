#include "Puffs.h"

#include "Frame/Frame.h"
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
		std::memcpy(rCurrent.puiTypeIndices, rPrevious.puiTypeIndices, static_cast<size_t>(rCurrent.iCount) * sizeof(uint8_t));
		std::memcpy(rCurrent.puiControllerTypeIndices, rPrevious.puiControllerTypeIndices, static_cast<size_t>(rCurrent.iCount) * sizeof(uint8_t));
		std::memcpy(rCurrent.pfStartTimes, rPrevious.pfStartTimes, static_cast<size_t>(rCurrent.iCount) * sizeof(float));
	}
}

void PuffsInterpolate::Update([[maybe_unused]] game::FrameInterpolate& __restrict rFrameInterpolate, [[maybe_unused]] const game::Frame& __restrict rPreviousFrame, [[maybe_unused]] float fDeltaTime)
{
	PuffsInterpolate& __restrict rCurrent = rFrameInterpolate.puffs;
	const PuffsInterpolate& rPrevious = rPreviousFrame.interpolate.puffs;
	float fCurrentTime = rPreviousFrame.interpolate.fCurrentTime + fDeltaTime;

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

void PuffsPostRender::Update([[maybe_unused]] game::Frame& __restrict rFrame, [[maybe_unused]] const game::Frame& __restrict rPreviousFrame, [[maybe_unused]] float fDeltaTime)
{
}

void PuffsPostRender::PreCollision([[maybe_unused]] game::Frame& __restrict rFrame, [[maybe_unused]] const game::Frame& __restrict rPreviousFrame, [[maybe_unused]] float fDeltaTime)
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

void PuffsPostRender::PostCollision([[maybe_unused]] game::Frame& __restrict rFrame, [[maybe_unused]] const game::Frame& __restrict rPreviousFrame, [[maybe_unused]] float fDeltaTime)
{
}

void PuffsPostRender::AreaDamage([[maybe_unused]] game::Frame& __restrict rFrame, [[maybe_unused]] const game::Frame& __restrict rPreviousFrame, [[maybe_unused]] float fDeltaTime)
{
}

void PuffsPostRender::Spawn([[maybe_unused]] game::Frame& __restrict rFrame, [[maybe_unused]] float fDeltaTime)
{
}

void PuffsPostRender::Destroy(game::Frame& __restrict rFrame, float fCurrentTime)
{
	PuffsInterpolate& rInterpolate = rFrame.interpolate.puffs;
	PuffsPostRender& rPostRender = rFrame.postRender.puffs;

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
	PROFILE_SET_COUNT(kCpuCounterPuffs, rCurrent.iCount);

	if (rCurrent.iCount == 0)
	{
		WritePipelineIndirectBuffers(iCommandBuffer, 0);
		return;
	}

	ResizeBufferUpdateDescriptor(rCurrent, iCommandBuffer);

	auto pPuffsLayouts = reinterpret_cast<shaders::AxisAlignedQuadLayout*>(gpBufferManager->mDynamicStorageBuffers.at(kCrc).at(iCommandBuffer).mpMappedMemory);

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
		XMStoreFloat4A(&f4Position, vecPosition);
		if (f4Position.x < game::gpCamera->f4RenderVisibleArea.x || f4Position.x > game::gpCamera->f4RenderVisibleArea.z || f4Position.y > game::gpCamera->f4RenderVisibleArea.y || f4Position.y < game::gpCamera->f4RenderVisibleArea.w)
		{
			continue;
		}

		// Calculate terrain elevation and project to base height
		float fElevation = gpIslands->GlobalElevation(vecPosition);
		XMVECTOR vecBasePosition = common::ToBaseHeight(vecPosition, game::gpCamera->mVecEyePosition, std::max(fElevation, gBaseHeight.Get()));
		XMStoreFloat4A(&f4Position, vecBasePosition);

		// Build AxisAlignedQuadLayout
		XMFLOAT4 f4VertexRect = {f4Position.x - fArea, f4Position.y + fArea, 2.0f * fArea, -2.0f * fArea};
		XMFLOAT4 f4TextureRect = {0.0f, 0.0f, 1.0f, 1.0f};

		XMFLOAT4A f4Misc {};
		f4Misc.x = fIntensity;  // Smoke.frag uses this as intensity multiplier
		f4Misc.y = fIntensity;  // Smoke.frag uses pow(f4Misc.y, globalLayout.f4SmokeTwo.z)
		f4Misc.w = fRotation;   // Smoke.frag uses this for Rotate()

		shaders::AxisAlignedQuadLayout& rQuadLayout = pPuffsLayouts[iPuffsRendered];
		rQuadLayout.f4VertexRect = f4VertexRect;
		rQuadLayout.f4TextureRect = f4TextureRect;
		rQuadLayout.f4Misc = f4Misc;
		rQuadLayout.uiColor = rType.uiColor;

		++iPuffsRendered;
	}

	PROFILE_SET_COUNT(kCpuCounterPuffsRendered, iPuffsRendered);
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
