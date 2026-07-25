#pragma once

#if defined(BT_CLIENT)

#include "Frame/Collections/Collection.h"
#include "Frame/GridCoord.h"

namespace engine
{

struct FrameStaticData;

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
	static constexpr bool kbManualRender = true;

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
	XMVECTOR* __restrict pVecSmoothedPositions = nullptr;
	float* __restrict pfIntensities = nullptr;
	float* __restrict pfStartTimes = nullptr;

	auto Members(this auto&& rSelf)
	{
		return std::tie(rSelf.puiTypeIndices, rSelf.pVecPositions, rSelf.pVecSmoothedPositions, rSelf.pfIntensities, rSelf.pfStartTimes);
	}

	// Graphics resources
	static void GraphicsResources();

	// Render
	static void BeginRender(int64_t iCommandBuffer, const std::unordered_map<GridCoord, game::FrameInterpolate>& rRenderInterpolates, const std::vector<GridCoord>& rActiveCoords);
	static void Render(const game::FrameInterpolate& __restrict rFrameInterpolate, int64_t iCommandBuffer);
	static void EndRender(int64_t iCommandBuffer);
};
using smoke_trails_t = SmokeTrailsInterpolate::id_t;

struct SmokeTrailsPostRender : public Collection<SmokeTrailsPostRender>
{
	// Allocate and copy
	static void AllocateAndCopy(SmokeTrailsPostRender& rCurrent, const SmokeTrailsPostRender& rPrevious);

	// Update
	static void Update(game::Frame& __restrict rFrame, const game::Frame& __restrict rPreviousFrame, const FrameStaticData& rStaticData);

	// Add trail
	static void Add(game::Frame& __restrict rFrame, smoke_trails_t& rId, uint8_t uiTypeIndex, smoke_trails_t reuseId = {});

	// Remove trail by ID
	static void Remove(game::Frame& __restrict rFrame, smoke_trails_t& rId);

	smoke_trails_t* __restrict puiIds = nullptr;
	auto Members(this auto&& rSelf)
	{
		return std::tie(rSelf.puiIds);
	}

};

extern template struct Collection<SmokeTrailsInterpolate, CollectionFlags::kIdToIndex>;
extern template struct Collection<SmokeTrailsPostRender>;

} // namespace engine

#endif // BT_CLIENT
