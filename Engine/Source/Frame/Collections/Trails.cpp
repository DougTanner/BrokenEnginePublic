#include "Trails.h"

#include "Frame/Frame.h"
#include "Graphics/Graphics.h"
#include "Profile/ProfileManager.h"

namespace engine
{

void TrailsInterpolate::Sync(game::FrameInterpolate& rFrameInterpolate, id_t id, const SyncData& rData)
{
	TrailsInterpolate& rTrails = rFrameInterpolate.trails;
	int64_t iIndex = rTrails.IdToIndex(id);

	rTrails.pVecPositions[iIndex] = rData.vecPosition;
	rTrails.pfIntensities[iIndex] = rData.fIntensity;
}

void TrailsInterpolate::Update([[maybe_unused]] TrailsInterpolate& __restrict rCurrent, [[maybe_unused]] const game::Frame& __restrict rPreviousFrame, [[maybe_unused]] float fDeltaTime)
{
	const TrailsInterpolate& rPrevious = rPreviousFrame.interpolate.trails;

	if (rCurrent.iCount == 0)
	{
		return;
	}

	// Note: Owner is responsible for writing position, intensity via Sync() each frame
	std::memcpy(rCurrent.puiTypeIndices, rPrevious.puiTypeIndices, static_cast<size_t>(rCurrent.iCount) * sizeof(uint8_t));

	static constexpr float kfSmoothingFactor = 0.15f;

	for (int64_t i = 0; i < rCurrent.iCount; ++i)
	{
		// Load
		XMVECTOR vecPosition = rPrevious.pVecPositions[i];
		float fIntensity = rPrevious.pfIntensities[i];
		float fWidth = rPrevious.pfWidths[i];
		float fStartTime = rPrevious.pfStartTimes[i];
		XMVECTOR vecSmoothedPosition = rPrevious.pVecSmoothedPositions[i];

		// Update: previous position tracks behind current position (one frame delay)
		XMVECTOR vecPreviousPosition = rPrevious.pVecPositions[i];

		// Update: smoothed position gradually approaches current for stable direction
		vecSmoothedPosition = XMVectorLerp(vecSmoothedPosition, vecPosition, kfSmoothingFactor);

		// Save
		rCurrent.pVecPositions[i] = vecPosition;
		rCurrent.pfIntensities[i] = fIntensity;
		rCurrent.pfWidths[i] = fWidth;
		rCurrent.pfStartTimes[i] = fStartTime;
		rCurrent.pVecPreviousPositions[i] = vecPreviousPosition;
		rCurrent.pVecSmoothedPositions[i] = vecSmoothedPosition;
	}
}

void TrailsPostRender::Update([[maybe_unused]] TrailsPostRender& __restrict rCurrent, [[maybe_unused]] const TrailsPostRender& __restrict rPrevious)
{
	for (int64_t i = 0; i < rCurrent.iCount; ++i)
	{
		// Load
		trails_t id = rPrevious.puiIds[i];

		// Save
		rCurrent.puiIds[i] = id;
	}
}

uint8_t TrailsPostRender::RegisterType(const TrailsInterpolate::Type& rType)
{
	TrailsInterpolate::sTypes.push_back(rType);
	return static_cast<uint8_t>(TrailsInterpolate::sTypes.size() - 1);
}

const TrailsInterpolate::Type& TrailsPostRender::GetType(uint8_t uiIndex)
{
	return TrailsInterpolate::sTypes.at(uiIndex);
}

void TrailsPostRender::Add(game::Frame& __restrict rFrame, trails_t& rId, uint8_t uiTypeIndex)
{
	TrailsInterpolate& rInterpolate = rFrame.interpolate.trails;
	TrailsPostRender& rPostRender = rFrame.postRender.trails;

	engine::GrowPairedCollections(rInterpolate, rPostRender, rInterpolate.Members(), rPostRender.Members());
	auto [uiSpawnIndex, newId] = engine::AddIndexableElement(rInterpolate, rPostRender, rFrame.postRender);
	rId = newId;
	rPostRender.puiIds[uiSpawnIndex] = newId;

	// Zero-init all members
	rInterpolate.puiTypeIndices[uiSpawnIndex] = uiTypeIndex;
	rInterpolate.pVecPositions[uiSpawnIndex] = XMVectorZero();
	rInterpolate.pfIntensities[uiSpawnIndex] = 0.0f;
	rInterpolate.pfWidths[uiSpawnIndex] = 0.0f;
	rInterpolate.pfStartTimes[uiSpawnIndex] = 0.0f;
	rInterpolate.pVecPreviousPositions[uiSpawnIndex] = XMVectorZero();
	rInterpolate.pVecSmoothedPositions[uiSpawnIndex] = XMVectorZero();
}

void TrailsPostRender::Remove(game::Frame& __restrict rFrame, trails_t& rId)
{
	TrailsInterpolate& rInterpolate = rFrame.interpolate.trails;
	TrailsPostRender& rPostRender = rFrame.postRender.trails;

	engine::RemoveIndexableElement(rInterpolate, rPostRender, rId, rInterpolate.Members(), rPostRender.Members());

	rId = {};
}

void TrailsInterpolate::Render([[maybe_unused]] const game::FrameInterpolate& __restrict rFrameInterpolate, [[maybe_unused]] int64_t iCommandBuffer)
{
	const TrailsInterpolate& rCurrent = rFrameInterpolate.trails;
	PROFILE_SET_COUNT(kCpuCounterTrails, rCurrent.iCount);

	if (rCurrent.iCount == 0)
	{
		WritePipelineIndirectBuffers(iCommandBuffer, 0);
		return;
	}

	ResizeBufferUpdateDescriptor(rCurrent, iCommandBuffer);

	auto pTrailLayouts = reinterpret_cast<shaders::QuadLayout*>(gpBufferManager->mDynamicStorageBuffers.at(kCrc).at(iCommandBuffer).mpMappedMemory);

	static common::RandomEngine sRandomEngine;
	int64_t iTrailsRendered = 0;

	for (int64_t i = 0; i < rCurrent.iCount; ++i)
	{
		// Load
		XMVECTOR vecPosition = rCurrent.pVecPositions[i];
		const TrailsInterpolate::Type& rType = TrailsInterpolate::sTypes.at(rCurrent.puiTypeIndices[i]);
		float fIntensity = rCurrent.pfIntensities[i];
		float fWidth = rCurrent.pfWidths[i];
		float fStartTime = rCurrent.pfStartTimes[i];
		XMVECTOR vecPreviousPosition = rCurrent.pVecPreviousPositions[i];
		XMVECTOR vecSmoothedPosition = rCurrent.pVecSmoothedPositions[i];

		// Visibility culling
		XMFLOAT4A f4Position {};
		XMStoreFloat4A(&f4Position, vecPosition);
		if (f4Position.x < game::gpCamera->f4RenderVisibleArea.x || f4Position.x > game::gpCamera->f4RenderVisibleArea.z ||
		    f4Position.y > game::gpCamera->f4RenderVisibleArea.y || f4Position.y < game::gpCamera->f4RenderVisibleArea.w)
		{
			continue;
		}

		// Calculate jitter for visual variation
		float fJitterOne = gSmokeTrailsSideJitter.Get() * common::Random(sRandomEngine);
		fJitterOne = fJitterOne * fJitterOne;
		float fJitterTwo = gSmokeTrailsSideJitter.Get() * common::Random(sRandomEngine);
		fJitterTwo = fJitterTwo * fJitterTwo;

		// Project current position to base height
		float fElevation = gpIslands->GlobalElevation(vecPosition);
		XMVECTOR vecBasePosition = common::ToBaseHeight(vecPosition, game::gpCamera->mVecEyePosition, std::max(fElevation, gBaseHeight.Get()));

		// Project previous position to base height
		fElevation = gpIslands->GlobalElevation(vecPreviousPosition);
		XMVECTOR vecBasePreviousPosition = common::ToBaseHeight(vecPreviousPosition, game::gpCamera->mVecEyePosition, std::max(fElevation, gBaseHeight.Get()));

		// Calculate direction from previous to current
		XMVECTOR vecToPrevious = vecBasePosition - vecBasePreviousPosition;
		float fLengthScale = XMVectorGetX(XMVector3Length(vecToPrevious));
		if (fLengthScale <= 0.01f)
		{
			continue;
		}

		// Calculate smoothed direction
		XMVECTOR vecToSmoothed = vecBasePosition - vecSmoothedPosition;
		if (XMVectorGetX(XMVector3Length(vecToSmoothed)) <= 0.01f)
		{
			vecToSmoothed = vecToPrevious;
		}
		XMVECTOR vecToSmoothedNormal = XMVector3Normalize(vecToSmoothed);

		// Calculate perpendicular (left) direction for width
		XMVECTOR vecLeftNormal = XMVector3Normalize(XMVector3Cross(vecToSmoothedNormal, XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f)));

		// Calculate quad corners
		XMVECTOR vecPointOne = vecBasePosition + gSmokeTrailsWidthCurrent.Get() * fWidth * vecLeftNormal;
		XMVECTOR vecPointTwo = vecBasePosition + gSmokeTrailsWidthCurrent.Get() * fWidth * -vecLeftNormal;

		float fLength = gSmokeTrailsLength.Get() + gSmokeTrailsLengthJitter.Get() * common::Random(sRandomEngine);
		if (rFrameInterpolate.fCurrentTime - fStartTime < 0.05f)
		{
			fLength = 0.0f;
		}

		XMVECTOR vecPointThree = vecBasePreviousPosition + gSmokeTrailsWidthPrevious.Get() * fJitterOne * vecLeftNormal - fLength * fLengthScale * vecToSmoothedNormal;
		XMVECTOR vecPointFour = vecBasePreviousPosition + gSmokeTrailsWidthPrevious.Get() * fJitterTwo * -vecLeftNormal - fLength * fLengthScale * vecToSmoothedNormal;

		// Build QuadLayout (4 vertices with texcoords)
		XMStoreFloat4A(&f4Position, vecPointOne);
		pTrailLayouts[iTrailsRendered].pf4VerticesTexcoords[0] = {f4Position.x, f4Position.y, 0.0f, 0.0f};
		XMStoreFloat4A(&f4Position, vecPointTwo);
		pTrailLayouts[iTrailsRendered].pf4VerticesTexcoords[1] = {f4Position.x, f4Position.y, 1.0f, 0.0f};
		XMStoreFloat4A(&f4Position, vecPointThree);
		pTrailLayouts[iTrailsRendered].pf4VerticesTexcoords[2] = {f4Position.x, f4Position.y, 0.0f, 1.0f};
		XMStoreFloat4A(&f4Position, vecPointFour);
		pTrailLayouts[iTrailsRendered].pf4VerticesTexcoords[3] = {f4Position.x, f4Position.y, 1.0f, 1.0f};

		float fQuantity = fIntensity * gSmokeTrailsQuantity.Get() / fLengthScale;
		pTrailLayouts[iTrailsRendered].pf4Misc[0] = {fQuantity, 1.0f, 0.0f, 0.0f};
		pTrailLayouts[iTrailsRendered].pf4Misc[1] = {fQuantity, 1.0f, 0.0f, 0.0f};
		pTrailLayouts[iTrailsRendered].pf4Misc[2] = {fQuantity, 0.0f, 0.0f, 0.0f};
		pTrailLayouts[iTrailsRendered].pf4Misc[3] = {fQuantity, 0.0f, 0.0f, 0.0f};

		pTrailLayouts[iTrailsRendered].f4Misc = {};
		pTrailLayouts[iTrailsRendered].uiColor = rType.uiColor;

		++iTrailsRendered;
	}

