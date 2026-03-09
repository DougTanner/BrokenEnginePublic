#include "HexShields.h"

#if defined(BT_CLIENT)

namespace engine
{

void HexShieldsInterpolate::Update([[maybe_unused]] game::FrameInterpolate& __restrict rFrameInterpolate, [[maybe_unused]] const game::Frame& __restrict rPreviousFrame)
{
	HexShieldsInterpolate& __restrict rCurrent = rFrameInterpolate.hexShields;
	const HexShieldsInterpolate& rPrevious = rPreviousFrame.interpolate.hexShields;

	for (int64_t i = 0; i < rCurrent.iCount; ++i)
	{
		rCurrent.pf4Transforms[0][i] = rPrevious.pf4Transforms[0][i];
		rCurrent.pf4Transforms[1][i] = rPrevious.pf4Transforms[1][i];
		rCurrent.pf4Transforms[2][i] = rPrevious.pf4Transforms[2][i];
		rCurrent.pf4TransformNormals[0][i] = rPrevious.pf4TransformNormals[0][i];
		rCurrent.pf4TransformNormals[1][i] = rPrevious.pf4TransformNormals[1][i];
		rCurrent.pf4TransformNormals[2][i] = rPrevious.pf4TransformNormals[2][i];
		for (int64_t j = 0; j < shaders::kiHexShieldDirections; ++j)
		{
			rCurrent.pf4Directions[j][i] = rPrevious.pf4Directions[j][i];
			rCurrent.pfVertIntensities[j][i] = rPrevious.pfVertIntensities[j][i];
			rCurrent.pfFragIntensities[j][i] = rPrevious.pfFragIntensities[j][i];
		}
	}
}

void HexShieldsInterpolate::Sync(game::FrameInterpolate& rFrameInterpolate, id_t id, const SyncData& rData)
{
	HexShieldsInterpolate& rHexShields = rFrameInterpolate.hexShields;
	int64_t iIndex = rHexShields.IdToIndex(id);

	// Write all owner-provided fields
	rHexShields.pVecPositions[iIndex] = XMVectorSetW(rData.vecPosition, 1.0f);
	rHexShields.pf4Transforms[0][iIndex] = rData.pf4Transforms[0];
	rHexShields.pf4Transforms[1][iIndex] = rData.pf4Transforms[1];
	rHexShields.pf4Transforms[2][iIndex] = rData.pf4Transforms[2];
	rHexShields.pf4TransformNormals[0][iIndex] = rData.pf4TransformNormals[0];
	rHexShields.pf4TransformNormals[1][iIndex] = rData.pf4TransformNormals[1];
	rHexShields.pf4TransformNormals[2][iIndex] = rData.pf4TransformNormals[2];
	for (int64_t j = 0; j < shaders::kiHexShieldDirections; ++j)
	{
		rHexShields.pf4Directions[j][iIndex] = rData.pf4Directions[j];
		rHexShields.pfVertIntensities[j][iIndex] = rData.pfVertIntensities[j];
		rHexShields.pfFragIntensities[j][iIndex] = rData.pfFragIntensities[j];
	}
	rHexShields.pfLightingIntensities[iIndex] = rData.fLightingIntensity;
	rHexShields.pfSizes[iIndex] = rData.fSize;
	rHexShields.pfColorMixes[iIndex] = rData.fColorMix;
}

void HexShieldsPostRender::Update([[maybe_unused]] game::Frame& __restrict rFrame, [[maybe_unused]] const game::Frame& __restrict rPreviousFrame)
{
}

void HexShieldsPostRender::PreCollision([[maybe_unused]] game::Frame& __restrict rFrame, [[maybe_unused]] const game::Frame& __restrict rPreviousFrame)
{
}

void HexShieldsPostRender::Add(game::Frame& __restrict rFrame, hex_shields_t& rId, uint8_t uiTypeIndex)
{
	ASSERT(!rId.IsValid());

	HexShieldsInterpolate& rInterpolate = rFrame.interpolate.hexShields;
	HexShieldsPostRender& rPostRender = rFrame.postRender.hexShields;

	GrowPairedCollections(rInterpolate, rPostRender, rInterpolate.Members(), rPostRender.Members());
	auto [uiSpawnIndex, newId] = AddVisualIndexableElement(rInterpolate, rPostRender, rFrame.postRender);
	rId = newId;
	rPostRender.puiIds[uiSpawnIndex] = newId;

	// Zero-init all members
	rInterpolate.pVecPositions[uiSpawnIndex] = XMVectorSetW(XMVectorZero(), 1.0f);
	rInterpolate.pf4Transforms[0][uiSpawnIndex] = {};
	rInterpolate.pf4Transforms[1][uiSpawnIndex] = {};
	rInterpolate.pf4Transforms[2][uiSpawnIndex] = {};
	rInterpolate.pf4TransformNormals[0][uiSpawnIndex] = {};
	rInterpolate.pf4TransformNormals[1][uiSpawnIndex] = {};
	rInterpolate.pf4TransformNormals[2][uiSpawnIndex] = {};
	rInterpolate.puiTypeIndices[uiSpawnIndex] = uiTypeIndex;
	for (int64_t j = 0; j < shaders::kiHexShieldDirections; ++j)
	{
		rInterpolate.pf4Directions[j][uiSpawnIndex] = {};
		rInterpolate.pfVertIntensities[j][uiSpawnIndex] = 0.0f;
		rInterpolate.pfFragIntensities[j][uiSpawnIndex] = 0.0f;
	}
	rInterpolate.pfLightingIntensities[uiSpawnIndex] = 0.0f;
	rInterpolate.pfSizes[uiSpawnIndex] = 0.0f;
	rInterpolate.pfColorMixes[uiSpawnIndex] = 0.0f;
}

void HexShieldsPostRender::Remove(game::Frame& __restrict rFrame, hex_shields_t& rId)
{
	ASSERT(rId.IsValid());

	HexShieldsInterpolate& rInterpolate = rFrame.interpolate.hexShields;
	HexShieldsPostRender& rPostRender = rFrame.postRender.hexShields;

	RemoveIndexableElement(rInterpolate, rPostRender, rId, rInterpolate.Members(), rPostRender.Members());

	rId = {};
}

void HexShieldsPostRender::PostCollision([[maybe_unused]] game::Frame& __restrict rFrame, [[maybe_unused]] const game::Frame& __restrict rPreviousFrame)
{
}

void HexShieldsPostRender::AreaDamage([[maybe_unused]] game::Frame& __restrict rFrame, [[maybe_unused]] const game::Frame& __restrict rPreviousFrame)
{
}

} // namespace engine

#endif // BT_CLIENT
