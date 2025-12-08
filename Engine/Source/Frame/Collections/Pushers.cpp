#include "Pushers.h"

#include "Frame/Frame.h"

namespace engine
{

// Zone acceleration structure (preserved from original pool implementation)
alignas(64) uint16_t gppuiPushersPerZone[kiPusherZones][kiPusherZones] {};
alignas(64) int64_t gpppuiPusherZones[kiPusherZones][kiPusherZones][kiMaxPushersPerZone] {};

float gfPusherArenaLeft = -0.5f * kfPusherArenaSize;
float gfPusherArenaTop = 0.5f * kfPusherArenaSize;

// Pointer to current frame's interpolate pushers (set in SetupZones for ApplyPush access)
const PushersInterpolate* gpCurrentPushersInterpolate = nullptr;

void PushersInterpolate::Register()
{
}

void PushersInterpolate::Update([[maybe_unused]] PushersInterpolate& __restrict rCurrent, [[maybe_unused]] const game::Frame& __restrict rPreviousFrame, [[maybe_unused]] float fDeltaTime)
{
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

	PROFILE_SET_COUNT(kCpuCounterPushers, rCurrent.iCount);
}

void PushersInterpolate::SetupZones([[maybe_unused]] game::Frame& __restrict rFrame)
{
	const PushersInterpolate& rCurrent = rFrame.interpolate.pushers;

	// Store pointer for ApplyPush access
	gpCurrentPushersInterpolate = &rCurrent;

	// Clear zone counts
	ZeroMemory(gppuiPushersPerZone, sizeof(gppuiPushersPerZone));

	// Center arena on player position
	gfPusherArenaLeft = XMVectorGetX(rFrame.interpolate.player.vecPosition) - 0.5f * kfPusherArenaSize;
	gfPusherArenaTop = XMVectorGetY(rFrame.interpolate.player.vecPosition) + 0.5f * kfPusherArenaSize;

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

XMVECTOR XM_CALLCONV PushersInterpolate::ApplyPush(FXMVECTOR vecPosition, id_t uiIgnorePusher, PusherFlags_t includeFlags, PusherFlags_t excludeFlags)
{
	ASSERT(gCurrentFrameTypeProcessing == FrameType::kPostRender);
	ASSERT(gpCurrentPushersInterpolate != nullptr);

	const PushersInterpolate& rCurrent = *gpCurrentPushersInterpolate;

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

void PushersPostRender::Update([[maybe_unused]] PushersPostRender& __restrict rCurrent, [[maybe_unused]] const PushersPostRender& __restrict rPrevious, [[maybe_unused]] float fDeltaTime)
{
	for (int64_t i = 0; i < rCurrent.iCount; ++i)
	{
		// Load
		pusher_t id = rPrevious.puiIds[i];

		// Save
		rCurrent.puiIds[i] = id;
	}
}

void PushersPostRender::PreCollision([[maybe_unused]] game::Frame& __restrict rFrame, [[maybe_unused]] const game::Frame& __restrict rPreviousFrame, [[maybe_unused]] float fDeltaTime)
{
}

void PushersPostRender::Add(game::Frame& __restrict rFrame, pusher_t& rId)
{
	PushersInterpolate& rInterpolate = rFrame.interpolate.pushers;
	PushersPostRender& rPostRender = rFrame.postRender.pushers;

	engine::GrowPairedCollections(rInterpolate, rPostRender, rInterpolate.Members(), rPostRender.Members());
	auto [uiSpawnIndex, newId] = engine::AddIndexableElement(rInterpolate, rPostRender, rFrame.postRender);
	rId = newId;
	rPostRender.puiIds[uiSpawnIndex] = newId;

	// Zero-init all members
	rInterpolate.pVecPositions[uiSpawnIndex] = XMVectorZero();
	rInterpolate.pfRadii[uiSpawnIndex] = 0.0f;
	rInterpolate.pfIntensities[uiSpawnIndex] = 0.0f;
	rInterpolate.pfPowers[uiSpawnIndex] = 0.0f;
	rInterpolate.pFlags[uiSpawnIndex] = {};
}

void PushersPostRender::Remove(game::Frame& __restrict rFrame, pusher_t& rId)
{
	if (!rId.IsValid())
	{
		return;
	}

	PushersInterpolate& rInterpolate = rFrame.interpolate.pushers;
	PushersPostRender& rPostRender = rFrame.postRender.pushers;

	engine::RemoveIndexableElement(rInterpolate, rPostRender, rId, rInterpolate.Members(), rPostRender.Members());

	rId = {};
}

void XM_CALLCONV PushersPostRender::UpdatePosition(game::Frame& __restrict rFrame, pusher_t id, FXMVECTOR vecPosition)
{
	if (!id.IsValid())
	{
		return;
	}

	PushersInterpolate& rInterpolate = rFrame.interpolate.pushers;
	int64_t iIndex = static_cast<int64_t>(rInterpolate.idToIndexMap.at(id));
	rInterpolate.pVecPositions[iIndex] = vecPosition;
}

void PushersPostRender::UpdateIntensity(game::Frame& __restrict rFrame, pusher_t id, float fIntensity)
{
	if (!id.IsValid())
	{
		return;
	}

	PushersInterpolate& rInterpolate = rFrame.interpolate.pushers;
	int64_t iIndex = static_cast<int64_t>(rInterpolate.idToIndexMap.at(id));
	rInterpolate.pfIntensities[iIndex] = fIntensity;
}

void PushersPostRender::UpdateRadius(game::Frame& __restrict rFrame, pusher_t id, float fRadius)
{
	if (!id.IsValid())
	{
		return;
	}

	PushersInterpolate& rInterpolate = rFrame.interpolate.pushers;
	int64_t iIndex = static_cast<int64_t>(rInterpolate.idToIndexMap.at(id));
	rInterpolate.pfRadii[iIndex] = fRadius;
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

} // namespace engine
