#include "Game.h"

#include "Network/ClientSession.h"
#include "Frame/Collections/Players/Players.h"

namespace game
{

#if defined(BT_CLIENT)

std::vector<engine::GridCoord> ClientSession::ComputeDesiredCoords() const
{
	std::vector<engine::GridCoord> desiredCoords;
	desiredCoords.reserve(5);

	bool bDead = gpGame->mGameFlags & engine::GameFlags::kDeathScreen;

	if (gpGame->HumanPlayerId().IsValid())
	{
		desiredCoords.push_back(gpGame->mHumanGridCoord);
	}
	else if (bDead)
	{
		// Dead: keep death coord + pre-emptive origin for respawn
		desiredCoords.push_back(gpGame->mHumanGridCoord);
		if (gpGame->mHumanGridCoord != engine::kOriginCoord)
		{
			desiredCoords.push_back(engine::kOriginCoord);
		}
	}
	else
	{
		// Not yet assigned: just origin
		desiredCoords.push_back(engine::kOriginCoord);
	}

	return desiredCoords;
}

void ClientSession::UpdateSubscriptions()
{
	if (mpClientNetwork == nullptr)
	{
		return;
	}

	// Player alive but not yet in assigned cell (mid-transfer): maintain current subscriptions
	// Only guard when coord has confirmed server data — initial spawn must subscribe first
	if (gpGame->HumanPlayerId().IsValid() && gpGame->mCoordFrames.contains(gpGame->mHumanGridCoord)
		&& gpGame->mCoordFrames.at(gpGame->mHumanGridCoord).iConfirmedTick >= 0
		&& !gpGame->CurrentFrame(gpGame->mHumanGridCoord).interpolate.pPlayers->idToIndexMap.contains(gpGame->HumanPlayerId()))
	{
		return;
	}

	// Heap: vector operations for subscription queue
	ScopedSuppressAllocationTracking suppressAllocationTracking;

	std::vector<engine::GridCoord> desiredCoords = ComputeDesiredCoords();

	UnsubscribeStaleCoords(desiredCoords);
	BuildSubscriptionQueue(desiredCoords);

	// Start subscribing
	TrySubscribeNext();
}

#endif // BT_CLIENT

} // namespace game
