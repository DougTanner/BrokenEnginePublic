#pragma once

#include "Frame/Collections/Collection.h"

namespace game
{

struct Frame;

}

namespace engine
{

// Zone system constants (preserved from original pool implementation)
inline constexpr float kfPusherArenaSize = 400.0f;
inline constexpr float kfPusherZoneSize = 8.0f;
inline constexpr int64_t kiPusherZones = static_cast<int64_t>(common::Ceil(kfPusherArenaSize / kfPusherZoneSize));
inline constexpr int64_t kiMaxPushersPerZone = 512;

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
	static XMVECTOR XM_CALLCONV ApplyPush(FXMVECTOR vecPosition, id_t uiIgnorePusher = id_t {}, PusherFlags_t includeFlags = PusherFlags::kTypeDefault, PusherFlags_t excludeFlags = PusherFlags::kTypeMines);

	// Render
	static void Render(const game::FrameInterpolate& __restrict rFrameInterpolate, int64_t iCommandBuffer);

	// Member arrays (SOA)
	XMVECTOR* __restrict pVecPositions = nullptr;
	float* __restrict pfRadii = nullptr;
	float* __restrict pfIntensities = nullptr;
	float* __restrict pfPowers = nullptr;
	PusherFlags_t* __restrict pFlags = nullptr;

	auto Members(this auto&& rSelf)
	{
		return std::tie(rSelf.pVecPositions, rSelf.pfRadii, rSelf.pfIntensities,
		                rSelf.pfPowers, rSelf.pFlags);
	}

	bool operator==(const PushersInterpolate& rOther) const;
};
using pusher_t = PushersInterpolate::id_t;

struct PushersPostRender : public Collection<PushersPostRender>
{
	// Allocate and copy
	static void AllocateAndCopy(PushersPostRender& rCurrent, const PushersPostRender& rPrevious);

	// Update
	static void Update(game::Frame& __restrict rFrame, const game::Frame& __restrict rPreviousFrame);
	static void PreCollision(game::Frame& __restrict rFrame, const game::Frame& __restrict rPreviousFrame);

	// Add pusher
	static void Add(game::Frame& __restrict rFrame, pusher_t& rId);

	// Remove pusher by ID
	static void Remove(game::Frame& __restrict rFrame, pusher_t& rId);
	static void PostCollision(game::Frame& __restrict rFrame, const game::Frame& __restrict rPreviousFrame);
	static void AreaDamage(game::Frame& __restrict rFrame, const game::Frame& __restrict rPreviousFrame);
	static void Destroy(game::Frame& __restrict rFrame);
	static void Spawn(game::Frame& __restrict rFrame);

	pusher_t* __restrict puiIds = nullptr;
	auto Members(this auto&& rSelf) { return std::tie(rSelf.puiIds); }

	bool operator==(const PushersPostRender& rOther) const;
};

} // namespace engine
