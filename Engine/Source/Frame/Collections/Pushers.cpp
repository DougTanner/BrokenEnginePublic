#include "Pushers.h"

#include "Frame/Frame.h"
#include "Graphics/Graphics.h"
#include "Profile/ProfileManager.h"

namespace engine
{

// Zone acceleration structure (preserved from original pool implementation)
alignas(64) uint16_t gppuiPushersPerZone[kiPusherZones][kiPusherZones] {};
alignas(64) int64_t gpppuiPusherZones[kiPusherZones][kiPusherZones][kiMaxPushersPerZone] {};

float gfPusherArenaLeft = -0.5f * kfPusherArenaSize;
float gfPusherArenaTop = 0.5f * kfPusherArenaSize;

void PushersInterpolate::Register()
{
}

void PushersInterpolate::AllocateAndCopy(PushersInterpolate& rCurrent, const PushersInterpolate& rPrevious)
{
	Allocate(rCurrent, rPrevious, rCurrent.Members());
}

void PushersInterpolate::Update([[maybe_unused]] game::FrameInterpolate& __restrict rFrameInterpolate, [[maybe_unused]] const game::Frame& __restrict rPreviousFrame)
{
	PushersInterpolate& __restrict rCurrent = rFrameInterpolate.pushers;
	const PushersInterpolate& rPrevious = rPreviousFrame.interpolate.pushers;

	for (int64_t i = 0; i < rCurrent.iCount; ++i)
	{
		// Load
		XMVECTOR vecPosition = rPrevious.pVecPositions[i];
		float fRadius = rPrevious.pfRadii[i];
		float fIntensity = rPrevious.pfIntensities[i];
		float fPower = rPrevious.pfPowers[i];
		PusherFlags_t flags = rPrevious.pFlags[i];

		// Save
		rCurrent.pVecPositions[i] = vecPosition;
		rCurrent.pfRadii[i] = fRadius;
		rCurrent.pfIntensities[i] = fIntensity;
		rCurrent.pfPowers[i] = fPower;
		rCurrent.pFlags[i] = flags;
	}

	gpProfileManager->SetCount(kCpuCounterPushers, rCurrent.iCount);
}

void XM_CALLCONV PushersInterpolate::Sync(game::FrameInterpolate& rFrameInterpolate, id_t id, const SyncData& rData)
{
	if (!id.IsValid())
	{
		return;
	}

	PushersInterpolate& rPushers = rFrameInterpolate.pushers;
	int64_t iIndex = rPushers.IdToIndex(id);

	rPushers.pVecPositions[iIndex] = rData.vecPosition;
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
	XMVECTOR vecPlayerPos = rFrame.interpolate.players.iCount > 0 ? rFrame.interpolate.players.pVecPositions[0] : XMVectorZero();
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

				gpppuiPusherZones[x][y][iPushersPerZone] = i;
				++gppuiPushersPerZone[x][y];
			}
		}
	}
}

