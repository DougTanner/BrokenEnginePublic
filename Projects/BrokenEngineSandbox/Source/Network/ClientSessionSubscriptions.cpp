#include "Game.h"

#include "Network/ClientSession.h"

namespace game
{

#if defined(BT_CLIENT)

void ClientSession::UpdateDesiredCoords(const char* pcReason)
{
	std::vector<engine::GridCoord> desiredCoords;
	desiredCoords.reserve(5);

	bool bDead = gpGame->mGameFlags & engine::GameFlags::kDeathScreen;

	if (gpGame->HumanPlayerId().IsValid())
	{
		desiredCoords.push_back(gpGame->mHumanGridCoord);

		if constexpr (kbEnableQuadrantNeighborSubscriptions)
		{
			engine::GridCoord quadrantOffsets[3];
			quadrantOffsets[0] = {.x = gpGame->miQuadrantDirX, .y = 0};
			quadrantOffsets[1] = {.x = 0, .y = gpGame->miQuadrantDirY};
			quadrantOffsets[2] = {.x = gpGame->miQuadrantDirX, .y = gpGame->miQuadrantDirY};

			for (const engine::GridCoord& rOffset : quadrantOffsets)
			{
				desiredCoords.push_back({gpGame->mHumanGridCoord.x + rOffset.x, gpGame->mHumanGridCoord.y + rOffset.y});
			}
		}
	}
	else if (bDead)
	{
		if constexpr (kbEnableQuadrantNeighborSubscriptions)
		{
			// Death screen: keep current subscriptions + ensure origin for respawn
			desiredCoords = mDesiredCoords;
			if (!desiredCoords.empty() && !std::ranges::contains(desiredCoords, engine::kOriginCoord))
			{
				desiredCoords.push_back(engine::kOriginCoord);
			}
		}
		else
		{
			desiredCoords.push_back(gpGame->mHumanGridCoord);
			if (gpGame->mHumanGridCoord != engine::kOriginCoord)
			{
				desiredCoords.push_back(engine::kOriginCoord);
			}
		}
	}
	else
	{
		desiredCoords.push_back(engine::kOriginCoord);
	}

	if (desiredCoords != mDesiredCoords)
	{
		Log(kLogNetwork, "Desired subscriptions changed Reason: {} Count: {} -> {}", pcReason, mDesiredCoords.size(), desiredCoords.size());
		for (const engine::GridCoord& rCoord : mDesiredCoords)
		{
			if (!std::ranges::contains(desiredCoords, rCoord))
			{
				Log(kLogNetwork, "  Removed ({},{})", rCoord.x, rCoord.y);
			}
		}
		for (const engine::GridCoord& rCoord : desiredCoords)
		{
			if (!std::ranges::contains(mDesiredCoords, rCoord))
			{
				Log(kLogNetwork, "  Added ({},{})", rCoord.x, rCoord.y);
			}
		}
		mDesiredCoords = desiredCoords;
	}
}

void ClientSession::UpdateSubscriptions()
{
	if (mpClientNetwork == nullptr)
	{
		return;
	}

	// Heap: vector operations for subscription queue
	ScopedSuppressAllocationTracking suppressAllocationTracking;

	UnsubscribeStaleCoords(mDesiredCoords);
	BuildSubscriptionQueue(mDesiredCoords);
	TrySubscribeNext();
}

#endif // BT_CLIENT

} // namespace game
