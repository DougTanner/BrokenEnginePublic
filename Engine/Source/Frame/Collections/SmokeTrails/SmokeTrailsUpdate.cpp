#include "SmokeTrails.h"

#if defined(BT_CLIENT)

namespace engine
{

static constexpr float kfSmoothingRate = 20.0f;

void SmokeTrailsInterpolate::Update(game::FrameInterpolate& __restrict rFrameInterpolate, [[maybe_unused]] const game::Frame& __restrict rPreviousFrame)
{
	SmokeTrailsInterpolate& rCurrent = rFrameInterpolate.smokeTrails;
	float fSmoothingInterpolant = common::ExponentialInterpolant(kfSmoothingRate, rFrameInterpolate.fDeltaTime);

	for (int64_t i = 0; i < rCurrent.iCount; ++i)
	{
		if (XMVectorGetW(rCurrent.pVecSmoothedPositions[i]) == 0.0f)
		{
			rCurrent.pVecSmoothedPositions[i] = rCurrent.pVecPositions[i];
		}
		else
		{
			rCurrent.pVecSmoothedPositions[i] = XMVectorLerp(rCurrent.pVecSmoothedPositions[i], rCurrent.pVecPositions[i], fSmoothingInterpolant);
		}
	}
}

void SmokeTrailsInterpolate::Sync(game::FrameInterpolate& rFrameInterpolate, id_t id, const SyncData& rData)
{
	SmokeTrailsInterpolate& rSmokeTrails = rFrameInterpolate.smokeTrails;
	int64_t iIndex = rSmokeTrails.IdToIndex(id);

	rSmokeTrails.pVecPositions[iIndex] = XMVectorSetW(rData.vecPosition, 1.0f);
	rSmokeTrails.pfIntensities[iIndex] = rData.fIntensity;
}

void SmokeTrailsPostRender::Update([[maybe_unused]] game::Frame& __restrict rFrame, [[maybe_unused]] const game::Frame& __restrict rPreviousFrame, [[maybe_unused]] const FrameStaticData& rStaticData)
{
}

void SmokeTrailsPostRender::Add(game::Frame& __restrict rFrame, smoke_trails_t& rId, uint8_t uiTypeIndex, smoke_trails_t reuseId)
{
	ASSERT(!rId.IsValid());

	SmokeTrailsInterpolate& rInterpolate = rFrame.interpolate.smokeTrails;
	SmokeTrailsPostRender& rPostRender = rFrame.postRender.smokeTrails;

	GrowPairedCollections(rInterpolate, rPostRender, rInterpolate.Members(), rPostRender.Members());

	smoke_trails_t id;
	int64_t iSpawnIndex = 0;
	if (reuseId.IsValid())
	{
		auto [index, reusedId] = AddIndexableElementWithId(rInterpolate, rPostRender, reuseId);
		iSpawnIndex = index;
		id = reusedId;
	}
	else
	{
		auto [index, newId] = AddVisualIndexableElement(rInterpolate, rPostRender, rFrame.postRender);
		iSpawnIndex = index;
		id = newId;
	}

	rId = id;
	rPostRender.puiIds[iSpawnIndex] = id;
	ZeroMemberRow(iSpawnIndex, rInterpolate.Members());
	rInterpolate.puiTypeIndices[iSpawnIndex] = uiTypeIndex;
	if (!reuseId.IsValid())
	{
		rInterpolate.pfStartTimes[iSpawnIndex] = rFrame.interpolate.fCurrentTime;
	}
}

void SmokeTrailsPostRender::Remove(game::Frame& __restrict rFrame, smoke_trails_t& rId)
{
	SmokeTrailsInterpolate& rInterpolate = rFrame.interpolate.smokeTrails;
	SmokeTrailsPostRender& rPostRender = rFrame.postRender.smokeTrails;

	RemoveIndexableElementAndClearHandle(rInterpolate, rPostRender, rId, rInterpolate.Members(), rPostRender.Members());
}

} // namespace engine

#endif // BT_CLIENT
