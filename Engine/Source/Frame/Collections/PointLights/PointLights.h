#pragma once

#if defined(BT_CLIENT)

#include "Frame/Collections/Collection.h"
#include "Frame/GridCoord.h"

namespace engine
{

class Wrapper;
struct FrameStaticData;

struct PointLightsType
{
	common::crc_t crc = 0;
	uint32_t uiColor = 0xFFFFFFFF;
	float fVisibleArea = 1.0f;
	float fVisibleIntensity = 1.0f;
	float fLightingArea = 1.0f;
	float fLightingIntensity = 1.0f;
	bool bCameraAligned = false;

	Wrapper* pVisibleIntensityWrapper = nullptr;
	Wrapper* pLightingAreaWrapper = nullptr;
	Wrapper* pLightingIntensityWrapper = nullptr;
};

struct PointLightsInterpolate : public Collection<PointLightsInterpolate, CollectionFlags::kIdToIndex>,
                                public TypeRegistry<PointLightsType>,
                                public ControllerTypeRegistry<PointLightsInterpolate>
{
	static constexpr const char* kName = "PointLights";
	static constexpr common::crc_t kCrc = common::CrcConsteval("PointLights");

	// Allocate and copy
	static void AllocateAndCopy(PointLightsInterpolate& rCurrent, const PointLightsInterpolate& rPrevious);

	// Interpolate
	static void Update(game::FrameInterpolate& __restrict rFrameInterpolate, const game::Frame& __restrict rPreviousFrame);

	// SyncData for parent-provided values
	struct SyncData
	{
		XMVECTOR vecPosition;
		float fVisibleArea;
		float fVisibleIntensity;
		float fLightingArea;
		float fLightingIntensity;
		float fRotation;
	};

	// Sync owned point light with parent-provided data
	static void Sync(game::FrameInterpolate& rFrameInterpolate, id_t id, const SyncData& rData);

	uint8_t* __restrict puiTypeIndices = nullptr;
	XMVECTOR* __restrict pVecPositions = nullptr;
	float* __restrict pfRotations = nullptr;

	// Per-instance animatable properties (initialized from Type defaults, can be overridden by controllers)
	float* __restrict pfVisibleAreas = nullptr;
	float* __restrict pfVisibleIntensities = nullptr;
	float* __restrict pfLightingAreas = nullptr;
	float* __restrict pfLightingIntensities = nullptr;

	// Controller fields (kuiInvalidControllerType = not controlled)
	uint8_t* __restrict puiControllerTypeIndices = nullptr;
	float* __restrict pfStartTimes = nullptr;
	float* __restrict pfBaseRotations = nullptr;

	auto Members(this auto&& rSelf)
	{
		return std::tie(rSelf.puiTypeIndices, rSelf.pVecPositions, rSelf.pfRotations,
		                rSelf.pfVisibleAreas, rSelf.pfVisibleIntensities, rSelf.pfLightingAreas, rSelf.pfLightingIntensities,
		                rSelf.puiControllerTypeIndices, rSelf.pfStartTimes, rSelf.pfBaseRotations);
	}
	auto PersistentMembers(this auto&& rSelf)
	{
		return std::tie(rSelf.puiTypeIndices, rSelf.puiControllerTypeIndices, rSelf.pfStartTimes, rSelf.pfBaseRotations);
	}

	// Graphics resources
	static void GraphicsResources();

	// Render
	static void BeginRender(int64_t iCommandBuffer, const std::unordered_map<GridCoord, game::FrameInterpolate>& rRenderInterpolates, const std::vector<GridCoord>& rActiveCoords);
	static void Render(const game::FrameInterpolate& __restrict rFrameInterpolate, int64_t iCommandBuffer);
	static void EndRender(int64_t iCommandBuffer);
};
using point_lights_t = PointLightsInterpolate::id_t;

struct PointLightsPostRender : public Collection<PointLightsPostRender>
{
	// Allocate and copy
	static void AllocateAndCopy(PointLightsPostRender& rCurrent, const PointLightsPostRender& rPrevious);

	// Update
	static void Update(game::Frame& __restrict rFrame, const game::Frame& __restrict rPreviousFrame, const FrameStaticData& rStaticData);

	// Add non-controlled point light
	static void Add(game::Frame& __restrict rFrame, point_lights_t& rId, uint8_t uiTypeIndex);

	// Add controlled point light with keyframe animation (fire-and-forget, auto-destroys when animation ends)
	static void XM_CALLCONV AddControlled(game::Frame& __restrict rFrame, float fCurrentTime, uint8_t uiControllerTypeIndex, FXMVECTOR vecPosition, float fRotation);

	static void Remove(game::Frame& __restrict rFrame, point_lights_t& rId);

	// Destroy handles auto-removal of expired controlled lights
	static void Destroy(game::Frame& __restrict rFrame, const FrameStaticData& rStaticData);

	point_lights_t* __restrict puiIds = nullptr;
	auto Members(this auto&& rSelf) { return std::tie(rSelf.puiIds); }

};

extern template struct Collection<PointLightsInterpolate, CollectionFlags::kIdToIndex>;
extern template struct Collection<PointLightsPostRender>;

} // namespace engine

#endif // BT_CLIENT
