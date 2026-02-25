#include "WindRadials.h"

#ifdef BT_CLIENT

#include "Frame/Frame.h"
#include "Frame/Render.h"
#include "Graphics/Graphics.h"
#include "Graphics/GraphicsUtils.h"
#include "Graphics/Managers/BufferManager.h"
#include "Graphics/Managers/PipelineManager.h"
#include "Profile/ProfileManager.h"
#include "Ui/WrapperBase.h"

namespace engine
{

void WindRadialsInterpolate::Register()
{
}

void WindRadialsInterpolate::AllocateAndCopy(WindRadialsInterpolate& rCurrent, const WindRadialsInterpolate& rPrevious)
{
	Allocate(rCurrent, rPrevious, rCurrent.Members());

	if (rCurrent.iCount > 0)
	{
		std::memcpy(rCurrent.puiControllerTypeIndices, rPrevious.puiControllerTypeIndices, rCurrent.iCount * sizeof(rCurrent.puiControllerTypeIndices[0]));
		std::memcpy(rCurrent.pfStartTimes, rPrevious.pfStartTimes, rCurrent.iCount * sizeof(rCurrent.pfStartTimes[0]));
		std::memcpy(rCurrent.pfBaseIntensities, rPrevious.pfBaseIntensities, rCurrent.iCount * sizeof(rCurrent.pfBaseIntensities[0]));
		std::memcpy(rCurrent.pfBaseSizes, rPrevious.pfBaseSizes, rCurrent.iCount * sizeof(rCurrent.pfBaseSizes[0]));
	}
}

void WindRadialsInterpolate::Update([[maybe_unused]] game::FrameInterpolate& __restrict rFrameInterpolate, [[maybe_unused]] const game::Frame& __restrict rPreviousFrame)
{
	if (!gWind.Get<bool>())
	{
		return;
	}

	WindRadialsInterpolate& __restrict rCurrent = rFrameInterpolate.windRadials;
	const WindRadialsInterpolate& rPrevious = rPreviousFrame.interpolate.windRadials;
	float fCurrentTime = rPreviousFrame.interpolate.fCurrentTime + rFrameInterpolate.fDeltaTime;

	for (int64_t i = 0; i < rCurrent.iCount; ++i)
	{
		// Load from previous frame
		XMVECTOR vecPosition = rPrevious.pVecPositions[i];
		float fBaseIntensity = rCurrent.pfBaseIntensities[i];
		float fBaseSize = rCurrent.pfBaseSizes[i];

		// Interpolate controller keyframes
		uint8_t uiControllerTypeIndex = rCurrent.puiControllerTypeIndices[i];
		float fStartTime = rCurrent.pfStartTimes[i];
		float fElapsedTime = fCurrentTime - fStartTime;
		const WindRadialControllerType& rController = WindRadialsInterpolate::GetControllerType(uiControllerTypeIndex);
		WindRadialKeyframe interpolated = InterpolateWindRadialKeyframes(rController, fElapsedTime);

		// Save
		rCurrent.pVecPositions[i] = vecPosition;
		rCurrent.pfIntensities[i] = fBaseIntensity * interpolated.fIntensity;
		rCurrent.pfSizes[i] = fBaseSize * interpolated.fSize;
	}
}

void WindRadialsPostRender::AllocateAndCopy(WindRadialsPostRender& rCurrent, const WindRadialsPostRender& rPrevious)
{
	Allocate(rCurrent, rPrevious, rCurrent.Members());
}

void WindRadialsPostRender::Update([[maybe_unused]] game::Frame& __restrict rFrame, [[maybe_unused]] const game::Frame& __restrict rPreviousFrame)
{
}

void WindRadialsPostRender::PreCollision([[maybe_unused]] game::Frame& __restrict rFrame, [[maybe_unused]] const game::Frame& __restrict rPreviousFrame)
{
}

void XM_CALLCONV WindRadialsPostRender::AddControlled(game::Frame& __restrict rFrame, float fCurrentTime, uint8_t uiControllerTypeIndex, FXMVECTOR vecPosition, float fBaseIntensity, float fBaseSize)
{
	WindRadialsInterpolate& rInterpolate = rFrame.interpolate.windRadials;
	WindRadialsPostRender& rPostRender = rFrame.postRender.windRadials;

	// Get controller type
	const WindRadialControllerType& rController = WindRadialsInterpolate::GetControllerType(uiControllerTypeIndex);

	GrowPairedCollections(rInterpolate, rPostRender, rInterpolate.Members(), rPostRender.Members());
	int64_t iSpawnIndex = AddElement(rInterpolate, rPostRender);

	// Set position
	rInterpolate.pVecPositions[iSpawnIndex] = vecPosition;

	// Initialize from base values * first keyframe
	rInterpolate.pfIntensities[iSpawnIndex] = fBaseIntensity * rController.keyframes[0].fIntensity;
	rInterpolate.pfSizes[iSpawnIndex] = fBaseSize * rController.keyframes[0].fSize;

	// Set controller fields
	rInterpolate.puiControllerTypeIndices[iSpawnIndex] = uiControllerTypeIndex;
	rInterpolate.pfStartTimes[iSpawnIndex] = fCurrentTime;
	rInterpolate.pfBaseIntensities[iSpawnIndex] = fBaseIntensity;
	rInterpolate.pfBaseSizes[iSpawnIndex] = fBaseSize;
}

void WindRadialsPostRender::PostCollision([[maybe_unused]] game::Frame& __restrict rFrame, [[maybe_unused]] const game::Frame& __restrict rPreviousFrame)
{
}

void WindRadialsPostRender::AreaDamage([[maybe_unused]] game::Frame& __restrict rFrame, [[maybe_unused]] const game::Frame& __restrict rPreviousFrame)
{
}

void WindRadialsPostRender::Transfer([[maybe_unused]] game::Frame& __restrict rFrame)
{
}

void WindRadialsPostRender::Destroy(game::Frame& __restrict rFrame)
{
	WindRadialsInterpolate& rInterpolate = rFrame.interpolate.windRadials;
	WindRadialsPostRender& rPostRender = rFrame.postRender.windRadials;

	float fCurrentTime = rFrame.interpolate.fCurrentTime;

	for (int64_t i = 0; i < rInterpolate.iCount; ++i)
	{
		uint8_t uiControllerTypeIndex = rInterpolate.puiControllerTypeIndices[i];
		const WindRadialControllerType& rController = WindRadialsInterpolate::GetControllerType(uiControllerTypeIndex);

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

void WindRadialsPostRender::Spawn([[maybe_unused]] game::Frame& __restrict rFrame)
{
}

bool WindRadialsInterpolate::operator==(const WindRadialsInterpolate& rOther) const
{
	bool bEqual = true;
	bEqual &= common::BreakOnNotEqual<Collection>(*this, rOther);

	for (int64_t i = 0; i < iCount; ++i)
	{
		bEqual &= common::BreakOnNotEqual(pVecPositions[i], rOther.pVecPositions[i]);
		bEqual &= common::BreakOnNotEqual(pfIntensities[i], rOther.pfIntensities[i]);
		bEqual &= common::BreakOnNotEqual(pfSizes[i], rOther.pfSizes[i]);
		bEqual &= common::BreakOnNotEqual(puiControllerTypeIndices[i], rOther.puiControllerTypeIndices[i]);
		bEqual &= common::BreakOnNotEqual(pfStartTimes[i], rOther.pfStartTimes[i]);
		bEqual &= common::BreakOnNotEqual(pfBaseIntensities[i], rOther.pfBaseIntensities[i]);
		bEqual &= common::BreakOnNotEqual(pfBaseSizes[i], rOther.pfBaseSizes[i]);
	}

	return bEqual;
}

bool WindRadialsPostRender::operator==(const WindRadialsPostRender& rOther) const
{
	bool bEqual = true;
	bEqual &= common::BreakOnNotEqual<Collection>(*this, rOther);
	return bEqual;
}

void WindRadialsInterpolate::GraphicsResources()
{
	gpBufferManager->CreateDynamicBuffer(kCrc, kBufferMain, kName, sizeof(shaders::AxisAlignedQuadLayout));
	gpPipelineManager->CreateDynamicPipelineWindDepositAxisAlignedA(kCrc, kName, sizeof(shaders::AxisAlignedQuadLayout));
	gpPipelineManager->CreateDynamicPipelineWindDepositAxisAlignedB(kCrc, kName);
}

static int64_t siRendered = 0;

void WindRadialsInterpolate::BeginRender([[maybe_unused]] int64_t iCommandBuffer, const std::unordered_map<GridCoord, game::FrameInterpolate>& rRenderInterpolates, const std::vector<GridCoord>& rActiveCoords)
{
	siRendered = 0;

	if (!gWind.Get<bool>())
	{
		return;
	}

	int64_t iTotalCapacity = 0;
	for (const GridCoord& rCoord : rActiveCoords)
	{
		auto it = rRenderInterpolates.find(rCoord);
		if (it != rRenderInterpolates.end())
		{
			iTotalCapacity += it->second.windRadials.iCapacity;
		}
	}

	if (iTotalCapacity == 0)
	{
		return;
	}

	if (Buffer* pBuffer = gpBufferManager->ResizeDynamicBufferIfNeeded(kCrc, kBufferMain, kName, sizeof(shaders::AxisAlignedQuadLayout), iTotalCapacity, iCommandBuffer))
	{
		gpPipelineManager->mDynamicPipelineMaps[kDynamicPipelineWindDepositAxisAlignedA].at(kCrc)->UpdateStorageBufferDescriptor(iCommandBuffer, 1, pBuffer);
		gpPipelineManager->mDynamicPipelineMaps[kDynamicPipelineWindDepositAxisAlignedB].at(kCrc)->UpdateStorageBufferDescriptor(iCommandBuffer, 1, pBuffer);
	}
}

void WindRadialsInterpolate::Render([[maybe_unused]] const game::FrameInterpolate& __restrict rFrameInterpolate, [[maybe_unused]] int64_t iCommandBuffer)
{
	const WindRadialsInterpolate& rCurrent = rFrameInterpolate.windRadials;

	if (!gWind.Get<bool>() || rCurrent.iCount == 0)
	{
		return;
	}

	auto [pLayouts, iBufferCapacity] = gpBufferManager->GetDynamicStorageBuffer<shaders::AxisAlignedQuadLayout>(kCrc, kBufferMain, iCommandBuffer);

	for (int64_t i = 0; i < rCurrent.iCount; ++i)
	{
		// Load
		XMVECTOR vecPosition = rCurrent.pVecPositions[i];
		float fIntensity = rCurrent.pfIntensities[i];
		float fSize = rCurrent.pfSizes[i];

		// Visibility culling
		XMFLOAT4A f4Position {};
		if (!IsPointVisible(vecPosition, f4Position))
		{
			continue;
		}

		// Project to base height
		XMVECTOR vecBasePosition = ProjectToBaseHeight(vecPosition);
		XMStoreFloat4A(&f4Position, vecBasePosition);

		// Build AxisAlignedQuadLayout
		// Per-quad params: {intensity, 0, 0, 1} where .w = 1.0 is the radial flag for WindDeposit.frag
		XMFLOAT4A f4Params = {fIntensity, 0.0f, 0.0f, 1.0f};
		BuildAxisAlignedQuad(pLayouts[siRendered], f4Position, fSize, f4Params, 0xFFFFFFFF);

		++siRendered;
	}
}

void WindRadialsInterpolate::EndRender([[maybe_unused]] int64_t iCommandBuffer)
{
	gpPipelineManager->mDynamicPipelineMaps[kDynamicPipelineWindDepositAxisAlignedA].at(kCrc)->WriteIndirectBuffer(iCommandBuffer, giWindTextureIndex == 0 ? siRendered : 0);
	gpPipelineManager->mDynamicPipelineMaps[kDynamicPipelineWindDepositAxisAlignedB].at(kCrc)->WriteIndirectBuffer(iCommandBuffer, giWindTextureIndex == 1 ? siRendered : 0);
}

} // namespace engine

#endif // BT_CLIENT
