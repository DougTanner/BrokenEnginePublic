#include "Puffs.h"

#include "Frame/Frame.h"
#include "Graphics/Graphics.h"
#include "Profile/ProfileManager.h"

namespace engine
{

void PuffsInterpolate::Update([[maybe_unused]] PuffsInterpolate& __restrict rCurrent, [[maybe_unused]] const PuffsInterpolate& __restrict rPrevious, [[maybe_unused]] float fCurrentTime)
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
		float fIntensity = rPrevious.pfIntensities[i];
		float fArea = rPrevious.pfAreas[i];
		float fRotation = rPrevious.pfRotations[i];

		// Load controller fields
		uint8_t uiControllerTypeIndex = rPrevious.puiControllerTypeIndices[i];
		float fStartTime = rPrevious.pfStartTimes[i];

		// Apply controller interpolation if this is a controlled puff
		if (uiControllerTypeIndex != kuiInvalidControllerType)
		{
			float fElapsedTime = fCurrentTime - fStartTime;
			const PuffControllerType& rController = sControllerTypes.at(uiControllerTypeIndex);
			PuffKeyframe interpolated = InterpolatePuffKeyframes(rController, fElapsedTime);

			// Map PuffKeyframe fields to puff properties
			fArea = interpolated.fArea;
			fIntensity = interpolated.fIntensity;
			fRotation = interpolated.fRotation;
		}

		// Save
		rCurrent.puiTypeIndices[i] = uiTypeIndex;
		rCurrent.pVecPositions[i] = vecPosition;
		rCurrent.pfIntensities[i] = fIntensity;
		rCurrent.pfAreas[i] = fArea;
		rCurrent.pfRotations[i] = fRotation;

		// Save controller fields
		rCurrent.puiControllerTypeIndices[i] = uiControllerTypeIndex;
		rCurrent.pfStartTimes[i] = fStartTime;
	}
}

void PuffsInterpolate::Sync([[maybe_unused]] PuffsInterpolate& __restrict rCurrent, [[maybe_unused]] const game::Frame& __restrict rPreviousFrame, [[maybe_unused]] float fDeltaTime)
{
}

void PuffsPostRender::Update([[maybe_unused]] PuffsPostRender& __restrict rCurrent, [[maybe_unused]] const PuffsPostRender& __restrict rPrevious)
{
	engine::ReallocateAndCopyMetadata(rCurrent, rPrevious, rCurrent.Members());
}

uint8_t PuffsPostRender::RegisterType(const PuffsInterpolate::Type& rType)
{
	PuffsInterpolate::sTypes.push_back(rType);
	return static_cast<uint8_t>(PuffsInterpolate::sTypes.size() - 1);
}

const PuffsInterpolate::Type& PuffsPostRender::GetType(uint8_t uiIndex)
{
	return PuffsInterpolate::sTypes.at(uiIndex);
}

uint8_t PuffsInterpolate::RegisterControllerType(const PuffControllerType& rType)
{
	sControllerTypes.push_back(rType);
	return static_cast<uint8_t>(sControllerTypes.size() - 1);
}

const PuffsInterpolate::PuffControllerType& PuffsInterpolate::GetControllerType(uint8_t uiIndex)
{
	return sControllerTypes.at(uiIndex);
}

PuffsInterpolate::PuffKeyframe PuffsInterpolate::InterpolatePuffKeyframes(const PuffControllerType& rController, float fElapsedTime)
{
	int64_t iKeyframeCount = rController.uiKeyframeCount;

	if (fElapsedTime <= rController.pfTimes[0])
	{
		return rController.keyframes[0];
	}
	if (fElapsedTime >= rController.pfTimes[iKeyframeCount - 1])
	{
		return rController.keyframes[iKeyframeCount - 1];
	}

	for (int64_t j = 1; j < iKeyframeCount; ++j)
	{
		if (fElapsedTime < rController.pfTimes[j])
		{
			float fPreviousTime = rController.pfTimes[j - 1];
			float fPercent = (fElapsedTime - fPreviousTime) / (rController.pfTimes[j] - fPreviousTime);
			return PuffKeyframe::Lerp(rController.keyframes[j - 1], rController.keyframes[j], fPercent);
		}
	}

	return rController.keyframes[iKeyframeCount - 1];
}

void XM_CALLCONV PuffsPostRender::AddControlled(game::Frame& __restrict rFrame, float fCurrentTime, uint8_t uiControllerTypeIndex, FXMVECTOR vecPosition)
{
	PuffsInterpolate& rInterpolate = rFrame.interpolate.puffs;
	PuffsPostRender& rPostRender = rFrame.postRender.puffs;

	// Get controller type
	const PuffsInterpolate::PuffControllerType& rController = PuffsInterpolate::sControllerTypes.at(uiControllerTypeIndex);

	engine::GrowPairedCollections(rInterpolate, rPostRender, rInterpolate.Members(), rPostRender.Members());
	int64_t iSpawnIndex = engine::AddElement(rInterpolate, rPostRender);

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

		const PuffsInterpolate::PuffControllerType& rController = PuffsInterpolate::sControllerTypes.at(uiControllerTypeIndex);

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
			// Remove the puff using swap-and-pop
			if (rInterpolate.iCount - 1 > i)
			{
				engine::SwapElement(rInterpolate, i, rInterpolate.Members());
				engine::SwapElement(rPostRender, i, rPostRender.Members());
			}
			--rInterpolate.iCount;
			--rPostRender.iCount;
			--i; // Re-check this index (new element swapped in)
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
		const PuffsInterpolate::Type& rType = PuffsInterpolate::sTypes.at(rCurrent.puiTypeIndices[i]);
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
