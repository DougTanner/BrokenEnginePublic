#include "Pushers.h"

#include "Frame/Collections/Players/Players.h"

namespace engine
{

// Zone system constants for spatial acceleration
constexpr float kfPusherArenaSize = 400.0f;
constexpr float kfPusherZoneSize = 8.0f;
constexpr int64_t kiPusherZones = static_cast<int64_t>(common::Ceil(kfPusherArenaSize / kfPusherZoneSize));
constexpr int64_t kiMaxPushersPerZone = 512;

// Zone acceleration structure: thread_local so each Dispatch worker and reconcile thread gets its own copy
alignas(64) thread_local uint16_t gppuiPushersPerZone[kiPusherZones][kiPusherZones] {};
alignas(64) thread_local int16_t gpppuiPusherZones[kiPusherZones][kiPusherZones][kiMaxPushersPerZone] {};

thread_local float gfPusherArenaLeft = -0.5f * kfPusherArenaSize;
thread_local float gfPusherArenaTop = 0.5f * kfPusherArenaSize;

void PushersInterpolate::Update([[maybe_unused]] game::FrameInterpolate& __restrict rFrameInterpolate, [[maybe_unused]] const game::Frame& __restrict rPreviousFrame)
{
	PushersInterpolate& __restrict rCurrent = rFrameInterpolate.pushers;
	const PushersInterpolate& rPrevious = rPreviousFrame.interpolate.pushers;
	CopyMemberRows(rCurrent.iCount, rCurrent.Members(), rPrevious.Members());
}

void XM_CALLCONV PushersInterpolate::Sync(game::FrameInterpolate& rFrameInterpolate, id_t id, const SyncData& rData)
{
	if (!id.IsValid())
	{
		return;
	}

	PushersInterpolate& rPushers = rFrameInterpolate.pushers;
	int64_t iIndex = rPushers.IdToIndex(id);

	rPushers.pVecPositions[iIndex] = XMVectorSetW(rData.vecPosition, 1.0f);
	rPushers.pfRadii[iIndex] = rData.fRadius;
	rPushers.pfIntensities[iIndex] = rData.fIntensity;
	rPushers.pfPowers[iIndex] = rData.fPower;
	rPushers.pFlags[iIndex] = rData.flags;
}

void PushersInterpolate::SetupZones([[maybe_unused]] game::Frame& __restrict rFrame)
{
	const PushersInterpolate& rCurrent = rFrame.interpolate.pushers;

	// Clear zone counts
	ZeroMemory(gppuiPushersPerZone, sizeof(gppuiPushersPerZone));

	// Center arena on player position
	XMVECTOR vecPlayerPos = rFrame.interpolate.pPlayers->iCount > 0 ? rFrame.interpolate.pPlayers->pVecPositions[0] : XMVectorZero();
	gfPusherArenaLeft = XMVectorGetX(vecPlayerPos) - 0.5f * kfPusherArenaSize;
	gfPusherArenaTop = XMVectorGetY(vecPlayerPos) + 0.5f * kfPusherArenaSize;

	for (int64_t i = 0; i < rCurrent.iCount; ++i)
	{
		XMFLOAT4A f4Position {};
		XMStoreFloat4A(&f4Position, rCurrent.pVecPositions[i]);
		float fRadius = rCurrent.pfRadii[i];

		// Calculate zone bounds based on position and radius
		int64_t iZoneStartX = static_cast<int64_t>((f4Position.x - fRadius - gfPusherArenaLeft) / kfPusherZoneSize);
		int64_t iZoneEndX = static_cast<int64_t>((f4Position.x + fRadius - gfPusherArenaLeft) / kfPusherZoneSize);
		int64_t iZoneStartY = static_cast<int64_t>(-(f4Position.y + fRadius - gfPusherArenaTop) / kfPusherZoneSize);
		int64_t iZoneEndY = static_cast<int64_t>(-(f4Position.y - fRadius - gfPusherArenaTop) / kfPusherZoneSize);

		// Skip if completely outside arena
		if (iZoneStartX >= kiPusherZones || iZoneEndX < 0 || iZoneStartY >= kiPusherZones || iZoneEndY < 0)
		{
			continue;
		}

		// Clamp to valid zone indices
		iZoneStartX = std::clamp(iZoneStartX, 0ll, kiPusherZones - 1);
		iZoneEndX = std::clamp(iZoneEndX, 0ll, kiPusherZones - 1);
		iZoneStartY = std::clamp(iZoneStartY, 0ll, kiPusherZones - 1);
		iZoneEndY = std::clamp(iZoneEndY, 0ll, kiPusherZones - 1);

		// Add pusher index to all overlapping zones
		for (int64_t y = iZoneStartY; y <= iZoneEndY; ++y)
		{
			for (int64_t x = iZoneStartX; x <= iZoneEndX; ++x)
			{
				int64_t iPushersPerZone = gppuiPushersPerZone[x][y];
				if (iPushersPerZone >= kiMaxPushersPerZone) [[unlikely]]
				{
					DEBUG_BREAK();
					continue;
				}

				gpppuiPusherZones[x][y][iPushersPerZone] = static_cast<int16_t>(i);
				++gppuiPushersPerZone[x][y];
			}
		}
	}
}

XMVECTOR XM_CALLCONV PushersInterpolate::ApplyPush(const game::FrameInterpolate& rFrameInterpolate, FXMVECTOR vecPosition, id_t uiIgnorePusher, PusherFlags_t includeFlags, PusherFlags_t excludeFlags)
{
	const PushersInterpolate& rCurrent = rFrameInterpolate.pushers;

	XMFLOAT2A f2Position {};
	XMStoreFloat2A(&f2Position, vecPosition);

	// Get zone for query position
	int64_t iZoneX = std::clamp(static_cast<int64_t>((f2Position.x - gfPusherArenaLeft) / kfPusherZoneSize), 0ll, kiPusherZones - 1);
	int64_t iZoneY = std::clamp(static_cast<int64_t>(-(f2Position.y - gfPusherArenaTop) / kfPusherZoneSize), 0ll, kiPusherZones - 1);
	int16_t* piZone = gpppuiPusherZones[iZoneX][iZoneY];
	int64_t iPushersInZone = gppuiPushersPerZone[iZoneX][iZoneY];

	// Look up ignore pusher index if valid
	int64_t iIgnoreIndex = -1;
	if (uiIgnorePusher.IsValid())
	{
		auto it = rCurrent.idToIndexMap.find(uiIgnorePusher);
		if (it != rCurrent.idToIndexMap.end())
		{
			iIgnoreIndex = static_cast<int64_t>(it->second);
		}
	}

	auto vecPush = XMVectorZero();
	for (int64_t j = 0; j < iPushersInZone; ++j)
	{
		int64_t i = piZone[j];

		// Skip ignored pusher
		if (i == iIgnoreIndex) [[unlikely]]
		{
			continue;
		}

		PusherFlags_t flags = rCurrent.pFlags[i];

		// Skip if flags match exclude mask
		if (flags.Mask(excludeFlags) != 0u) [[unlikely]]
		{
			continue;
		}

		// Skip if flags don't match include mask
		if (flags.Mask(includeFlags) == 0u) [[unlikely]]
		{
			continue;
		}

		auto vecPusherPosition = rCurrent.pVecPositions[i];
		if (XMVector3NearEqual(vecPosition, vecPusherPosition, XMVectorReplicate(kfEpsilon))) [[unlikely]]
		{
			continue;
		}

		auto vecFromPusher = XMVectorSubtract(vecPosition, vecPusherPosition);
		auto vecDistanceSquared = XMVector3LengthSq(vecFromPusher);
		float fRadius = rCurrent.pfRadii[i];
		if (XMVectorGetX(vecDistanceSquared) > fRadius * fRadius) [[likely]]
		{
			continue;
		}

		// Calculate intensity with power falloff
		auto vecIntensity = XMVectorSubtract(XMVectorReplicate(1.0f), XMVectorDivide(vecDistanceSquared, XMVectorReplicate(fRadius * fRadius)));
		vecIntensity = XMVectorPow(vecIntensity, XMVectorReplicate(rCurrent.pfPowers[i]));
		vecIntensity = XMVectorMultiply(XMVectorReplicate(rCurrent.pfIntensities[i]), vecIntensity);

		auto vecFromPusherNormal = XMVector3Normalize(vecFromPusher);
		vecPush = XMVectorMultiplyAdd(vecIntensity, vecFromPusherNormal, vecPush);
	}

	return vecPush;
}

void PushersPostRender::Update([[maybe_unused]] game::Frame& __restrict rFrame, [[maybe_unused]] const game::Frame& __restrict rPreviousFrame, [[maybe_unused]] const FrameStaticData& rStaticData)
{
}

void PushersPostRender::Add(game::Frame& __restrict rFrame, pusher_t& rId)
{
	ASSERT(!rId.IsValid());

	PushersInterpolate& rInterpolate = rFrame.interpolate.pushers;
	PushersPostRender& rPostRender = rFrame.postRender.pushers;

	GrowPairedCollections(rInterpolate, rPostRender, rInterpolate.Members(), rPostRender.Members());
	auto [uiSpawnIndex, newId] = AddIndexableElement(rInterpolate, rPostRender, rFrame.postRender);
	rId = newId;
	rPostRender.puiIds[uiSpawnIndex] = newId;

	ZeroMemberRow(uiSpawnIndex, rInterpolate.Members());
}

void PushersPostRender::Remove(game::Frame& __restrict rFrame, pusher_t& rId)
{
	PushersInterpolate& rInterpolate = rFrame.interpolate.pushers;
	PushersPostRender& rPostRender = rFrame.postRender.pushers;

	RemoveIndexableElementAndClearHandle(rInterpolate, rPostRender, rId, rInterpolate.Members(), rPostRender.Members());
}

} // namespace engine
