#pragma once

#include "Frame/Collections/Collection.h"
#include "Shaders/ShaderLayouts.h"

namespace engine
{

struct TrailsInterpolate : public Collection<TrailsInterpolate, CollectionFlags::kIdToIndex>,
                           public Renderable<TrailsInterpolate, "Trails", {RenderableFlags::kSmoke}>
{

	// Types (configuration shared across trails)
	struct Type
	{
		common::crc_t crc = 0;
		uint32_t uiColor = 0xFFFFFFFF;
	};

	static inline std::vector<Type> sTypes;

	// Update
	static void Update(TrailsInterpolate& __restrict rCurrent, const game::Frame& __restrict rPreviousFrame, float fDeltaTime);

	// Render
	static void Render(const game::FrameInterpolate& __restrict rFrameInterpolate, int64_t iCommandBuffer);

	// Member arrays (SOA)
	uint8_t* __restrict puiTypeIndices = nullptr;
	XMVECTOR* __restrict pVecPositions = nullptr;
	float* __restrict pfIntensities = nullptr;
	float* __restrict pfWidths = nullptr;
	float* __restrict pfStartTimes = nullptr;

	// Smoothing state (for trail rendering)
	XMVECTOR* __restrict pVecPreviousPositions = nullptr;
	XMVECTOR* __restrict pVecSmoothedPositions = nullptr;

	auto Members(this auto&& rSelf)
	{
		return std::tie(rSelf.puiTypeIndices, rSelf.pVecPositions,
		                rSelf.pfIntensities, rSelf.pfWidths, rSelf.pfStartTimes,
		                rSelf.pVecPreviousPositions, rSelf.pVecSmoothedPositions);
	}

	// Utility
	bool operator==(const TrailsInterpolate& rOther) const;
};
using trails_t = TrailsInterpolate::id_t;

struct TrailsPostRender : public Collection<TrailsPostRender>
{
	// Update
	static void Update(TrailsPostRender& __restrict rCurrent, const TrailsPostRender& __restrict rPrevious);
	static uint8_t RegisterType(const TrailsInterpolate::Type& rType);
	static const TrailsInterpolate::Type& GetType(uint8_t uiIndex);

	// Add trail (returns ID for external tracking)
	static trails_t XM_CALLCONV Add(game::Frame& __restrict rFrame, float fCurrentTime, uint8_t uiTypeIndex, FXMVECTOR vecPosition, float fIntensity, float fWidth);

	// Remove trail by ID
	static void Remove(game::Frame& __restrict rFrame, trails_t id);

	trails_t* __restrict puiIds = nullptr;
	auto Members(this auto&& rSelf) { return std::tie(rSelf.puiIds); }

	// Utility
	bool operator==(const TrailsPostRender& rOther) const;
};

static_assert(sizeof(shaders::QuadLayout) == kQuadLayoutSize);

} // namespace engine
