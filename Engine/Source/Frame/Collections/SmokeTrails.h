#pragma once

#ifdef BT_CLIENT

#include "Frame/Collections/Collection.h"
#include "Frame/GridCoord.h"

namespace engine
{

struct SmokeTrailsType
{
	common::crc_t crc = 0;
	uint32_t uiColor = 0xFFFFFFFF;
	float fWidth = 1.0f;
};

struct SmokeTrailsInterpolate : public Collection<SmokeTrailsInterpolate, CollectionFlags::kIdToIndex>,
                                public TypeRegistry<SmokeTrailsType>
{
	static constexpr const char* kName = "SmokeTrails";
	static constexpr common::crc_t kCrc = common::CrcConsteval("SmokeTrails");

	// Register
	static void Register();

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

	// Graphics resources
	static void GraphicsResources();

	// Reset render state (clears cached positions for world reset)
	static void ResetRenderState();

	// Render
	static void BeginRender(int64_t iCommandBuffer, const std::unordered_map<GridCoord, game::FrameInterpolate>& rRenderInterpolates, const std::vector<GridCoord>& rActiveCoords);
	static void Render(const game::FrameInterpolate& __restrict rFrameInterpolate, int64_t iCommandBuffer, uint16_t uiFrameId);
	static void EndRender(int64_t iCommandBuffer);
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
	static void Add(game::Frame& __restrict rFrame, smoke_trails_t& rId, uint8_t uiTypeIndex, smoke_trails_t reuseId = {});

	// Remove trail by ID
	static void Remove(game::Frame& __restrict rFrame, smoke_trails_t& rId);
	static void PostCollision(game::Frame& __restrict rFrame, const game::Frame& __restrict rPreviousFrame);
	static void AreaDamage(game::Frame& __restrict rFrame, const game::Frame& __restrict rPreviousFrame);
	static void Transfer(game::Frame& __restrict rFrame);
	static void Destroy(game::Frame& __restrict rFrame);
	static void Spawn(game::Frame& __restrict rFrame);

	smoke_trails_t* __restrict puiIds = nullptr;
	auto Members(this auto&& rSelf) { return std::tie(rSelf.puiIds); }

	// Utility
	bool operator==(const SmokeTrailsPostRender& rOther) const;
};

extern template struct Collection<SmokeTrailsInterpolate, CollectionFlags::kIdToIndex>;
extern template struct Collection<SmokeTrailsPostRender>;

} // namespace engine

#endif // BT_CLIENT
