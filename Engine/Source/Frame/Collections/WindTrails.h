#pragma once

#include "Frame/Collections/Collection.h"
#include "Frame/GridCoord.h"

namespace engine
{

struct WindTrailsInterpolate : public Collection<WindTrailsInterpolate, CollectionFlags::kIdToIndex>
{
	static constexpr const char* kName = "WindTrails";
	static constexpr common::crc_t kCrc = common::CrcConsteval("WindTrails");

	// Register
	static void Register();

	// Allocate and copy
	static void AllocateAndCopy(WindTrailsInterpolate& rCurrent, const WindTrailsInterpolate& rPrevious);

	// SyncData for parent-provided values
	struct SyncData
	{
		XMVECTOR vecPosition;
		float fIntensity;
		float fWidth;
		float fLengthMultiplier = 1.0f;
	};

	// Sync owned wind trail with parent-provided data
	static void Sync(game::FrameInterpolate& rFrameInterpolate, id_t id, const SyncData& rData);

	// Update
	static void Update(game::FrameInterpolate& __restrict rFrameInterpolate, const game::Frame& __restrict rPreviousFrame);

	// Member arrays (SOA)
	XMVECTOR* __restrict pVecPositions = nullptr;
	float* __restrict pfIntensities = nullptr;
	float* __restrict pfWidths = nullptr;
	float* __restrict pfLengthMultipliers = nullptr;

	auto Members(this auto&& rSelf)
	{
		return std::tie(rSelf.pVecPositions, rSelf.pfIntensities, rSelf.pfWidths, rSelf.pfLengthMultipliers);
	}

	// Utility
	bool operator==(const WindTrailsInterpolate& rOther) const;

	// Graphics resources
	static void GraphicsResources();

	// Reset render state (clears cached positions for world reset)
	static void ResetRenderState();

	// Render
	static void BeginRender(int64_t iCommandBuffer, const std::unordered_map<GridCoord, game::FrameInterpolate>& rRenderInterpolates, const std::vector<GridCoord>& rActiveCoords);
	static void Render(const game::FrameInterpolate& __restrict rFrameInterpolate, int64_t iCommandBuffer, uint16_t uiFrameId);
	static void EndRender(int64_t iCommandBuffer);
};
using wind_trail_t = WindTrailsInterpolate::id_t;

struct WindTrailsPostRender : public Collection<WindTrailsPostRender>
{
	// Allocate and copy
	static void AllocateAndCopy(WindTrailsPostRender& rCurrent, const WindTrailsPostRender& rPrevious);

	// Update
	static void Update(game::Frame& __restrict rFrame, const game::Frame& __restrict rPreviousFrame);
	static void PreCollision(game::Frame& __restrict rFrame, const game::Frame& __restrict rPreviousFrame);
	static void PostCollision(game::Frame& __restrict rFrame, const game::Frame& __restrict rPreviousFrame);
	static void AreaDamage(game::Frame& __restrict rFrame, const game::Frame& __restrict rPreviousFrame);
	static void Transfer(game::Frame& __restrict rFrame);
	static void Destroy(game::Frame& __restrict rFrame);
	static void Spawn(game::Frame& __restrict rFrame);

	// Add wind trail (Sync pattern - owner manages lifetime)
	static void Add(game::Frame& __restrict rFrame, wind_trail_t& rId);

	// Remove wind trail by ID
	static void Remove(game::Frame& __restrict rFrame, wind_trail_t& rId);

	wind_trail_t* __restrict puiIds = nullptr;
	auto Members(this auto&& rSelf) { return std::tie(rSelf.puiIds); }

	// Utility
	bool operator==(const WindTrailsPostRender& rOther) const;
};

} // namespace engine
