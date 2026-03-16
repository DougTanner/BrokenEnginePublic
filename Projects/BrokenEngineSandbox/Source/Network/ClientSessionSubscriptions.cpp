#include "Game.h"

#include "Network/ClientSession.h"

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

		// Add quadrant neighbor coords for seamless cross-cell gameplay
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

	// Heap: vector operations for subscription queue
	ScopedSuppressAllocationTracking suppressAllocationTracking;

	std::vector<engine::GridCoord> desiredCoords = ComputeDesiredCoords();

	if constexpr (kbEnableQuadrantNeighborSubscriptions)
	{
		// Death/respawn screen: keep current subscriptions + subscribe to origin for respawn
		if (gpGame->mGameFlags & engine::GameFlags::kDeathScreen)
		{
			desiredCoords = mPreviousDesiredCoords;
			if (!desiredCoords.empty() && !std::ranges::contains(desiredCoords, engine::kOriginCoord))
			{
				desiredCoords.push_back(engine::kOriginCoord);
			}
		}
	}

	// Log when the desired subscription set changes // DT: TEMP
	if (desiredCoords != mPreviousDesiredCoords)
	{
		bool bDead = gpGame->mGameFlags & engine::GameFlags::kDeathScreen;
		const char* pcState = gpGame->HumanPlayerId().IsValid() ? "alive" : (bDead ? "dead" : "unassigned");
		Log(kLogNetwork, "Desired subscriptions changed State: {} HumanCoord: ({},{}) QuadrantDir: ({},{}) Count: {} -> {}", pcState, gpGame->mHumanGridCoord.x, gpGame->mHumanGridCoord.y, gpGame->miQuadrantDirX, gpGame->miQuadrantDirY, mPreviousDesiredCoords.size(), desiredCoords.size());
		for (const engine::GridCoord& rCoord : mPreviousDesiredCoords)
		{
			if (!std::ranges::contains(desiredCoords, rCoord))
			{
				Log(kLogNetwork, "  Removed ({},{})", rCoord.x, rCoord.y);
			}
		}
		for (const engine::GridCoord& rCoord : desiredCoords)
		{
			if (!std::ranges::contains(mPreviousDesiredCoords, rCoord))
			{
				Log(kLogNetwork, "  Added ({},{})", rCoord.x, rCoord.y);
			}
		}
		mPreviousDesiredCoords = desiredCoords;

		// DT TEMP
		const std::vector<engine::ClientCoordSlot>& rSlots = mpClientNetwork->GetCoordSlots();
		for (int64_t i = 0; i < std::ssize(rSlots); ++i)
		{
			const engine::ClientCoordSlot& rSlot = rSlots.at(i);
			if (rSlot.eState != engine::CoordSubscriptionState::kUnsubscribed)
			{
				Log(kLogNetwork, "  Slot {} Coord: ({},{}) State: {}", i, rSlot.coord.x, rSlot.coord.y, static_cast<int>(rSlot.eState));
			}
		}
	}

	UnsubscribeStaleCoords(desiredCoords);
	BuildSubscriptionQueue(desiredCoords);

	TrySubscribeNext();
}

#endif // BT_CLIENT

} // namespace game