	PROFILE_SET_COUNT(kCpuCounterTrailsRendered, iTrailsRendered);
	WritePipelineIndirectBuffers(iCommandBuffer, iTrailsRendered);
}

bool TrailsInterpolate::operator==(const TrailsInterpolate& rOther) const
{
	bool bEqual = true;
	bEqual &= common::BreakOnNotEqual<Collection>(*this, rOther);

	for (int64_t i = 0; i < iCount; ++i)
	{
		bEqual &= common::BreakOnNotEqual(puiTypeIndices[i], rOther.puiTypeIndices[i]);
		bEqual &= common::BreakOnNotEqual(pVecPositions[i], rOther.pVecPositions[i]);
		bEqual &= common::BreakOnNotEqual(pfIntensities[i], rOther.pfIntensities[i]);
		bEqual &= common::BreakOnNotEqual(pfWidths[i], rOther.pfWidths[i]);
		bEqual &= common::BreakOnNotEqual(pfStartTimes[i], rOther.pfStartTimes[i]);
		bEqual &= common::BreakOnNotEqual(pVecPreviousPositions[i], rOther.pVecPreviousPositions[i]);
		bEqual &= common::BreakOnNotEqual(pVecSmoothedPositions[i], rOther.pVecSmoothedPositions[i]);
	}

	return bEqual;
}

bool TrailsPostRender::operator==(const TrailsPostRender& rOther) const
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
