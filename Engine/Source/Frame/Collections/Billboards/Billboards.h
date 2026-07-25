#pragma once

#if defined(BT_CLIENT)

#include "Frame/Collections/Collection.h"
#include "Frame/GridCoord.h"

namespace engine
{

struct FrameStaticData;

enum class BillboardFlags : uint8_t
{
	kOffscreenOnly   = 0x01,
	kOffscreenRotate = 0x02,
};
using BillboardFlags_t = common::Flags<BillboardFlags>;

struct BillboardsType
{
	common::crc_t crc = 0;
	float fSize = 1.0f;
	float fAlpha = 1.0f;
	uint8_t uiGameType = 0;
};

struct BillboardsInterpolate : public Collection<BillboardsInterpolate, CollectionFlags::kIdToIndex>,
                               public TypeRegistry<BillboardsType>
{
	static constexpr const char* kName = "Billboards";
	static constexpr common::crc_t kCrc = common::CrcConsteval("Billboards");

	// Allocate and copy
	static void AllocateAndCopy(BillboardsInterpolate& rCurrent, const BillboardsInterpolate& rPrevious);

	// SyncData for parent-provided values
	struct SyncData
	{
		XMVECTOR vecPosition;
		uint8_t uiTypeIndex;
		BillboardFlags_t flags;
		float fRotation;
		float fExtra;
	};

	// Sync owned billboard with parent-provided data
	static void Sync(game::FrameInterpolate& rFrameInterpolate, id_t id, const SyncData& rData);

	// Interpolate
	static void Update(game::FrameInterpolate& __restrict rFrameInterpolate, const game::Frame& __restrict rPreviousFrame);

	uint8_t* __restrict puiTypeIndices = nullptr;
	BillboardFlags_t* __restrict pFlags = nullptr;
	float* __restrict pfRotations = nullptr;
	float* __restrict pfExtra = nullptr;
	XMVECTOR* __restrict pVecPositions = nullptr;
	auto Members(this auto&& rSelf) { return std::tie(rSelf.puiTypeIndices, rSelf.pFlags, rSelf.pfRotations, rSelf.pfExtra, rSelf.pVecPositions); }

	// Graphics resources
	static void GraphicsResources();

	// Render
	static void BeginRender(int64_t iCommandBuffer, const std::unordered_map<GridCoord, game::FrameInterpolate>& rRenderInterpolates, const std::vector<GridCoord>& rActiveCoords);
	static void Render(const game::FrameInterpolate& __restrict rFrameInterpolate, int64_t iCommandBuffer);
	static void EndRender(int64_t iCommandBuffer);
};
using billboard_t = BillboardsInterpolate::id_t;

struct BillboardsPostRender : public Collection<BillboardsPostRender>
{
	// Allocate and copy
	static void AllocateAndCopy(BillboardsPostRender& rCurrent, const BillboardsPostRender& rPrevious);

	// Update
	static void Update(game::Frame& __restrict rFrame, const game::Frame& __restrict rPreviousFrame, const FrameStaticData& rStaticData);
	static void Add(game::Frame& __restrict rFrame, billboard_t&& rId, uint8_t) = delete;
	static void Add(game::Frame& __restrict rFrame, billboard_t& rId, uint8_t uiTypeIndex);
	static void Remove(game::Frame& __restrict rFrame, billboard_t& rId);

	billboard_t* __restrict puiIds = nullptr;
	auto Members(this auto&& rSelf) { return std::tie(rSelf.puiIds); }

};

extern template struct Collection<BillboardsInterpolate, CollectionFlags::kIdToIndex>;
extern template struct Collection<BillboardsPostRender>;

} // namespace engine

#endif // BT_CLIENT
