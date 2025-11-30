#pragma once

#include "Frame/Collections/Collection.h"
#include "Shaders/ShaderLayouts.h"

namespace engine
{

struct PointLightsInterpolate : public Collection<PointLightsInterpolate, CollectionFlags::kIdToIndex>,
                                public Renderable<PointLightsInterpolate, RenderableFlags::kAxisAlignedLighting>
{
	static constexpr char kpcName[] = "PointLights";

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

	// Interpolate
	static void Update(PointLightsInterpolate& __restrict rCurrent, const PointLightsInterpolate& __restrict rPrevious);
	static void Sync(PointLightsInterpolate& __restrict rCurrent, const game::Frame& __restrict rPreviousFrame, float fDeltaTime);

	uint8_t* __restrict puiTypeIndices = nullptr;
	XMVECTOR* __restrict pVecPositions = nullptr;
	float* __restrict pfRotations = nullptr;

	// Per-instance animatable properties (initialized from Type defaults, can be overridden by controllers)
	float* __restrict pfVisibleAreas = nullptr;
	float* __restrict pfVisibleIntensities = nullptr;
	float* __restrict pfLightingAreas = nullptr;
	float* __restrict pfLightingIntensities = nullptr;

	auto Members(this auto&& rSelf) { return std::tie(rSelf.puiTypeIndices, rSelf.pVecPositions, rSelf.pfRotations, rSelf.pfVisibleAreas, rSelf.pfVisibleIntensities, rSelf.pfLightingAreas, rSelf.pfLightingIntensities); }

	// Render
	static void Render(const game::Frame& __restrict rFrame, int64_t iCommandBuffer);

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
	static point_lights_t Add(game::Frame& __restrict rFrame, uint8_t uiTypeIndex);
	static void Remove(game::Frame& __restrict rFrame, point_lights_t id);

	point_lights_t* __restrict puiIds = nullptr;
	auto Members(this auto&& rSelf) { return std::tie(rSelf.puiIds); }

	// Utility
	bool operator==(const PointLightsPostRender& rOther) const;
};

static_assert(sizeof(shaders::AxisAlignedQuadLayout) == kAxisAlignedQuadLayoutSize);

} // namespace engine