XMVECTOR XM_CALLCONV PushersInterpolate::ApplyPush(const game::FrameInterpolate& rFrameInterpolate, FXMVECTOR vecPosition, id_t uiIgnorePusher, PusherFlags_t includeFlags, PusherFlags_t excludeFlags)
{
	const PushersInterpolate& rCurrent = rFrameInterpolate.pushers;

	auto vecPosition2d = XMVectorSetZ(vecPosition, 0.0f);
	XMFLOAT2A f2Position {};
	XMStoreFloat2A(&f2Position, vecPosition);

	// Get zone for query position
	int64_t iZoneX = std::clamp(static_cast<int64_t>((f2Position.x - gfPusherArenaLeft) / kfPusherZoneSize), 0ll, kiPusherZones - 1);
	int64_t iZoneY = std::clamp(static_cast<int64_t>(-(f2Position.y - gfPusherArenaTop) / kfPusherZoneSize), 0ll, kiPusherZones - 1);
	int64_t* piZone = gpppuiPusherZones[iZoneX][iZoneY];
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
		if (flags & excludeFlags) [[unlikely]]
		{
			continue;
		}

		// Skip if flags don't match include mask
		if (!(flags & includeFlags)) [[unlikely]]
		{
			continue;
		}

		auto vecPusherPosition = XMVectorSetW(rCurrent.pVecPositions[i], 1.0f);
		if (XMVector3NearEqual(vecPosition2d, vecPusherPosition, XMVectorReplicate(kfEpsilon))) [[unlikely]]
		{
			continue;
		}

		auto vecFromPusher = XMVectorSubtract(vecPosition2d, vecPusherPosition);
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

void PushersPostRender::AllocateAndCopy(PushersPostRender& rCurrent, const PushersPostRender& rPrevious)
{
	Allocate(rCurrent, rPrevious, rCurrent.Members());

	if (rCurrent.iCount > 0)
	{
		std::memcpy(rCurrent.puiIds, rPrevious.puiIds, rCurrent.iCount * sizeof(rCurrent.puiIds[0]));
	}
}

void PushersPostRender::Update([[maybe_unused]] game::Frame& __restrict rFrame, [[maybe_unused]] const game::Frame& __restrict rPreviousFrame)
{
}

void PushersPostRender::PreCollision([[maybe_unused]] game::Frame& __restrict rFrame, [[maybe_unused]] const game::Frame& __restrict rPreviousFrame)
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
}

void PushersPostRender::Remove(game::Frame& __restrict rFrame, pusher_t& rId)
{
	ASSERT(rId.IsValid());

	PushersInterpolate& rInterpolate = rFrame.interpolate.pushers;
	PushersPostRender& rPostRender = rFrame.postRender.pushers;

	RemoveIndexableElement(rInterpolate, rPostRender, rId, rInterpolate.Members(), rPostRender.Members());

	rId = {};
}

void PushersPostRender::PostCollision([[maybe_unused]] game::Frame& __restrict rFrame, [[maybe_unused]] const game::Frame& __restrict rPreviousFrame)
{
}

void PushersPostRender::AreaDamage([[maybe_unused]] game::Frame& __restrict rFrame, [[maybe_unused]] const game::Frame& __restrict rPreviousFrame)
{
}

void PushersPostRender::Destroy([[maybe_unused]] game::Frame& __restrict rFrame)
{
}

void PushersPostRender::Spawn([[maybe_unused]] game::Frame& __restrict rFrame)
{
}

bool PushersInterpolate::operator==(const PushersInterpolate& rOther) const
{
	bool bEqual = true;
	bEqual &= common::BreakOnNotEqual<Collection>(*this, rOther);

	for (int64_t i = 0; i < iCount; ++i)
	{
		bEqual &= common::BreakOnNotEqual(pVecPositions[i], rOther.pVecPositions[i]);
		bEqual &= common::BreakOnNotEqual(pfRadii[i], rOther.pfRadii[i]);
		bEqual &= common::BreakOnNotEqual(pfIntensities[i], rOther.pfIntensities[i]);
		bEqual &= common::BreakOnNotEqual(pfPowers[i], rOther.pfPowers[i]);
		bEqual &= common::BreakOnNotEqual(pFlags[i], rOther.pFlags[i]);
	}

	return bEqual;
}

bool PushersPostRender::operator==(const PushersPostRender& rOther) const
{
	bool bEqual = true;
	bEqual &= common::BreakOnNotEqual<Collection>(*this, rOther);

	for (int64_t i = 0; i < iCount; ++i)
	{
		bEqual &= common::BreakOnNotEqual(puiIds[i], rOther.puiIds[i]);
	}

	return bEqual;
}

void PushersInterpolate::Render([[maybe_unused]] const game::FrameInterpolate& __restrict rFrameInterpolate, [[maybe_unused]] int64_t iCommandBuffer)
{
}

} // namespace engine
