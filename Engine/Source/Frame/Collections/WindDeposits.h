#pragma once

#include "Frame/Collections/Collection.h"

namespace engine
{

struct WindDepositsInterpolate : public Collection<WindDepositsInterpolate, CollectionFlags::kIdToIndex>,
                                 public Renderable<WindDepositsInterpolate, "WindDeposits", {RenderableFlags::kWindDeposit}>
{
	// Register
	static void Register();

	// Graphics resources
	static void GraphicsResources();

	// Allocate and copy
	static void AllocateAndCopy(WindDepositsInterpolate& rCurrent, const WindDepositsInterpolate& rPrevious);

	// SyncData for parent-provided values
	struct SyncData
	{
		XMVECTOR vecPosition;
		float fIntensity;
	};

	// Sync owned wind deposit with parent-provided data (bFirstSync=true initializes previous position)
	static void Sync(game::FrameInterpolate& rFrameInterpolate, id_t id, const SyncData& rData, bool bFirstSync);

	// Update
	static void Update(game::FrameInterpolate& __restrict rFrameInterpolate, const game::Frame& __restrict rPreviousFrame);

	// Render
	static void Render(const game::FrameInterpolate& __restrict rFrameInterpolate, int64_t iCommandBuffer);

	// Member arrays (SOA)
	XMVECTOR* __restrict pVecPositions = nullptr;
	float* __restrict pfIntensities = nullptr;
	XMVECTOR* __restrict pVecPreviousPositions = nullptr;

	auto Members(this auto&& rSelf)
	{
		return std::tie(rSelf.pVecPositions, rSelf.pfIntensities, rSelf.pVecPreviousPositions);
	}

	// Utility
	bool operator==(const WindDepositsInterpolate& rOther) const;
};
using wind_deposit_t = WindDepositsInterpolate::id_t;

struct WindDepositsPostRender : public Collection<WindDepositsPostRender>
{
	// Allocate and copy
	static void AllocateAndCopy(WindDepositsPostRender& rCurrent, const WindDepositsPostRender& rPrevious);

	// Update
	static void Update(game::Frame& __restrict rFrame, const game::Frame& __restrict rPreviousFrame);
	static void PreCollision(game::Frame& __restrict rFrame, const game::Frame& __restrict rPreviousFrame);
	static void PostCollision(game::Frame& __restrict rFrame, const game::Frame& __restrict rPreviousFrame);
	static void AreaDamage(game::Frame& __restrict rFrame, const game::Frame& __restrict rPreviousFrame);
	static void Destroy(game::Frame& __restrict rFrame);
	static void Spawn(game::Frame& __restrict rFrame);

	// Add wind deposit
	static void Add(game::Frame& __restrict rFrame, wind_deposit_t& rId);

	// Remove wind deposit by ID
	static void Remove(game::Frame& __restrict rFrame, wind_deposit_t& rId);

	wind_deposit_t* __restrict puiIds = nullptr;
	auto Members(this auto&& rSelf) { return std::tie(rSelf.puiIds); }

	// Utility
	bool operator==(const WindDepositsPostRender& rOther) const;
};

static_assert(sizeof(shaders::QuadLayout) == kQuadLayoutSize);

} // namespace engine
