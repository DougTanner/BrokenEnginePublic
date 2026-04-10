#pragma once

#include "Frame/Collections/Collection.h"
#include "Frame/GridCoord.h"

namespace game
{

struct Frame;

}

namespace engine
{

struct FrameStaticData;

enum class PusherFlags : uint8_t
{
	kTypeNone    = 0x00,
	kTypeDefault = 0x01,
	kTypeMines   = 0x02,
};
using PusherFlags_t = common::Flags<PusherFlags>;

struct PushersInterpolate : public Collection<PushersInterpolate, CollectionFlags::kIdToIndex>
{
	// Register
	static void Register();

	// Graphics resources
	static void GraphicsResources() {}

	// Allocate and copy
	static void AllocateAndCopy(PushersInterpolate& rCurrent, const PushersInterpolate& rPrevious);

	// Update
	static void Update(game::FrameInterpolate& __restrict rFrameInterpolate, const game::Frame& __restrict rPreviousFrame);

	// Sync data for owner collections to update pusher state
	struct SyncData
	{
		XMVECTOR vecPosition;
		float fRadius;
		float fIntensity;
		float fPower;
		PusherFlags_t flags;
	};

	// Sync pusher state from owner collection
	static void XM_CALLCONV Sync(game::FrameInterpolate& rFrameInterpolate, id_t id, const SyncData& rData);

	// Zone system - builds spatial acceleration structure each frame
	static void SetupZones(game::Frame& __restrict rFrame);

	// Query force at position using zone acceleration
	static XMVECTOR XM_CALLCONV ApplyPush(const game::FrameInterpolate& rFrameInterpolate, FXMVECTOR vecPosition, id_t uiIgnorePusher = id_t {}, PusherFlags_t includeFlags = PusherFlags::kTypeDefault, PusherFlags_t excludeFlags = PusherFlags::kTypeMines);

#if defined(BT_CLIENT)
	// Render
	static void BeginRender(int64_t iCommandBuffer, const std::unordered_map<GridCoord, game::FrameInterpolate>& rRenderInterpolates, const std::vector<GridCoord>& rActiveCoords);
	static void Render(const game::FrameInterpolate& __restrict rFrameInterpolate, int64_t iCommandBuffer);
	static void EndRender(int64_t iCommandBuffer);
#endif

	// Member arrays (SOA)
	XMVECTOR* __restrict pVecPositions = nullptr;
	float* __restrict pfRadii = nullptr;
	float* __restrict pfIntensities = nullptr;
	float* __restrict pfPowers = nullptr;
	PusherFlags_t* __restrict pFlags = nullptr;

	auto Members(this auto&& rSelf)
	{
		return std::tie(rSelf.pVecPositions, rSelf.pfRadii, rSelf.pfIntensities, rSelf.pfPowers, rSelf.pFlags);
	}

	bool LogDifferences(const PushersInterpolate& rOther) const;
};
using pusher_t = PushersInterpolate::id_t;

struct PushersPostRender : public Collection<PushersPostRender>
{
	// Allocate and copy
	static void AllocateAndCopy(PushersPostRender& rCurrent, const PushersPostRender& rPrevious);

	// Update
	static void Update(game::Frame& __restrict rFrame, const game::Frame& __restrict rPreviousFrame, const FrameStaticData& rStaticData);
	static void PreCollision(game::Frame& __restrict rFrame, const game::Frame& __restrict rPreviousFrame, const FrameStaticData& rStaticData);

	// Add pusher
	static void Add(game::Frame& __restrict rFrame, pusher_t& rId);

	// Remove pusher by ID
	static void Remove(game::Frame& __restrict rFrame, pusher_t& rId);
	static void PostCollision(game::Frame& __restrict rFrame, const game::Frame& __restrict rPreviousFrame, const FrameStaticData& rStaticData);
	static void AreaDamage(game::Frame& __restrict rFrame, const game::Frame& __restrict rPreviousFrame, const FrameStaticData& rStaticData);
	static void Transfer(game::Frame& __restrict rFrame, const FrameStaticData& rStaticData);
	static void Destroy(game::Frame& __restrict rFrame, const FrameStaticData& rStaticData);
	static void Spawn(game::Frame& __restrict rFrame, const FrameStaticData& rStaticData);

	pusher_t* __restrict puiIds = nullptr;
	auto Members(this auto&& rSelf) { return std::tie(rSelf.puiIds); }

	bool LogDifferences(const PushersPostRender& rOther) const;
};

extern template struct Collection<PushersInterpolate, CollectionFlags::kIdToIndex>;
extern template struct Collection<PushersPostRender>;

} // namespace engine
