#pragma once

#include "Frame/Collections/Collection.h"

namespace engine
{

struct SmokeTrailsType
{
	common::crc_t crc = 0;
	uint32_t uiColor = 0xFFFFFFFF;
	float fWidth = 1.0f;
};

struct SmokeTrailsInterpolate : public Collection<SmokeTrailsInterpolate, CollectionFlags::kIdToIndex>,
                                public TypeRegistry<SmokeTrailsType>,
                                public Renderable<SmokeTrailsInterpolate, "SmokeTrails", {RenderableFlags::kSmoke}>
{
	// Register
	static void Register();

	// Graphics resources
	static void GraphicsResources();

	// Reset render state (clears cached positions for world reset)
	static void ResetRenderState();

	// Allocate and copy
	static void AllocateAndCopy(SmokeTrailsInterpolate& rCurrent, const SmokeTrailsInterpolate& rPrevious);

	// SyncData for parent-provided values
	struct SyncData
	{
		XMVECTOR vecPosition;
		float fIntensity;
	};

	// Sync owned trail with parent-provided data
	static void Sync(game::FrameInterpolate& rFrameInterpolate, id_t id, const SyncData& rData);

	// Update
	static void Update(game::FrameInterpolate& __restrict rFrameInterpolate, const game::Frame& __restrict rPreviousFrame);

	// Render
	static void Render(const game::FrameInterpolate& __restrict rFrameInterpolate, int64_t iCommandBuffer);

	// Member arrays (SOA)
	uint8_t* __restrict puiTypeIndices = nullptr;
	XMVECTOR* __restrict pVecPositions = nullptr;
	float* __restrict pfIntensities = nullptr;
	float* __restrict pfStartTimes = nullptr;

	auto Members(this auto&& rSelf)
	{
		return std::tie(rSelf.puiTypeIndices, rSelf.pVecPositions, rSelf.pfIntensities, rSelf.pfStartTimes);
	}

	// Utility
	bool operator==(const SmokeTrailsInterpolate& rOther) const;
};
using smoke_trails_t = SmokeTrailsInterpolate::id_t;

struct SmokeTrailsPostRender : public Collection<SmokeTrailsPostRender>
{
	// Allocate and copy
	static void AllocateAndCopy(SmokeTrailsPostRender& rCurrent, const SmokeTrailsPostRender& rPrevious);

	// Update
	static void Update(game::Frame& __restrict rFrame, const game::Frame& __restrict rPreviousFrame);
	static void PreCollision(game::Frame& __restrict rFrame, const game::Frame& __restrict rPreviousFrame);

	// Add trail
	static void Add(game::Frame& __restrict rFrame, smoke_trails_t& rId, uint8_t uiTypeIndex);

	// Remove trail by ID
	static void Remove(game::Frame& __restrict rFrame, smoke_trails_t& rId);
	static void PostCollision(game::Frame& __restrict rFrame, const game::Frame& __restrict rPreviousFrame);
	static void AreaDamage(game::Frame& __restrict rFrame, const game::Frame& __restrict rPreviousFrame);
	static void Destroy(game::Frame& __restrict rFrame);
	static void Spawn(game::Frame& __restrict rFrame);

	smoke_trails_t* __restrict puiIds = nullptr;
	auto Members(this auto&& rSelf) { return std::tie(rSelf.puiIds); }

	// Utility
	bool operator==(const SmokeTrailsPostRender& rOther) const;
};

static_assert(sizeof(shaders::QuadLayout) == kQuadLayoutSize);

} // namespace engine
