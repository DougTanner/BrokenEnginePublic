#pragma once

#include "Frame/Collections/Collection.h"
#include "Shaders/ShaderLayouts.h"

namespace engine
{

enum class BillboardFlags : uint8_t
{
	kOffscreenOnly   = 0x01,
	kOffscreenRotate = 0x02,

	// DT: GAMELOGIC
	kTypeNone  = 0x04,
	kTypeArmor = 0x08,
};
using BillboardFlags_t = common::Flags<BillboardFlags>;

struct BillboardsType
{
	common::crc_t crc = 0;
	float fSize = 1.0f;
	float fAlpha = 1.0f;
};

struct BillboardsInterpolate : public Collection<BillboardsInterpolate, CollectionFlags::kIdToIndex>,
                               public TypeRegistry<BillboardsType>,
                               public Renderable<BillboardsInterpolate, "Billboards", {RenderableFlags::kBillboards}>
{
	// Register
	static void Register();

	// Graphics resources
	static void GraphicsResources();

	// SyncData for parent-provided values
	struct SyncData
	{
		XMVECTOR vecPosition;
		uint8_t uiTypeIndex;
		uint8_t uiFlags;
		float fRotation;
		float fExtra;
	};

	// Sync owned billboard with parent-provided data
	static void Sync(game::FrameInterpolate& rFrameInterpolate, id_t id, const SyncData& rData);

	// Interpolate
	static void Update(BillboardsInterpolate& __restrict rCurrent, const game::Frame& __restrict rPreviousFrame, float fDeltaTime);

	uint8_t* __restrict puiTypeIndices = nullptr;
	uint8_t* __restrict puiFlags = nullptr;
	float* __restrict pfRotations = nullptr;
	float* __restrict pfExtra = nullptr;
	XMVECTOR* __restrict pVecPositions = nullptr;
	auto Members(this auto&& rSelf) { return std::tie(rSelf.puiTypeIndices, rSelf.puiFlags, rSelf.pfRotations, rSelf.pfExtra, rSelf.pVecPositions); }

	// Render
	static void Render(const game::FrameInterpolate& __restrict rFrameInterpolate, int64_t iCommandBuffer);

	// Utility
	bool operator==(const BillboardsInterpolate& rOther) const;
};
using billboard_t = BillboardsInterpolate::id_t;

struct BillboardsPostRender : public Collection<BillboardsPostRender>
{
	// Update
	static void Update(BillboardsPostRender& __restrict rCurrent, const BillboardsPostRender& __restrict rPrevious, float fDeltaTime);
	static void PreCollision(game::Frame& __restrict rFrame, const game::Frame& __restrict rPreviousFrame, float fDeltaTime);
	static void Add(game::Frame& __restrict rFrame, billboard_t&& rId, uint8_t) = delete;
	static void Add(game::Frame& __restrict rFrame, billboard_t& rId, uint8_t uiTypeIndex);
	static void Remove(game::Frame& __restrict rFrame, billboard_t& rId);

	billboard_t* __restrict puiIds = nullptr;
	auto Members(this auto&& rSelf) { return std::tie(rSelf.puiIds); }

	// Utility
	bool operator==(const BillboardsPostRender& rOther) const;
};

static_assert(sizeof(shaders::BillboardLayout) == kBillboardLayoutSize);

} // namespace engine
