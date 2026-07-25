#include "AreaLights.h"

#if defined(BT_CLIENT)

namespace engine
{

void AreaLightsInterpolate::Update([[maybe_unused]] game::FrameInterpolate& __restrict rFrameInterpolate, [[maybe_unused]] const game::Frame& __restrict rPreviousFrame)
{
}

void AreaLightsInterpolate::Sync(game::FrameInterpolate& rFrameInterpolate, id_t id, const SyncData& rData)
{
	AreaLightsInterpolate& rAreaLights = rFrameInterpolate.areaLights;
	int64_t iIndex = rAreaLights.IdToIndex(id);

	rAreaLights.puiTypeIndices[iIndex] = rData.uiTypeIndex;
	rAreaLights.pVecVisiblePositions[0][iIndex] = rData.vecVisiblePositions[0];
	rAreaLights.pVecVisiblePositions[1][iIndex] = rData.vecVisiblePositions[1];
	rAreaLights.pVecVisiblePositions[2][iIndex] = rData.vecVisiblePositions[2];
	rAreaLights.pVecVisiblePositions[3][iIndex] = rData.vecVisiblePositions[3];
	rAreaLights.pfIntensityMultipliers[iIndex] = rData.fIntensityMultiplier;

}

void AreaLightsPostRender::Update([[maybe_unused]] game::Frame& __restrict rFrame, [[maybe_unused]] const game::Frame& __restrict rPreviousFrame, [[maybe_unused]] const FrameStaticData& rStaticData)
{
}

void AreaLightsPostRender::Add(game::Frame& __restrict rFrame, area_lights_t& rId, uint8_t uiTypeIndex)
{
	ASSERT(!rId.IsValid());

	AreaLightsInterpolate& rInterpolate = rFrame.interpolate.areaLights;
	AreaLightsPostRender& rPostRender = rFrame.postRender.areaLights;

	GrowPairedCollections(rInterpolate, rPostRender, rInterpolate.Members(), rPostRender.Members());
	auto [uiSpawnIndex, newId] = AddVisualIndexableElement(rInterpolate, rPostRender, rFrame.postRender);
	rId = newId;
	rPostRender.puiIds[uiSpawnIndex] = newId;
	ZeroMemberRow(uiSpawnIndex, rInterpolate.Members());
	rInterpolate.puiTypeIndices[uiSpawnIndex] = uiTypeIndex;
	rInterpolate.pfIntensityMultipliers[uiSpawnIndex] = 1.0f;
}

void AreaLightsPostRender::Remove(game::Frame& __restrict rFrame, area_lights_t& rId)
{
	AreaLightsInterpolate& rInterpolate = rFrame.interpolate.areaLights;
	AreaLightsPostRender& rPostRender = rFrame.postRender.areaLights;

	RemoveIndexableElementAndClearHandle(rInterpolate, rPostRender, rId, rInterpolate.Members(), rPostRender.Members());
}

} // namespace engine

#endif // BT_CLIENT
