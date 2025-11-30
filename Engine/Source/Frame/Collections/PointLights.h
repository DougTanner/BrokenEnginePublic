#pragma once

#include "Frame/Collections/Collection.h"
#include "Shaders/ShaderLayouts.h"

namespace engine
{

struct PointLightsInterpolate : public Collection<PointLightsInterpolate, CollectionFlags::kIdToIndex>,
                                public Renderable<PointLightsInterpolate, "PointLights", {RenderableFlags::kAxisAlignedLighting, RenderableFlags::kVisibleLights}>,
                                public ControllerTypeRegistry<PointLightsInterpolate>
{

	// Types
	struct Type
	{
		common::crc_t crc = 0;
		uint32_t uiColor = 0xFFFFFFFF;
		float fVisibleArea = 1.0f;
		float fVisibleIntensity = 1.0f;
		float fLightingArea = 1.0f;
		float fLightingIntensity = 1.0f;
	};

	static inline std::vector<Type> sTypes;

	// Interpolate - takes fCurrentTime for controller animation
	static void Update(PointLightsInterpolate& __restrict rCurrent, const PointLightsInterpolate& __restrict rPrevious, float fCurrentTime);
	static void Sync(PointLightsInterpolate& __restrict rCurrent, const game::Frame& __restrict rPreviousFrame, float fDeltaTime);

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

	// Render
	static void Render(const game::FrameInterpolate& __restrict rFrameInterpolate, int64_t iCommandBuffer);

	// Utility
	bool operator==(const PointLightsInterpolate& rOther) const;
};
using point_lights_t = PointLightsInterpolate::id_t;

struct PointLightsPostRender : public Collection<PointLightsPostRender>
{
	// Update
	static void Update(PointLightsPostRender& __restrict rCurrent, const PointLightsPostRender& __restrict rPrevious);
	static uint8_t RegisterType(const PointLightsInterpolate::Type& rType);
	static const PointLightsInterpolate::Type& GetType(uint8_t uiIndex);

	// Add non-controlled point light
	static point_lights_t Add(game::Frame& __restrict rFrame, uint8_t uiTypeIndex);

	// Add controlled point light with keyframe animation
	static point_lights_t XM_CALLCONV AddControlled(game::Frame& __restrict rFrame, float fCurrentTime, uint8_t uiControllerTypeIndex, FXMVECTOR vecPosition, float fRotation);

	static void Remove(game::Frame& __restrict rFrame, point_lights_t id);

	// Destroy handles auto-removal of expired controlled lights
	static void Destroy(game::Frame& __restrict rFrame, float fCurrentTime);

	point_lights_t* __restrict puiIds = nullptr;
	auto Members(this auto&& rSelf) { return std::tie(rSelf.puiIds); }

	// Utility
	bool operator==(const PointLightsPostRender& rOther) const;
};

static_assert(sizeof(shaders::AxisAlignedQuadLayout) == kAxisAlignedQuadLayoutSize);

} // namespace engine
